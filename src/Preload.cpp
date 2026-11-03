#include "Preload.h"

#include "Detectors.h"
#include "Hooks.h"
#include "Hud.h"
#include "InteriorSim.h"
#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr std::uint32_t kStateUnknown = 0xFF;
	constexpr std::uint32_t kUnloaded = 0;                 // TESObjectCELL::CellState
	constexpr std::uint32_t kUnattached = 4;
	constexpr std::uint32_t kAttached = 7;

	// Interior cells are permanent forms, so the pointer is kept.
	struct InteriorPin
	{
		RE::TESObjectCELL* cell{ nullptr };
		std::uint32_t      lastState{ kStateUnknown };
	};

	bool                     g_initialized{ false };
	bool                     g_active{ false };
	InteriorPin              g_interior;
	std::int64_t             g_nextRefreshUs{ 0 };
	std::uint64_t            g_keepCalls{ 0 };  // QueueInteriorCellLoad calls for the kept interior
	std::vector<std::int64_t> g_keepLoadsUs;    // recent loads started by KeepInterior
	std::int64_t             g_keepPausedUntilUs{ 0 };
	std::atomic<std::uint64_t> g_blockedUnloads{ 0 };  // AE#13370 calls skipped for the kept interior
	std::atomic<std::uint64_t> g_blockedCancels{ 0 };  // AE#18547 calls skipped for it
	std::atomic<std::uint64_t> g_blockedCellUnloads{ 0 };  // AE#13721 calls skipped for it
	std::atomic<bool>          g_keepActive{ false };  // keep it now: decided on the main thread, read by hooks on any thread
	std::mutex                 g_callerLock;
	std::set<std::string>      g_unloadCallers;  // callers of AE#13721 already traced

	bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
	{
		const auto lower = [](char a_c) { return (a_c >= 'A' && a_c <= 'Z') ? static_cast<char>(a_c - 'A' + 'a') : a_c; };
		return std::ranges::equal(a_lhs, a_rhs, [&](char a, char b) { return lower(a) == lower(b); });
	}

	bool HasEditorID(const RE::TESObjectCELL* a_cell, std::string_view a_editorID)
	{
		const char* edid = a_cell ? a_cell->GetFormEditorID() : nullptr;
		return edid && IEquals(edid, a_editorID);
	}

	// Not every editor ID is kept in the runtime map, so fall back to scanning cells.
	RE::TESObjectCELL* FindCell(std::string_view a_editorID)
	{
		if (auto* cell = RE::TESForm::LookupByEditorID<RE::TESObjectCELL>(a_editorID)) {
			return cell;
		}
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return nullptr;
		}
		for (auto* cell : data->interiorCells) {
			if (HasEditorID(cell, a_editorID)) {
				return cell;
			}
		}
		for (auto* world : data->GetFormArray<RE::TESWorldSpace>()) {
			if (!world) {
				continue;
			}
			for (auto& entry : world->cellMap) {
				if (HasEditorID(entry.second, a_editorID)) {
					return entry.second;
				}
			}
		}
		return nullptr;
	}

	void Init()
	{
		g_initialized = true;
		const auto& settings = Settings::Get();
		if (!settings.preloadEnabled) {
			return;
		}

		Trace::Args args;

		if (settings.preloadLinkedInteriors) {
			auto* ini = RE::INISettingCollection::GetSingleton();
			if (auto* setting = ini ? ini->GetSetting("bPreloadLinkedInteriors:General") : nullptr) {
				args.Bool("linkedInteriorsWas", setting->data.b);
				setting->data.b = true;
			}
		}

		if (!settings.preloadInterior.empty()) {
			auto* cell = FindCell(settings.preloadInterior);
			if (cell && cell->IsInteriorCell()) {
				g_interior.cell = cell;
			} else {
				logs::error("preload: interior cell '{}' {}", settings.preloadInterior, cell ? "is not an interior" : "not found");
			}
			args.Str("interiorName", settings.preloadInterior).Cell("interior", g_interior.cell);
		}

		g_active = g_interior.cell != nullptr;
		Trace::Write("preload.init", args.Bool("active", g_active), nullptr, true);
		logs::info("preload: active={} interior={}", g_active, Trace::FormString(g_interior.cell));
	}

	// The game's own preload path: PlayerCharacter::Update -> AE#40845 -> AE#13378 -> this, with true.
	// Unloaded: creates a CellLoaderTask (interior=1) for the IO thread. Already in TES::interiorBuffer:
	// moves the cell to the front of the buffer (most recently used), nothing is loaded.
	void QueueInteriorCellLoad(RE::TESObjectCELL* a_cell)
	{
		using func_t = void(RE::ExteriorCellLoader*, RE::TESObjectCELL*, bool);
		static REL::Relocation<func_t> func{ REL::ID(18540) };
		if (auto* loader = RE::ExteriorCellLoader::GetSingleton()) {
			func(loader, a_cell, true);
		}
	}

	void KeepInterior()
	{
		const auto& settings = Settings::Get();
		// AE#18540 is an AE ID; the SE ID was not looked up.
		if (!settings.keepInterior || !g_interior.cell || !REL::Module::IsAE()) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || player->GetParentCell() == g_interior.cell) {
			return;
		}
		const auto state = Trace::CellState(g_interior.cell);
		if (state != kUnloaded && state != kUnattached) {
			return;  // the game is loading or unloading it right now
		}
		const auto door = Detectors::NearestDoorTo(g_interior.cell);
		if (settings.keepInteriorNearDoorOnly && !door) {
			return;
		}

		const auto now = Trace::NowUs();
		if (now < g_keepPausedUntilUs) {
			return;
		}
		if (state == kUnattached) {
			// Loaded: nothing to do. It may be out of TES::interiorBuffer (pushed out while its unload
			// was blocked), and QueueInteriorCellLoad would then start a second load of a loaded cell.
			return;
		}
		if (state == kUnloaded) {
			// 0.8.0: the game unloaded it again on the next frame, and this loop loaded it every 2 s.
			// Three loads in 30 s mean something still unloads it: stop for a minute and say so.
			std::erase_if(g_keepLoadsUs, [&](std::int64_t a_us) { return now - a_us > 30'000'000; });
			if (g_keepLoadsUs.size() >= 3) {
				g_keepLoadsUs.clear();
				g_keepPausedUntilUs = now + 60'000'000;
				Trace::Write("preload.keep", Trace::Args{}.Str("action", "pause").Cell("cell", g_interior.cell), nullptr, true);
				return;
			}
			g_keepLoadsUs.push_back(now);
		}

		QueueInteriorCellLoad(g_interior.cell);
		++g_keepCalls;
		if (state == kUnloaded) {
			Trace::Write("preload.keep", Trace::Args{}
											 .Str("action", "queue")
											 .Cell("cell", g_interior.cell)
											 .Num("doorDistance", door ? door->distance : -1.0f)
											 .Int("calls", static_cast<std::int64_t>(g_keepCalls))
											 .Int("blockedUnloads", static_cast<std::int64_t>(g_blockedUnloads.load()))
											 .Int("blockedCancels", static_cast<std::int64_t>(g_blockedCancels.load())),
				nullptr, true);
		}
	}

	bool InInteriorBuffer(const RE::TESObjectCELL* a_cell)
	{
		const auto* tes = RE::TES::GetSingleton();
		auto*       ini = RE::INISettingCollection::GetSingleton();
		const auto* setting = ini ? ini->GetSetting("uInterior Cell Buffer:General") : nullptr;
		if (!tes || !tes->interiorBuffer || !setting) {
			return true;  // nothing to compare with: leave it alone
		}
		for (std::uint32_t i = 0; i < setting->data.u; ++i) {
			if (tes->interiorBuffer[i] == a_cell) {
				return true;
			}
		}
		return false;
	}

	// While it was kept, the buffer pushed it out and its unload was blocked (BlockCellUnload): it stays
	// loaded outside the buffer, and nothing would ever unload it. Back into the buffer once it is let go.
	void ReturnToBuffer()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!g_interior.cell || g_keepActive.load() || Trace::CellState(g_interior.cell) != kUnattached || !player ||
			player->GetParentCell() == g_interior.cell || InteriorSim::InTransition() || InInteriorBuffer(g_interior.cell)) {
			return;
		}
		Hooks::BufferInterior(g_interior.cell);
		Trace::Write("preload.keep", Trace::Args{}.Str("action", "returnToBuffer").Cell("cell", g_interior.cell), nullptr, true);
	}

	// Main thread, every tick: keep the interior while the player is outside it and near its door.
	void UpdateKeepActive()
	{
		const auto& settings = Settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		auto*       playerCell = player ? player->GetParentCell() : nullptr;
		bool        active = settings.keepInterior && g_interior.cell && REL::Module::IsAE() && playerCell && playerCell != g_interior.cell;
		if (active && settings.keepInteriorNearDoorOnly) {
			active = Detectors::NearestDoorTo(g_interior.cell).has_value();
		}
		if (active != g_keepActive.load()) {
			g_keepActive = active;
			Trace::Write("preload.keep", Trace::Args{}.Str("action", active ? "on" : "off").Cell("cell", g_interior.cell).Cell("playerCell", playerCell), nullptr, true);
		}
	}

	void NoteBlocked(std::atomic<std::uint64_t>& a_counter, std::string_view a_what)
	{
		const auto count = ++a_counter;
		if (count == 1 || count % 600 == 0) {  // the linked-interior preload asks every frame
			Trace::Write("preload.block", Trace::Args{}.Str("what", a_what).Cell("cell", g_interior.cell).Int("count", static_cast<std::int64_t>(count)));
		}
	}
}

namespace Preload
{
	void Tick()
	{
		if (!g_initialized) {
			Init();
		}
		if (!g_active) {
			return;
		}

		UpdateKeepActive();
		CheckStates("tick");

		const auto now = Trace::NowUs();
		if (now >= g_nextRefreshUs) {
			g_nextRefreshUs = now + static_cast<std::int64_t>(Settings::Get().preloadRefreshSeconds * 1'000'000.0f);
			KeepInterior();
			ReturnToBuffer();
		}
	}

	RE::TESObjectCELL* Interior()
	{
		return g_interior.cell;
	}

	// Any thread: g_keepActive is decided on the main thread every tick (UpdateKeepActive).
	bool ShouldKeep(RE::FormID a_cell)
	{
		return g_keepActive.load() && g_interior.cell && a_cell == g_interior.cell->GetFormID() && !InteriorSim::InTransition();
	}

	bool BlockUnload(RE::TESObjectCELL* a_cell)
	{
		if (!a_cell || !ShouldKeep(a_cell->GetFormID())) {
			return false;
		}
		NoteBlocked(g_blockedUnloads, "unload");
		return true;
	}

	bool BlockCellUnload(RE::TESObjectCELL* a_cell, const void* a_caller)
	{
		// Only a fully loaded cell: CellLoaderTask::Cancel also unloads through AE#13721, and a
		// half-loaded cell must not be left behind. Attached by InteriorSim (0.8.16) it is loaded too.
		const auto state = Trace::CellState(a_cell);
		if (!a_cell || !ShouldKeep(a_cell->GetFormID()) || (state != kUnattached && !(state == kAttached && InteriorSim::OwnsAttachedCell(a_cell)))) {
			return false;
		}
		const auto count = ++g_blockedCellUnloads;
		std::string caller = Trace::DescribeAddress(reinterpret_cast<std::uintptr_t>(a_caller), true);
		bool        first = false;
		{
			const std::scoped_lock lock{ g_callerLock };
			first = g_unloadCallers.insert(caller).second;
		}
		if (first || count % 600 == 0) {
			Trace::Write("preload.block", Trace::Args{}
											  .Str("what", "cellUnload")
											  .Str("caller", caller)
											  .Bool("firstFromCaller", first)
											  .Cell("cell", g_interior.cell)
											  .Int("count", static_cast<std::int64_t>(count)),
				nullptr, true);
		}
		return true;
	}

	bool BlockCancel(RE::FormID a_cell)
	{
		if (!ShouldKeep(a_cell)) {
			return false;
		}
		NoteBlocked(g_blockedCancels, "cancel");
		return true;
	}

	std::uint64_t BlockedUnloads()
	{
		return g_blockedUnloads;
	}

	void CheckStates(std::string_view a_where)
	{
		if (!g_active || !g_interior.cell || !Trace::IsMainThread()) {
			return;
		}
		const auto state = Trace::CellState(g_interior.cell);
		if (state == g_interior.lastState) {
			return;
		}
		Trace::Write("preload.change", Trace::Args{}
										   .Str("where", a_where)
										   .Raw("cells", "[" + Trace::Args{}
																   .Cell("cell", g_interior.cell)
																   .Str("from", g_interior.lastState == kStateUnknown ? "?"sv : Trace::CellStateName(g_interior.lastState))
																   .Str("to", Trace::CellStateName(state))
																   .Json() +
																   "]"),
			nullptr, true);
		if (state != kUnattached && !InteriorSim::OwnsAttachedCell(g_interior.cell)) {
			InteriorSim::Detach(a_where);  // its 3D may be rendered outside; the game owns it now
		}
		g_interior.lastState = state;
	}
}

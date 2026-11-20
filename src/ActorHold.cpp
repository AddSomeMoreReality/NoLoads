#include "ActorHold.h"

#include "HooksCommon.h"
#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr std::size_t    kMaxCells = 16;
	constexpr std::size_t    kMaxRemembered = 64;
	constexpr std::uint32_t  kWasHighMs = 10'000;   // MoveToHigh's snap is skipped this long after the actor was seen high in a held cell
	constexpr std::uint32_t  kMaxKeptRecords = 48;  // actor.keptHigh records per Loading Menu open or close
	constexpr std::uint32_t  kHigh = 0;             // RE::PROCESS_TYPE::kHigh
	constexpr std::ptrdiff_t kMessageInterfaceActor = 0x98;  // IMovementMessageInterface at MovementControllerNPC+0x120; +0x98 = +0x1B8, the actor (AE#41763, AE#41717)

	struct Slot
	{
		std::atomic<RE::FormID> cell{ 0 };
		std::uint32_t           owners{ 0 };  // main thread
	};

	std::array<Slot, kMaxCells>                            g_cells;
	std::atomic<std::uint32_t>                             g_heldCount{ 0 };
	std::atomic<RE::FormID>                                g_watchedCell{ 0 };
	std::array<std::atomic<std::uint64_t>, kMaxRemembered> g_wasHigh{};  // FormID << 32 | milliseconds since load; written by the main thread

	std::atomic<std::int64_t>  g_openUs{ -1 };
	std::atomic<std::int64_t>  g_closeUs{ -1 };
	std::atomic<std::uint32_t> g_keptRecords{ 0 };

	// Set by Install before the hooks go in, read-only afterwards.
	bool g_holdEnabled{ false };
	bool g_traceLevels{ false };
	bool g_desiredHook{ false };
	bool g_snapHook{ false };
	bool g_levelHooks{ false };

	std::atomic<std::uint64_t> g_keptHigh{ 0 };
	std::atomic<std::uint64_t> g_snapsSkipped{ 0 };
	std::atomic<std::uint64_t> g_snapsDone{ 0 };
	std::atomic<std::uint64_t> g_levelChanges{ 0 };
	std::atomic<std::uint64_t> g_warpsQueued{ 0 };

	std::uint32_t NowMs()
	{
		return static_cast<std::uint32_t>(Trace::NowUs() / 1000);
	}

	std::int64_t MsSince(const std::atomic<std::int64_t>& a_stamp)
	{
		const auto stamp = a_stamp.load(std::memory_order_relaxed);
		return stamp < 0 ? -1 : (Trace::NowUs() - stamp) / 1000;
	}

	std::int32_t CurrentLevel(const RE::Actor* a_actor)
	{
		return static_cast<std::int32_t>(std::to_underlying(a_actor->GetProcessLevel()));
	}

	RE::FormID CellOf(const RE::Actor* a_actor)
	{
		const auto* cell = a_actor ? a_actor->GetParentCell() : nullptr;
		return cell ? cell->GetFormID() : 0;
	}

	bool IsZero(const RE::NiPoint3& a_point)
	{
		return a_point.x == 0.0f && a_point.y == 0.0f && a_point.z == 0.0f;
	}

	bool WasHighRecently(const RE::Actor* a_actor)
	{
		const auto id = a_actor->GetFormID();
		const auto now = NowMs();
		for (const auto& entry : g_wasHigh) {
			const auto value = entry.load(std::memory_order_relaxed);
			if (static_cast<RE::FormID>(value >> 32) == id) {
				return now - static_cast<std::uint32_t>(value) <= kWasHighMs;
			}
		}
		return false;
	}

	// The base's name, not TESObjectREFR::GetName: that one reads the extra data list under its lock, and these
	// records come from job and loader threads too. FormIDs, not editor IDs, for the same reason.
	const char* BaseName(const RE::Actor* a_actor)
	{
		const auto* base = a_actor->GetActorBase();
		const char* name = base ? base->GetFullName() : nullptr;
		return name ? name : "";
	}

	Trace::Args ActorArgs(RE::Actor* a_actor)
	{
		Trace::Args args;
		args.FormID("actor", a_actor->GetFormID())
			.Str("name", BaseName(a_actor))
			.FormID("cell", CellOf(a_actor))
			.Bool("held", ActorHold::HoldsActor(a_actor))
			.Int("msSinceOpen", ActorHold::MsSinceOpen())
			.Int("msSinceClose", ActorHold::MsSinceClose());
		return args;
	}

	// AE#37503: the process level the game wants a_actor at (0 high .. 3 low; AE#37501 then moves it with
	// Actor vfunc 0xF1-0xF4). An actor of a held cell that is high stays high.
	struct DesiredProcessLevel
	{
		static std::uint32_t thunk(RE::Actor* a_actor)
		{
			const auto level = func(a_actor);
			if (level == kHigh || !a_actor || g_heldCount.load(std::memory_order_relaxed) == 0 || CurrentLevel(a_actor) != static_cast<std::int32_t>(kHigh) ||
				a_actor->IsPlayerRef() || !ActorHold::HoldsActor(a_actor) || a_actor->IsDisabled() || a_actor->IsDead()) {
				return level;
			}
			g_keptHigh.fetch_add(1, std::memory_order_relaxed);
			if (g_traceLevels && g_keptRecords.fetch_add(1, std::memory_order_relaxed) < kMaxKeptRecords) {
				auto args = ActorArgs(a_actor);
				Trace::Write("actor.keptHigh", args.Int("wanted", static_cast<std::int32_t>(level)).Raw("stack", Trace::StackJson(0, 8)), _ReturnAddress());
			}
			return kHigh;
		}

		static inline decltype(&thunk) func;
	};

	// AE#37621 (Actor::MoveToHigh) + 0x348: call AE#37618, which puts an actor that walks a path (or has
	// ExtraBadPosition, a zero position, or comes from middle-low / low) on its package's place. Skipped
	// for actors that were high in a held cell lately: they are where the portal showed them.
	struct MoveToHighSnap
	{
		static bool thunk(RE::Actor* a_actor, bool a_force)
		{
			const bool inScope = a_actor && ActorHold::InScope(a_actor);
			const bool held = a_actor && ActorHold::HoldsActor(a_actor);
			const bool wasHigh = held && WasHighRecently(a_actor);
			const auto from = a_actor ? a_actor->GetPosition() : RE::NiPoint3{};
			const bool skip = g_holdEnabled && wasHigh && !IsZero(from);
			bool       result = false;
			if (skip) {
				g_snapsSkipped.fetch_add(1, std::memory_order_relaxed);
			} else {
				result = func(a_actor, a_force);
				if (inScope) {
					g_snapsDone.fetch_add(1, std::memory_order_relaxed);
				}
			}
			if (g_traceLevels && inScope) {
				const auto to = a_actor->GetPosition();
				auto       args = ActorArgs(a_actor);
				args.Bool("force", a_force)
					.Bool("wasHigh", wasHigh)
					.Bool("skipped", skip)
					.Bool("result", result)
					.Pos("from", from)
					.Pos("to", to)
					.Num("distance", from.GetDistance(to))
					.Raw("stack", Trace::StackJson(0, 8));
				Trace::Write("actor.snap", args, _ReturnAddress());
			}
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Character vfunc 0xF1 MoveToHigh, 0xF2 MovetoLow, 0xF3 MovetoMiddleLow, 0xF4 MoveToMiddleHigh.
	template <std::uint32_t Level>
	struct MoveToLevel
	{
		static bool thunk(RE::Actor* a_this)
		{
			if (!a_this || !ActorHold::InScope(a_this)) {
				return func(a_this);
			}
			const auto before = CurrentLevel(a_this);
			const auto stack = Trace::StackJson(0, 8);
			const bool result = func(a_this);
			g_levelChanges.fetch_add(1, std::memory_order_relaxed);
			auto args = ActorArgs(a_this);
			args.Int("from", before)
				.Int("to", Level)
				.Int("after", CurrentLevel(a_this))
				.Bool("result", result)
				.Bool("wasHigh", WasHighRecently(a_this))
				.Pos("pos", a_this->GetPosition())
				.Raw("stack", stack);
			Trace::Write("actor.level", args, _ReturnAddress());
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// IMovementMessageInterface vfunc 1 of MovementControllerNPC (AE#41761): queues a warp to a path point.
	// MovementAgentPathFollowerVirtual (AE#92556) calls it for every point its virtual position passed;
	// AE#41717 applies the queue (AE#37892 -> AE#37877 -> AE#37313 -> SetPosition). Job threads.
	struct QueueWarp
	{
		static void thunk(void* a_this, void* a_location, void* a_extra)
		{
			auto* actor = a_this ? *reinterpret_cast<RE::Actor* const*>(static_cast<std::byte*>(a_this) + kMessageInterfaceActor) : nullptr;
			if (actor && ActorHold::InScope(actor)) {
				g_warpsQueued.fetch_add(1, std::memory_order_relaxed);
				auto args = ActorArgs(actor);
				Trace::Write("actor.warpQueued", args.Int("level", CurrentLevel(actor)).Pos("pos", actor->GetPosition()), _ReturnAddress());
			}
			func(a_this, a_location, a_extra);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace ActorHold
{
	void Hold(std::uint32_t a_owner, RE::FormID a_cell, bool a_held, std::string_view a_why)
	{
		if (!a_cell || a_owner >= 32) {
			return;
		}
		const auto bit = 1u << a_owner;
		Slot*      slot = nullptr;
		Slot*      empty = nullptr;
		for (auto& candidate : g_cells) {
			const auto id = candidate.cell.load(std::memory_order_relaxed);
			if (id == a_cell) {
				slot = &candidate;
				break;
			}
			if (!id && !empty) {
				empty = &candidate;
			}
		}

		bool changed = false;
		if (a_held) {
			if (!slot) {
				if (!empty) {
					logs::warn("actor hold: no free slot for cell {:08X} ({} cells held)", a_cell, kMaxCells);
					return;
				}
				slot = empty;
				slot->owners = 0;
				slot->cell.store(a_cell, std::memory_order_release);
				g_heldCount.fetch_add(1, std::memory_order_relaxed);
			}
			changed = (slot->owners & bit) == 0;
			slot->owners |= bit;
		} else if (slot && (slot->owners & bit)) {
			changed = true;
			slot->owners &= ~bit;
			if (!slot->owners) {
				slot->cell.store(0, std::memory_order_release);
				g_heldCount.fetch_sub(1, std::memory_order_relaxed);
			}
		}
		if (changed) {
			Trace::Write("actor.hold", Trace::Args{}
										   .Str("action", a_held ? "hold" : "release")
										   .FormID("cell", a_cell)
										   .Int("owner", a_owner)
										   .Str("why", a_why)
										   .Bool("cellHeld", HoldsCell(a_cell))
										   .Int("cellsHeld", g_heldCount.load(std::memory_order_relaxed))
										   .Int("msSinceOpen", MsSinceOpen())
										   .Int("msSinceClose", MsSinceClose()),
				nullptr, true);
		}
	}

	void Clear(std::string_view a_why)
	{
		const auto held = g_heldCount.exchange(0);
		for (auto& slot : g_cells) {
			slot.cell.store(0, std::memory_order_release);
			slot.owners = 0;
		}
		for (auto& entry : g_wasHigh) {
			entry.store(0, std::memory_order_relaxed);
		}
		Trace::Write("actor.hold", Trace::Args{}.Str("action", "clear").Str("why", a_why).Int("cellsReleased", held), nullptr, true);
	}

	bool HoldsCell(RE::FormID a_cell)
	{
		if (!a_cell || g_heldCount.load(std::memory_order_relaxed) == 0) {
			return false;
		}
		for (const auto& slot : g_cells) {
			if (slot.cell.load(std::memory_order_acquire) == a_cell) {
				return true;
			}
		}
		return false;
	}

	bool HoldsActor(const RE::Actor* a_actor)
	{
		return HoldsCell(CellOf(a_actor));
	}

	bool InScope(const RE::Actor* a_actor)
	{
		const auto id = CellOf(a_actor);
		return id && (id == g_watchedCell.load(std::memory_order_relaxed) || HoldsCell(id));
	}

	void SetWatchedCell(RE::FormID a_cell)
	{
		g_watchedCell.store(a_cell, std::memory_order_relaxed);
	}

	void NoteHigh(const RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		// The actor's own entry, else a free one, else the oldest.
		const auto                  id = a_actor->GetFormID();
		const auto                  now = NowMs();
		std::atomic<std::uint64_t>* own = nullptr;
		std::atomic<std::uint64_t>* vacant = nullptr;
		std::atomic<std::uint64_t>* oldest = &g_wasHigh[0];
		std::uint32_t               oldestAge = 0;
		for (auto& entry : g_wasHigh) {
			const auto value = entry.load(std::memory_order_relaxed);
			if (!value) {
				vacant = vacant ? vacant : &entry;
				continue;
			}
			if (static_cast<RE::FormID>(value >> 32) == id) {
				own = &entry;
				break;
			}
			if (const auto age = now - static_cast<std::uint32_t>(value); age >= oldestAge) {
				oldestAge = age;
				oldest = &entry;
			}
		}
		auto* target = own ? own : vacant ? vacant : oldest;
		target->store((static_cast<std::uint64_t>(id) << 32) | now, std::memory_order_relaxed);
	}

	void NoteTransition(bool a_open)
	{
		(a_open ? g_openUs : g_closeUs).store(Trace::NowUs(), std::memory_order_relaxed);
		g_keptRecords.store(0, std::memory_order_relaxed);
	}

	std::int64_t MsSinceOpen()
	{
		return MsSince(g_openUs);
	}

	std::int64_t MsSinceClose()
	{
		return MsSince(g_closeUs);
	}

	void Install()
	{
		const auto& settings = Settings::Get();
		g_holdEnabled = settings.interiorSimHoldActors;
		g_traceLevels = settings.probeTraceActorLevels;
		if (!REL::Module::IsAE()) {
			logs::info("actor hold: not installed, AE only (AE#37503, AE#37621+0x348)");
			return;
		}

		if (g_holdEnabled) {
			Hooks::DetourTransaction transaction{ "actor hold: " };
			transaction.Attach<DesiredProcessLevel>("AE#37503", REL::Relocation<std::uintptr_t>{ REL::ID(37503) }.address());
			g_desiredHook = transaction.Commit();
		}

		g_snapHook = Hooks::WriteCall<MoveToHighSnap>("actor hold: MoveToHigh -> snap to package location", REL::ID(37621), 0x348, REL::ID(37618));

		if (g_traceLevels) {
			REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[0] };
			MoveToLevel<0>::func = character.write_vfunc(0xF1, MoveToLevel<0>::thunk);  // MoveToHigh
			MoveToLevel<3>::func = character.write_vfunc(0xF2, MoveToLevel<3>::thunk);  // MovetoLow
			MoveToLevel<2>::func = character.write_vfunc(0xF3, MoveToLevel<2>::thunk);  // MovetoMiddleLow
			MoveToLevel<1>::func = character.write_vfunc(0xF4, MoveToLevel<1>::thunk);  // MoveToMiddleHigh
			REL::Relocation<std::uintptr_t> messages{ RE::VTABLE_MovementControllerNPC[1] };  // IMovementMessageInterface at +0x120
			QueueWarp::func = messages.write_vfunc(0x1, QueueWarp::thunk);
			g_levelHooks = true;
		}
		logs::info("actor hold: hold {} (AE#37503 detour {}), snap call {}, level and warp traces {}", g_holdEnabled, g_desiredHook, g_snapHook, g_levelHooks);
	}

	std::string StatusJson()
	{
		std::string cells;
		for (const auto& slot : g_cells) {
			if (const auto id = slot.cell.load(std::memory_order_relaxed)) {
				cells += std::format("{}\"{:08X}\"", cells.empty() ? "" : ",", id);
			}
		}
		return std::format(R"({{"enabled":{},"detour":{},"snapCall":{},"cells":[{}],"keptHighTotal":{},"snapsSkippedTotal":{},"snapsDoneTotal":{},"levelChangesTotal":{},"warpsQueuedTotal":{}}})",
			g_holdEnabled, g_desiredHook, g_snapHook, cells,
			g_keptHigh.load(std::memory_order_relaxed), g_snapsSkipped.load(std::memory_order_relaxed), g_snapsDone.load(std::memory_order_relaxed),
			g_levelChanges.load(std::memory_order_relaxed), g_warpsQueued.load(std::memory_order_relaxed));
	}
}

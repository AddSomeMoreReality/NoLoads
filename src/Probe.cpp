#include "Probe.h"

#include "Physics.h"
#include "Portal.h"
#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr std::uint32_t kUnattached = 4;  // TESObjectCELL::CellState
	constexpr std::uint32_t kAttached = 7;

	constexpr std::int64_t kSceneCheckUs = 1'000'000;
	constexpr std::int64_t kSoundFreshUs = 6'000'000;

	// One slot per sound by its address; a clash loses the older one. Written on the audio thread, read for
	// the probe: a torn value only spoils one row of a diagnostic.
	struct HeardSound
	{
		const RE::BSGameSound* sound{ nullptr };
		RE::NiPoint3           position;
		float                  listenerDistance{ 0.0f };
		float                  volume{ 0.0f };
		std::uint32_t          file{ 0 };
		std::uint32_t          directory{ 0 };
		std::uint32_t          durationMs{ 0 };
		std::int64_t           seenUs{ 0 };
		bool                   carried{ false };
	};
	std::array<HeardSound, 64> g_heardSounds{};

	std::string SoundsJson()
	{
		const auto  now = Trace::NowUs();
		std::string json;
		for (const auto& heard : g_heardSounds) {
			if (heard.sound && now - heard.seenUs < kSoundFreshUs) {
				json += (json.empty() ? "" : ",") + Trace::Args{}
														.Pos("pos", heard.position)
														.Num("listenerDistance", heard.listenerDistance)
														.Num("volume", heard.volume)
														.Int("durationMs", heard.durationMs)
														.Hex("file", heard.file)
														.Hex("dir", heard.directory)
														.Bool("carried", heard.carried)
														.Json();
			}
		}
		return "[" + json + "]";
	}

	std::int64_t g_nextProbeUs{ 0 };
	std::mutex   g_requestLock;
	std::string  g_requested;  // reason of a requested probe, empty if none; guarded by g_requestLock
	std::int64_t         g_nextSceneCheckUs{ 0 };
	std::set<RE::FormID> g_playingScenes;

	// Scenes that started or stopped: a bard's song is one, and 0.8.22 saw it break off. NG does not
	// define the scene events, so the scenes' own flag is polled.
	void TrackScenes(std::int64_t a_now)
	{
		auto* data = RE::TESDataHandler::GetSingleton();
		if (a_now < g_nextSceneCheckUs || !data) {
			return;
		}
		g_nextSceneCheckUs = a_now + kSceneCheckUs;
		auto* player = RE::PlayerCharacter::GetSingleton();
		for (const auto* scene : data->GetFormArray<RE::BGSScene>()) {
			if (!scene || scene->isPlaying == g_playingScenes.contains(scene->GetFormID())) {
				continue;
			}
			if (scene->isPlaying) {
				g_playingScenes.insert(scene->GetFormID());
			} else {
				g_playingScenes.erase(scene->GetFormID());
			}
			Trace::Write("scene.change", Trace::Args{}
											 .FormID("scene", scene->GetFormID())
											 .Bool("playing", scene->isPlaying)
											 .Form("quest", scene->parentQuest)
											 .Cell("playerCell", player ? player->GetParentCell() : nullptr));
		}
	}

	std::string CellJson(RE::TESObjectCELL* a_cell, std::string_view a_category)
	{
		Trace::Args args;
		args.Str("cat", a_category).Cell("cell", a_cell);
		const auto state = Trace::CellState(a_cell);
		args.Str("state", Trace::CellStateName(state));

		const auto* loaded = a_cell ? a_cell->GetRuntimeData().loadedData : nullptr;
		const auto* root = loaded ? loaded->cell3D.get() : nullptr;
		// Rendering needs the cell's 3D attached to the world scene graph and not culled.
		args.Bool("has3D", root != nullptr)
			.Bool("inScene", root && root->parent != nullptr)
			.Bool("culled", root && root->GetAppCulled());

		// Physics: the cell's Havok world; whether it is stepped comes from the probe's "worlds" list
		// (hkpWorld::stepDeltaTime counts). Asked only for cells with 3D (attached or unattached).
		if (state == kAttached || state == kUnattached) {
			if (auto* world = a_cell->GetbhkWorld()) {
				// bhkWorld::vfunc[0x32] (AE#77851) does not step a world with toggleCollision false;
				// traced to see whether unattached interiors are switched off.
				args.Hex("bhkWorld", reinterpret_cast<std::uintptr_t>(world))
					.Hex("hkpWorld", reinterpret_cast<std::uintptr_t>(world->GetWorld1()))
					.Int("worldActive", world->toggleCollision ? 1 : 0);
			}
		}
		return args.Json();
	}

	// Interiors reachable through load doors placed in the given (loaded) cell.
	void CollectLinkedInteriors(RE::TESObjectCELL* a_cell, std::set<RE::TESObjectCELL*>& a_out)
	{
		a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			const auto* teleport = a_ref ? a_ref->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
			if (teleport && teleport->teleportData) {
				const auto linked = teleport->teleportData->linkedDoor.get();
				auto*      target = linked ? linked->GetParentCell() : nullptr;
				if (target && target->IsInteriorCell() && target != a_cell) {
					a_out.insert(target);
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	// Actors at high/middle-high level, and at any level when they stand in one of the probed cells.
	void AddActors(std::string& a_json, const RE::BSTArray<RE::ActorHandle>& a_handles, std::string_view a_level, bool a_allLevels,
		const std::set<RE::TESObjectCELL*>& a_cells, const RE::NiPoint3& a_playerPos, bool a_playerOutside, std::size_t& a_count, std::size_t a_max)
	{
		for (const auto& handle : a_handles) {
			if (a_count >= a_max) {
				return;
			}
			const auto actorPtr = handle.get();
			auto*      actor = actorPtr.get();
			if (!actor || actor->IsPlayerRef()) {
				continue;
			}
			auto* cell = actor->GetParentCell();
			if (!a_allLevels && !a_cells.contains(cell)) {
				continue;
			}
			const auto* process = actor->GetActorRuntimeData().currentProcess;
			const auto* package = actor->GetCurrentPackage();
			Trace::Args args;
			args.Form("actor", actor)
				.Str("name", actor->GetName())
				.Str("level", a_level)
				.Int("processLevel", process ? static_cast<std::int64_t>(std::to_underlying(process->processLevel.get())) : -1)
				.Cell("cell", cell)
				.Pos("pos", actor->GetPosition())
				.Bool("3D", actor->Is3DLoaded())
				.Bool("ai", actor->IsAIEnabled())
				.Bool("dead", actor->IsDead())
				.FormID("package", package ? package->GetFormID() : 0);
			if (a_playerOutside && cell && cell->IsExteriorCell()) {
				args.Num("dist", actor->GetPosition().GetDistance(a_playerPos));
			}
			// 0.8.19: the controller of the portal's NPCs, to compare with the same NPCs while the player is inside
			if (const auto* controller = actor->GetCharController()) {
				args.Str("controllerFlags", Portal::ControllerFlagsText(*controller)).Num("lodDistance", controller->lodDistance);
			}
			a_json += (a_count ? "," : "") + args.Json();
			++a_count;
		}
	}

	void TakeProbe(std::string_view a_reason)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* tes = RE::TES::GetSingleton();
		if (!player || !tes) {
			return;
		}
		const auto& settings = Settings::Get();

		auto*      playerCell = player->GetParentCell();
		const auto outside = playerCell && playerCell->IsExteriorCell();
		const auto playerPos = player->GetPosition();

		Trace::Args args;
		args.Str("reason", a_reason)
			.Cell("playerCell", playerCell)
			.Form("world", tes->GetRuntimeData2().worldSpace)
			.Pos("playerPos", playerPos);
		if (const auto* calendar = RE::Calendar::GetSingleton()) {
			args.Num("gameHours", calendar->GetHoursPassed());
		}

		// cells
		std::string cells;
		std::set<RE::TESObjectCELL*> seen;
		std::set<RE::TESObjectCELL*> linked;
		const auto add = [&](RE::TESObjectCELL* a_cell, std::string_view a_category) {
			if (a_cell && seen.insert(a_cell).second) {
				cells += (cells.empty() ? "" : ",") + CellJson(a_cell, a_category);
			}
		};
		add(playerCell, "player");
		if (auto* grid = tes->gridCells) {
			for (std::uint32_t i = 0; i < grid->length; ++i) {
				for (std::uint32_t j = 0; j < grid->length; ++j) {
					auto* cell = grid->GetCell(i, j);
					add(cell, outside ? "grid" : "gridWhileInside");
					if (cell && outside && Trace::CellState(cell) == kAttached) {
						CollectLinkedInteriors(cell, linked);
					}
				}
			}
		}
		if (!outside && playerCell) {
			CollectLinkedInteriors(playerCell, linked);  // doors out of / deeper into the current interior
		}
		for (auto* cell : linked) {
			add(cell, "doorInterior");
		}
		auto*       ini = RE::INISettingCollection::GetSingleton();
		const auto* bufferSetting = ini ? ini->GetSetting("uInterior Cell Buffer:General") : nullptr;  // its length, H9
		if (tes->interiorBuffer && bufferSetting) {
			for (std::uint32_t i = 0; i < bufferSetting->data.u; ++i) {
				add(tes->interiorBuffer[i], "bufferedInterior");
			}
		}
		args.Raw("cells", "[" + cells + "]");

		// actors
		std::string actors;
		std::size_t count = 0;
		if (const auto* lists = RE::ProcessLists::GetSingleton()) {
			const auto max = static_cast<std::size_t>(settings.probeMaxActors);
			AddActors(actors, lists->highActorHandles, "high", true, seen, playerPos, outside, count, max);
			AddActors(actors, lists->middleHighActorHandles, "middleHigh", true, seen, playerPos, outside, count, max);
			AddActors(actors, lists->middleLowActorHandles, "middleLow", false, seen, playerPos, outside, count, max);
			AddActors(actors, lists->lowActorHandles, "low", false, seen, playerPos, outside, count, max);
			args.Int("highTotal", lists->highActorHandles.size())
				.Int("middleHighTotal", lists->middleHighActorHandles.size())
				.Int("middleLowTotal", lists->middleLowActorHandles.size())
				.Int("lowTotal", lists->lowActorHandles.size());
		}
		args.Raw("actors", "[" + actors + "]");

		// Havok worlds stepped since the previous probe: [hkpWorld, steps, simulated seconds]
		std::string worlds;
		for (const auto& world : Physics::TakeWorldSteps()) {
			worlds += std::format("{}[\"{:#x}\",{},{:.3f},{}]", worlds.empty() ? "" : ",", world.world, world.steps, world.simSeconds, world.mainThreadSteps);
		}
		args.Raw("worlds", "[" + worlds + "]");
		// 0.8.20: the flames of the interior the player stands in, to compare with the portal's status
		if (playerCell && playerCell->IsInteriorCell() && playerCell->GetRuntimeData().loadedData) {
			args.Raw("fires", Portal::FireSampleJson(playerCell->GetRuntimeData().loadedData->cell3D.get()))
				.Raw("masters", Portal::MasterParticlesJson(playerCell->GetRuntimeData().loadedData->cell3D.get()))
				.Raw("fog", Portal::FogJson(playerCell));  // T13: the interior's own fog, to compare with the portal's
		}

		args.Raw("sounds", SoundsJson());

		Trace::Write("probe", args, nullptr, true);
	}
}

namespace Probe
{
	void NoteSound(const RE::BSGameSound* a_sound, const RE::NiPoint3& a_position, bool a_carried)
	{
		auto& heard = g_heardSounds[(reinterpret_cast<std::uintptr_t>(a_sound) >> 4) % g_heardSounds.size()];
		heard.sound = a_sound;
		heard.position = a_position;
		heard.listenerDistance = a_sound->listenerDistance;
		heard.volume = a_sound->volume;
		heard.file = a_sound->resourceID.file;
		heard.directory = a_sound->resourceID.dir;
		heard.durationMs = a_sound->durationMilliseconds;
		heard.seenUs = Trace::NowUs();
		heard.carried = a_carried;
	}

	void Tick()
	{
		const auto& settings = Settings::Get();
		if (!settings.probeEnabled) {
			return;
		}
		std::string reason;
		{
			const std::scoped_lock lock{ g_requestLock };
			reason.swap(g_requested);
		}
		const auto now = Trace::NowUs();
		TrackScenes(now);
		if (reason.empty() && now < g_nextProbeUs) {
			return;
		}
		TakeProbe(reason.empty() ? "interval"sv : std::string_view{ reason });
		g_nextProbeUs = now + static_cast<std::int64_t>(settings.probeIntervalSeconds * 1'000'000.0f);
	}

	void Request(std::string_view a_reason)
	{
		const std::scoped_lock lock{ g_requestLock };
		g_requested = a_reason;
	}
}

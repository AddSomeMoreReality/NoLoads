#include "InteriorSim.h"

#include "Detectors.h"
#include "Hooks.h"
#include "Hud.h"
#include "MainThread.h"
#include "Physics.h"
#include "Portal.h"
#include "Preload.h"
#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr std::uint32_t  kUnattached = 4;  // TESObjectCELL::CellState
	constexpr std::uint32_t  kAttaching = 6;
	constexpr std::uint32_t  kAttached = 7;
	constexpr std::uint32_t  kStages = 2;
	constexpr std::uint32_t  kStagePortal = 1;  // the interior simulated and drawn by a second camera into a quad in its doorway (Portal)
	constexpr std::int64_t   kPromoteIntervalUs = 1'000'000;
	constexpr std::int64_t   kAttachRetryUs = 1'000'000;
	constexpr std::int64_t   kSettleUs = 1'500'000;     // after a loading screen or a load of the interior: the autosave, the first references
	constexpr std::int64_t   kActors3DWaitUs = 8'000'000;  // the longest wait for the interior's actors to get their 3D
	constexpr float          kUnitsPerMeter = 70.0f;

	// the door rectangle guessed from the door's bound sphere radius when it has no CPU vertex copy
	constexpr float kGuessDoorWidth = 0.9f;
	constexpr float kGuessDoorHeight = 1.6f;

	// culling counters
	constexpr float       kMaxWatchedRefRadius = 500.0f;  // interior references watched: small ones, near the door
	constexpr std::size_t kWatchedInteriorRefs = 3;
	constexpr std::size_t kWatchedInteriorActors = 2;

	// Stages 1-5 of 0.8.0-0.8.11 (physics only, NPC high, the cell's 3D at its door, lifted, a copy) are
	// gone from the cycle (user request, 25.10.2026): docs/INTERIOR_SIM_STAGES.md. Physics and NPC high
	// stay as part of the portal stage.
	constexpr std::array<std::string_view, kStages> kStageNames{
		"off",
		"physics + NPC high + portal camera in its door",
	};

	bool                       g_initialized{ false };
	std::atomic<std::uint32_t> g_stage{ 0 };

	// physics: the step detour runs on the physics thread
	std::recursive_mutex       g_worldLock;
	std::atomic<bool>          g_transition{ false };
	std::atomic<RE::bhkWorld*> g_playerWorld{ nullptr };  // set by the main thread every tick
	std::atomic<std::uint64_t> g_steps{ 0 };
	std::atomic<std::uint64_t> g_stepsBusy{ 0 };     // the main thread held g_worldLock: step skipped
	std::atomic<std::uint32_t> g_worldActive{ 0xFF };  // the byte before our step, 0xFF = not seen yet
	std::atomic<std::uint32_t> g_stepThread{ 0 };

	// AI
	std::int64_t  g_nextPromoteUs{ 0 };
	std::uint64_t g_promoted{ 0 };
	std::size_t   g_actors{ 0 };
	std::size_t   g_highActors{ 0 };

	// render
	RE::NiPointer<RE::NiNode>                  g_cell3D;  // the cell's own 3D the portal draws
	std::vector<RE::NiPointer<RE::NiAVObject>> g_hidden;  // shown objects we hid: the exterior and interior doors
	RE::NiPointer<RE::NiNode>                  g_host;    // the root node of the exterior cell the door hangs in
	std::atomic<const void*>                   g_hostRoot{ nullptr };  // the same, compared from the grid hooks
	std::int64_t                               g_nextAttachUs{ 0 };
	std::string                                g_renderFailure;
	std::int64_t                               g_loadedSinceUs{ 0 };  // the interior is loaded since then; 0: it is not
	std::atomic<std::int64_t>                  g_transitionEndUs{ 0 };

	// culling: OnVisible calls (from any thread) for the portal quad, the interior's nodes the portal
	// camera's culling should reach, and for a baseline the game draws itself. The culling process calls OnVisible for every object that
	// passed the frustum and occlusion tests (BSCullingProcess::Process, AE#76553).
	constexpr std::size_t                               kMaxWatched = 32;
	std::array<std::atomic<const void*>, kMaxWatched>   g_watched{};
	std::array<std::atomic<std::uint32_t>, kMaxWatched> g_visibleCalls{};
	std::atomic<std::uint32_t>                          g_watchedCount{ 0 };
	std::atomic<std::uint64_t>                          g_watchedMask{ 0 };
	std::vector<std::string>                            g_watchedNames;  // main thread
	std::vector<std::string>                            g_watchedTypes;  // their classes: only a few vtables are hooked
	std::int64_t                                        g_visibleSinceUs{ 0 };

	// attachment (0.8.16): the interior as a loaded cell next to the player's. The game's per-frame cell
	// updates run on job threads: they try-lock g_attachLock, which the main thread holds to attach and detach.
	std::mutex                       g_attachLock;
	std::atomic<RE::TESObjectCELL*>  g_attached{ nullptr };   // set once kAttached, cleared before the detach
	std::atomic<RE::TESObjectCELL*>  g_attaching{ nullptr };  // during our attach and detach calls
	thread_local bool                t_attaching{ false };    // AE#18976 runs for us on this thread
	RE::NiPointer<RE::NiAVObject>    g_attached3D;            // its 3D, under g_attachRoot while attached
	RE::NiPointer<RE::NiNode>        g_attachRoot;            // ours, in no scene: the world camera never culls what hangs there
	bool                             g_attachFailed{ false };  // the attach did not reach kAttached: not tried again
	std::atomic<const RE::bhkWorld*> g_interiorWorld{ nullptr };
	std::atomic<std::uint64_t>       g_gameInteriorSteps{ 0 };  // steps of the interior's world the game made itself
	std::uint64_t                    g_seenGameInteriorSteps{ 0 };  // physics thread
	std::atomic<std::uint64_t>       g_cellUpdates{ 0 };   // AE#18890 calls for it
	std::atomic<std::uint64_t>       g_graphUpdates{ 0 };  // AE#18900 calls for it
	std::atomic<std::uint64_t>       g_updatesBusy{ 0 };   // ... skipped: the main thread was attaching or detaching
	std::atomic<std::uint64_t>       g_portalGraphsKept{ 0 };
	std::uint64_t                    g_attaches{ 0 };

	// status
	std::int64_t  g_nextStatusUs{ 0 };
	std::uint64_t g_stepsAtStatus{ 0 };
	std::int64_t  g_lastTickUs{ 0 };
	std::int64_t  g_frameUsSum{ 0 };
	std::uint32_t g_frames{ 0 };

	RE::TESObjectCELL* PlayerCell()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player ? player->GetParentCell() : nullptr;
	}

	std::size_t ActorsWithout3D(RE::TESObjectCELL* a_cell)
	{
		std::size_t count = 0;
		a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			const auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			count += actor && !actor->IsDisabled() && !actor->Get3D() ? 1 : 0;
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return count;
	}

	// Why the stages cannot run right now, or empty when they can.
	std::string Blocker(RE::TESObjectCELL* a_cell, RE::TESObjectCELL* a_playerCell)
	{
		if (!Hooks::Ready()) {
			return "the portal's hooks are not installed";
		}
		if (!Portal::Fault().empty()) {
			return "portal fault: " + Portal::Fault();
		}
		if (!a_cell) {
			return "no [Preload] sInteriorCell";
		}
		if (!a_playerCell || a_playerCell == a_cell || !a_playerCell->IsExteriorCell()) {
			return "player is not outside";
		}
		const auto state = Trace::CellState(a_cell);
		if (state != kUnattached && !(state == kAttached && g_attached.load() == a_cell)) {
			return std::format("interior is {}", Trace::CellStateName(state));
		}
		// kUnattached comes right after the load is queued, long before the references have their 3D: a
		// portal started then collected no shapes for its lights, and MoveToHigh pulled actors that were
		// not loaded (0.8.22). The first seconds after an exit also hold the autosave, which detaches it.
		if (state == kUnattached && !Portal::Active()) {
			const auto now = Trace::NowUs();
			if (now < (std::max)(g_loadedSinceUs + kSettleUs, g_transitionEndUs.load() + kSettleUs)) {
				return "interior is settling";
			}
			if (now < g_loadedSinceUs + kActors3DWaitUs && ActorsWithout3D(a_cell) > 0) {
				return "interior's actors have no 3D yet";
			}
		}
		return {};
	}

	// ------------------------------------------------------------------
	// Attachment (AE IDs from the machine code of TES::SetCurrentCell, AE#13316, and AE#13317, 0.8.16)
	// ------------------------------------------------------------------
	std::size_t CountCharControllers(RE::TESObjectCELL* a_cell)
	{
		std::size_t count = 0;
		a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			count += actor && actor->GetCharController() ? 1 : 0;
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return count;
	}

	// Its 3D off g_attachRoot, before AE#19130 runs. AE#19130 calls TES::objRoot->DetachChild(cell3D),
	// and NiNode::DetachChild (AE#70292) takes the child's own index (+0x38) without checking its
	// parent: with the 3D under our node it detached objRoot's child of that index instead. In 0.8.17
	// that freed TES::tempNodeManager, and the game crashed on entering the tavern (AE#13292).
	void ReleaseAttached3D()
	{
		if (g_attached3D && g_attachRoot && g_attached3D->parent == g_attachRoot.get()) {
			g_attachRoot->DetachChild(g_attached3D.get());
		}
		g_attached3D.reset();
	}

	// As AE#13317 detaches the interior the player leaves: AE#19130 (kDetaching, its 3D out of
	// TES::objRoot, its Havok world inactive, kUnattached; it puts the cell at the front of
	// TES::interiorBuffer, AE#13435), then AE#19046 (its actors in the process lists).
	void DetachCell(std::string_view a_why)
	{
		using Detach = void (*)(RE::TESObjectCELL*, bool);
		using DetachActors = void (*)(RE::TESObjectCELL*, bool, bool);
		static REL::Relocation<Detach>       detach{ REL::ID(19130) };
		static REL::Relocation<DetachActors> detachActors{ REL::ID(19046) };
		auto*                                cell = g_attached.load();
		if (!cell) {
			return;
		}
		const auto t0 = std::chrono::steady_clock::now();
		const auto before = Trace::CellState(cell);
		{
			const std::scoped_lock lock{ g_worldLock, g_attachLock };
			g_attaching = cell;
			g_attached = nullptr;
			ReleaseAttached3D();
			if (before == kAttaching || before == kAttached) {
				detach(cell, true);
				detachActors(cell, true, false);
			}
			g_attaching = nullptr;
		}
		Trace::Write("interior.sim", Trace::Args{}
										 .Str("action", "cellDetach")
										 .Str("why", a_why)
										 .Cell("cell", cell)
										 .Str("stateBefore", Trace::CellStateName(before))
										 .Str("stateAfter", Trace::CellStateName(Trace::CellState(cell)))
										 .Num("ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()),
			nullptr, true);
	}

	// As TES::SetCurrentCell (AE#13316) attaches an interior the player enters, but without the player:
	// AE#18976 (kAttaching, the cell's 3D under a root node, its Havok world active), then AE#19063
	// (its references, kAttached), inside a batch of its Havok world (AE#77905 / AE#77906) as there.
	// The root is ours, not TES::objRoot: the world camera would draw the interior at its own
	// coordinates, somewhere in the exterior. 0.8.16 hung it under TES::objRoot, AppCulled, and the
	// copies of its lights collected no shape: ShadowSceneNode::AddLight (AE#106326) only queues a
	// light, and its shapes are collected later (AE#106342), past hidden nodes.
	bool AttachCell(RE::TESObjectCELL* a_cell)
	{
		using Attach3D = void (*)(RE::TESObjectCELL*, RE::NiNode*);
		using BeginBatch = void (*)(RE::bhkWorld*);
		using EndBatch = void (*)(RE::bhkWorld*, bool);
		using FinishAttach = void (*)(RE::TESObjectCELL*, bool);
		static REL::Relocation<Attach3D>     attach3D{ REL::ID(18976) };
		static REL::Relocation<BeginBatch>   beginBatch{ REL::ID(77905) };
		static REL::Relocation<EndBatch>     endBatch{ REL::ID(77906) };
		static REL::Relocation<FinishAttach> finishAttach{ REL::ID(19063) };

		auto*                         tes = RE::TES::GetSingleton();
		const auto*                   loaded = a_cell->GetRuntimeData().loadedData;
		RE::NiPointer<RE::NiAVObject> root{ loaded ? loaded->cell3D.get() : nullptr };
		if (!tes || tes->interiorCell || !root || root->parent || Trace::CellState(a_cell) != kUnattached) {
			return false;
		}
		if (!g_attachRoot) {
			g_attachRoot.reset(RE::NiNode::Create());
			g_attachRoot->name = "CellLoadResearch attached interior";
		}
		const auto t0 = std::chrono::steady_clock::now();
		const auto controllersBefore = CountCharControllers(a_cell);
		auto       state = kUnattached;
		{
			const std::scoped_lock lock{ g_worldLock, g_attachLock };
			g_attaching = a_cell;
			t_attaching = true;
			attach3D(a_cell, g_attachRoot.get());
			auto* world = a_cell->GetbhkWorld();
			if (world) {
				beginBatch(world);
			}
			finishAttach(a_cell, true);
			if (world) {
				endBatch(world, false);
			}
			t_attaching = false;
			g_attached3D = root;
			state = Trace::CellState(a_cell);
			if (state == kAttached) {
				g_attached = a_cell;
			}
			g_attaching = nullptr;
		}
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		++g_attaches;
		Trace::Write("interior.sim", Trace::Args{}
										 .Str("action", "cellAttach")
										 .Cell("cell", a_cell)
										 .Str("state", Trace::CellStateName(state))
										 .Num("ms", ms)
										 .Int("attaches", static_cast<std::int64_t>(g_attaches))
										 .Int("charControllersBefore", static_cast<std::int64_t>(controllersBefore))
										 .Int("charControllersAfter", static_cast<std::int64_t>(CountCharControllers(a_cell)))
										 .Bool("refsFullyLoaded", loaded && loaded->refsFullyLoaded)
										 .Int("queuedRefs", loaded ? loaded->queuedRefCount : -1)
										 .Bool("rootInOurNode", root->parent == g_attachRoot.get())
										 .Int("portalGraphsKept", static_cast<std::int64_t>(g_portalGraphsKept.load())),
			nullptr, true);
		if (state != kAttached) {
			g_attachFailed = true;
			g_attached = a_cell;  // DetachCell puts it back as far as the game's detach can
			DetachCell("attach failed");
			return false;
		}
		return true;
	}

	void DetachNow(std::string_view a_why);

	// The game changed the state of the interior we attached (a load, an unload): it is not ours any more.
	void CheckAttachment()
	{
		auto* cell = g_attached.load();
		if (!cell || Trace::CellState(cell) == kAttached) {
			return;
		}
		const auto state = Trace::CellState(cell);
		{
			const std::scoped_lock lock{ g_worldLock, g_attachLock };  // the per-frame updates may be walking it
			g_attached = nullptr;
			ReleaseAttached3D();
		}
		Trace::Write("interior.sim", Trace::Args{}.Str("action", "cellLost").Cell("cell", cell).Str("state", Trace::CellStateName(state)), nullptr, true);
		DetachNow("cell lost");  // the portal started on an attached cell; it starts again
	}

	// ------------------------------------------------------------------
	// AI
	// ------------------------------------------------------------------
	void PromoteActors(RE::TESObjectCELL* a_cell)
	{
		// Collected first: MoveToHigh moves actors between the process lists, and the cell's
		// reference lock is held inside ForEachReference.
		std::vector<RE::NiPointer<RE::Actor>> actors;
		a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			if (actor && !actor->IsPlayerRef() && !actor->IsDisabled() && !actor->IsDead()) {
				actors.emplace_back(actor);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});

		std::size_t promoted = 0;
		std::string levelsBefore;
		for (auto& actor : actors) {
			const auto level = actor->GetProcessLevel();
			levelsBefore += std::format("{}{}", levelsBefore.empty() ? "" : ",", static_cast<std::int32_t>(level));
			if (level != RE::PROCESS_TYPE::kHigh) {
				actor->MoveToHigh();
				++promoted;
			}
		}
		std::size_t high = 0;
		for (auto& actor : actors) {
			high += actor->GetProcessLevel() == RE::PROCESS_TYPE::kHigh ? 1 : 0;
		}
		g_actors = actors.size();
		g_highActors = high;
		g_promoted += promoted;
		if (promoted > 0) {
			Trace::Write("interior.sim", Trace::Args{}
											 .Str("action", "promote")
											 .Int("actors", static_cast<std::int64_t>(actors.size()))
											 .Int("promoted", static_cast<std::int64_t>(promoted))
											 .Int("highAfter", static_cast<std::int64_t>(high))
											 .Raw("levelsBefore", "[" + levelsBefore + "]"));
		}
	}

	// ------------------------------------------------------------------
	// Render
	// ------------------------------------------------------------------
	void Fail(std::string a_why)
	{
		if (a_why == g_renderFailure) {
			return;
		}
		g_renderFailure = a_why;
		Trace::Write("interior.sim", Trace::Args{}.Str("action", "renderFail").Str("why", a_why), nullptr, true);
	}

	// The door's transform in the interior's own coordinates: its 3D world transform if that was
	// computed, else the local one (cell nodes are at the origin), checked against the reference position.
	std::optional<RE::NiTransform> InsideDoorTransform(RE::TESObjectREFR* a_door, std::string& a_source)
	{
		const auto* node = a_door ? a_door->Get3D() : nullptr;
		if (!node) {
			return std::nullopt;
		}
		const auto pos = a_door->GetPosition();
		if (node->world.translate.GetDistance(pos) < 10.0f) {
			a_source = "world";
			return node->world;
		}
		if (node->local.translate.GetDistance(pos) < 10.0f) {
			a_source = "local";
			return node->local;
		}
		return std::nullopt;
	}

	void HideShown(RE::NiAVObject* a_object)
	{
		if (a_object && !a_object->GetAppCulled()) {
			a_object->SetAppCulled(true);
			g_hidden.emplace_back(a_object);
		}
	}

	// ------------------------------------------------------------------
	// Culling counters
	// ------------------------------------------------------------------
	std::uint64_t MaskBit(const void* a_object)
	{
		return 1ull << ((reinterpret_cast<std::uintptr_t>(a_object) >> 4) & 63);  // NiObjects are 16-byte aligned
	}

	void Watch(std::string a_name, const RE::NiAVObject* a_object)
	{
		const auto count = g_watchedCount.load();
		if (!a_object || count >= kMaxWatched) {
			return;
		}
		g_watched[count] = a_object;
		g_visibleCalls[count] = 0;
		g_watchedMask.fetch_or(MaskBit(a_object));
		const auto* rtti = a_object->GetRTTI();
		g_watchedNames.push_back(std::move(a_name));
		g_watchedTypes.emplace_back(rtti && rtti->name ? rtti->name : "?");
		g_watchedCount = count + 1;
	}

	// The node and its first shape of a hooked type (BSTriShape, BSDynamicTriShape, BSSubIndexTriShape).
	void WatchTree(const std::string& a_name, RE::NiAVObject* a_root)
	{
		if (!a_root) {
			return;
		}
		Watch(a_name, a_root);
		RE::BSGeometry* shape = nullptr;
		RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
			const auto type = a_geometry->GetType().get();
			if (type == RE::BSGeometry::Type::kTriShape || type == RE::BSGeometry::Type::kDynamicTriShape || type == RE::BSGeometry::Type::kSubIndexTriShape) {
				shape = a_geometry;
				return RE::BSVisit::BSVisitControl::kStop;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		if (shape && shape != a_root) {
			Watch(a_name + " shape", shape);
		}
	}

	void ClearWatch()
	{
		g_watchedCount = 0;
		g_watchedMask = 0;
		g_watchedNames.clear();
		g_watchedTypes.clear();
	}

	// Calls per second since the last report as JSON, then the counters start again.
	std::string TakeVisibility(std::int64_t a_now)
	{
		std::string json;
		const auto seconds = (std::max)(static_cast<double>(a_now - g_visibleSinceUs) / 1'000'000.0, 0.001);
		g_visibleSinceUs = a_now;
		const auto count = (std::min)(static_cast<std::size_t>(g_watchedCount.load()), g_watchedNames.size());
		for (std::size_t i = 0; i < count; ++i) {
			const auto perSecond = g_visibleCalls[i].exchange(0) / seconds;
			json += (json.empty() ? "" : ",") + Trace::Args{}.Str("what", g_watchedNames[i]).Str("type", g_watchedTypes[i]).Num("perSecond", perSecond).Json();
		}
		return "[" + json + "]";
	}

	// The node chain up to the scene root, "name (class) < parent (class) < ...".
	std::string Ancestry(const RE::NiAVObject* a_object)
	{
		std::string chain;
		for (auto* node = a_object; node; node = node->parent) {
			const auto* rtti = node->GetRTTI();
			chain += std::format("{}{} ({})", chain.empty() ? "" : " < ", node->name.c_str(), rtti && rtti->name ? rtti->name : "?");
		}
		return chain;
	}

	// The interior: its root, the first room node, and the three reference nodes nearest to the point
	// where its door went (scene coordinates, after the update).
	void WatchInterior(RE::NiAVObject* a_content, const RE::NiPoint3& a_doorPoint)
	{
		Watch("interior root", a_content);
		std::vector<std::pair<float, RE::NiAVObject*>> refs;
		bool                                           room = false;
		RE::BSVisit::TraverseScenegraphObjects(a_content, [&](RE::NiAVObject* a_object) {
			const auto*            rtti = a_object->GetRTTI();
			const std::string_view type = rtti && rtti->name ? rtti->name : "";
			if (!room && (type == "BSMultiBoundRoom" || type == "BSMultiBoundNode")) {
				room = true;
				Watch("interior room", a_object);
			}
			const auto radius = a_object->worldBound.radius;
			if (type == "BSFadeNode" && !a_object->GetAppCulled() && radius > 0.0f && radius < kMaxWatchedRefRadius) {
				refs.emplace_back(a_object->worldBound.center.GetDistance(a_doorPoint), a_object);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		std::ranges::sort(refs, {}, &std::pair<float, RE::NiAVObject*>::first);
		for (std::size_t i = 0; i < refs.size() && i < kWatchedInteriorRefs; ++i) {
			WatchTree(std::format("interior ref {}", i + 1), refs[i].second);
		}
	}

	// The node above the interior's actors and the first of them: does the portal camera's culling
	// reach them and their shapes (0.8.8-0.8.9: no NPC in the portal)?
	void WatchActors(RE::TESObjectCELL* a_cell)
	{
		std::size_t watched = 0;
		a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			auto* root = actor && !actor->IsDisabled() ? actor->Get3D() : nullptr;
			if (!root) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (watched == 0) {
				Watch("interior actors' node", root->parent);
			}
			WatchTree(std::format("interior actor {}", ++watched), root);
			return watched < kWatchedInteriorActors ? RE::BSContainer::ForEachResult::kContinue : RE::BSContainer::ForEachResult::kStop;
		});
	}

	// The portal quad hangs in the exterior door's node.
	bool Rendering()
	{
		return Portal::Active();
	}

	void DetachNow(std::string_view a_why)
	{
		if (Rendering()) {
			Portal::Stop();
			for (auto& object : g_hidden) {
				object->SetAppCulled(false);
			}
			Trace::Write("interior.sim", Trace::Args{}
											 .Str("action", "detach")
											 .Str("why", a_why)
											 .Str("mode", "portal")
											 .Int("restored", static_cast<std::int64_t>(g_hidden.size())),
				nullptr, true);
			ClearWatch();
			g_cell3D.reset();
			g_hidden.clear();
			g_host.reset();
			g_hostRoot = nullptr;
		}
		DetachCell(a_why);
	}

	// The root node of the exterior cell a_object hangs in: its ancestor right under ObjectLODRoot
	// (TES::objRoot). Exterior cells' roots are BSMultiBoundNodes there.
	RE::NiNode* CellRootOf(RE::NiAVObject* a_object)
	{
		auto*       tes = RE::TES::GetSingleton();
		const auto* objectRoot = tes ? tes->objRoot : nullptr;
		for (auto* node = a_object ? a_object->parent : nullptr; node; node = node->parent) {
			if (node->parent == objectRoot && objectRoot) {
				return node;
			}
		}
		return nullptr;
	}

	// The street in front of the exterior door: where the player arrives when leaving the interior
	// (the interior door's teleport marker), and the horizontal direction from the door to it.
	// Without a marker: the door mesh's local +Y.
	void Street(RE::TESObjectREFR* a_inside, const RE::NiAVObject* a_door, RE::NiPoint3& a_ground, RE::NiPoint3& a_outward)
	{
		const auto* teleport = a_inside ? a_inside->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
		if (const auto* data = teleport ? teleport->teleportData : nullptr) {
			a_ground = data->position;
			a_outward = a_ground - a_door->world.translate;
			a_outward.z = 0.0f;
			if (a_outward.Unitize() > 1.0f) {
				return;
			}
		}
		a_ground = a_door->world.translate;
		a_outward = { a_door->world.rotate.entry[0][1], a_door->world.rotate.entry[1][1], 0.0f };
		if (a_outward.Unitize() < 0.01f) {
			a_outward = { 0.0f, 1.0f, 0.0f };
		}
	}

	// Interior coordinates -> exterior: the interior door, turned around (180 deg about its own Z), lies
	// on the exterior door: result * insideDoor = outsideDoor * flip. Only world transforms are used,
	// so the game's Euler angle convention does not matter.
	RE::NiTransform InteriorToExterior(const RE::NiTransform& a_outsideDoor, RE::NiTransform a_insideDoor)
	{
		RE::NiTransform target = a_outsideDoor;
		RE::NiMatrix3   flip;
		flip.entry[0][0] = -1.0f;
		flip.entry[1][1] = -1.0f;
		target.rotate = target.rotate * flip;
		target.scale = 1.0f;
		a_insideDoor.scale = 1.0f;
		return target * a_insideDoor.Invert();
	}

	// The exterior door's rectangle across the doorway, from its vertices (the renderer's CPU copy):
	// extents along a_right and up around its bound center, on its street-side face. Without vertices,
	// a guess from its bound sphere.
	struct DoorRect
	{
		RE::NiPoint3 center;
		float        width{ 0.0f };
		float        height{ 0.0f };
		std::size_t  points{ 0 };
	};

	DoorRect DoorRectangle(RE::NiAVObject* a_door, const RE::NiPoint3& a_right, const RE::NiPoint3& a_outward)
	{
		const RE::NiPoint3        up{ 0.0f, 0.0f, 1.0f };
		const auto                center = a_door->worldBound.center;
		std::vector<RE::NiPoint3> points;
		RE::BSVisit::TraverseScenegraphGeometries(a_door, [&](RE::BSGeometry* a_geometry) {
			if (!a_geometry->GetAppCulled()) {
				(void)Detectors::WorldPoints(a_geometry, points);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		if (points.size() < 3) {
			const float radius = a_door->worldBound.radius;
			return { .center = center, .width = radius * kGuessDoorWidth, .height = radius * kGuessDoorHeight };
		}
		float minRight = FLT_MAX, maxRight = -FLT_MAX, minUp = FLT_MAX, maxUp = -FLT_MAX, maxOut = -FLT_MAX;
		for (const auto& point : points) {
			const auto offset = point - center;
			minRight = (std::min)(minRight, offset.Dot(a_right));
			maxRight = (std::max)(maxRight, offset.Dot(a_right));
			minUp = (std::min)(minUp, offset.Dot(up));
			maxUp = (std::max)(maxUp, offset.Dot(up));
			maxOut = (std::max)(maxOut, offset.Dot(a_outward));
		}
		return { .center = center + a_right * ((minRight + maxRight) * 0.5f) + up * ((minUp + maxUp) * 0.5f) + a_outward * maxOut,
			.width = maxRight - minRight,
			.height = maxUp - minUp,
			.points = points.size() };
	}

	// The portal stage: nothing of the interior goes into the exterior scene; Portal draws it through the door.
	bool AttachPortal(RE::TESObjectCELL* a_cell)
	{
		const auto* loaded = a_cell->GetRuntimeData().loadedData;
		auto*       root = loaded ? loaded->cell3D.get() : nullptr;
		if (!root) {
			Fail("the interior has no 3D");
			return false;
		}
		if (root->parent) {
			Fail("the interior's 3D is already in a scene");
			return false;
		}
		const auto door = Detectors::NearestDoorTo(a_cell);
		if (!door) {
			Fail("no door to it in the loaded cells");
			return false;
		}
		const auto outside = door->ref.get();
		const auto inside = door->linked.get();
		auto*      outsideNode = outside ? outside->Get3D() : nullptr;
		auto*      host = outsideNode ? CellRootOf(outsideNode) : nullptr;
		if (!outsideNode || !outsideNode->parent || !host) {
			Fail("the exterior door has no 3D in a cell's node");
			return false;
		}
		std::string source;
		const auto  insideTransform = InsideDoorTransform(inside.get(), source);
		if (!insideTransform) {
			Fail("the interior door has no usable 3D");
			return false;
		}

		RE::NiPoint3 ground, outward;
		Street(inside.get(), outsideNode, ground, outward);
		const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
		const auto         inward = outward * -1.0f;
		const auto         right = inward.Cross(up);  // the right of a player facing the door from the street
		const auto         rect = DoorRectangle(outsideNode, right, outward);
		const auto         toExterior = InteriorToExterior(outsideNode->world, *insideTransform);

		Portal::Door portal;
		portal.cell = a_cell;
		portal.scene.reset(root);
		portal.toExterior = toExterior;
		portal.center = rect.center;
		portal.right = right;
		portal.up = up;
		portal.inward = inward;
		portal.width = rect.width;
		portal.height = rect.height;
		portal.parent = outsideNode->parent;
		std::string why;
		if (!Portal::Prepare(why)) {  // before the attach: a failure repeats every second, and each attach runs the game's scripts
			Fail(why);
			return false;
		}
		if (Settings::Get().interiorSimAttachInterior && !g_attachFailed) {
			AttachCell(a_cell);
		}
		portal.attached = g_attached.load() == a_cell;
		if (!Portal::Start(portal, why)) {
			Fail(why);
			DetachCell("the portal did not start");
			return false;
		}
		HideShown(outsideNode);
		if (inside) {
			HideShown(inside->Get3D());  // it stands on the door plane, where the portal camera's near plane is
		}
		g_host.reset(host);
		g_hostRoot = host;
		g_cell3D.reset(root);
		g_renderFailure.clear();

		ClearWatch();
		g_visibleSinceUs = Trace::NowUs();
		Watch("door's node", outsideNode->parent);
		WatchTree("portal quad", Portal::Quad());
		WatchInterior(root, insideTransform->translate);  // reached only by the portal camera's culling
		WatchActors(a_cell);

		const auto* main = RE::Main::WorldRootCamera();
		const auto& mainFrustum = main ? main->GetRuntimeData2().viewFrustum : RE::NiFrustum{};
		Trace::Write("interior.sim", Trace::Args{}
										 .Str("action", "attach")
										 .Str("mode", "portal")
										 .Bool("cellAttached", portal.attached)
										 .Form("door", outside.get())
										 .Form("insideDoor", inside.get())
										 .Str("insideDoorSource", source)
										 .Pos("outsideDoorWorld", outsideNode->world.translate)
										 .Pos("rectCenter", rect.center)
										 .Num("rectWidth", rect.width)
										 .Num("rectHeight", rect.height)
										 .Int("rectPoints", static_cast<std::int64_t>(rect.points))
										 .Pos("right", right)
										 .Pos("inward", inward)
										 .Pos("toExteriorTranslate", toExterior.translate)
										 .Num("doorDistance", door->distance)
										 .Str("quadParent", Ancestry(Portal::Quad() ? Portal::Quad()->parent : nullptr))
										 .Raw("mainFrustum", std::format("[{:.4f},{:.4f},{:.4f},{:.4f},{:.2f},{:.0f}]", mainFrustum.fLeft, mainFrustum.fRight,
																 mainFrustum.fTop, mainFrustum.fBottom, mainFrustum.fNear, mainFrustum.fFar))
										 .Int("hidden", static_cast<std::int64_t>(g_hidden.size())),
			nullptr, true);
		return true;
	}

	void UpdateRender(RE::TESObjectCELL* a_cell, bool a_ready, std::int64_t a_now)
	{
		const bool  want = a_ready && g_stage.load() == kStagePortal;
		const auto* loaded = a_cell ? a_cell->GetRuntimeData().loadedData : nullptr;
		const auto* current = loaded ? loaded->cell3D.get() : nullptr;
		if (Rendering() && (!want || g_cell3D.get() != current)) {
			DetachNow(want ? "3D changed" : "stage or state");
		}
		if (Rendering() && !Detectors::NearestDoorTo(a_cell)) {
			DetachNow("its door left the loaded cells");
		}
		if (Rendering() && (!g_host || !g_host->parent)) {
			DetachNow("its exterior cell left the scene");  // the grid hooks should have caught it first
		}
		if (want && !Rendering() && a_now >= g_nextAttachUs) {
			g_nextAttachUs = a_now + kAttachRetryUs;
			AttachPortal(a_cell);
		}
	}

	// ------------------------------------------------------------------
	// Status
	// ------------------------------------------------------------------
	void Status(RE::TESObjectCELL* a_cell, const std::string& a_blocker)
	{
		const auto steps = g_steps.load();
		const auto newSteps = steps - g_stepsAtStatus;
		g_stepsAtStatus = steps;
		const auto frameMs = g_frames ? static_cast<double>(g_frameUsSum) / g_frames / 1000.0 : 0.0;
		g_frameUsSum = 0;
		g_frames = 0;

		const auto stage = g_stage.load();
		const auto visibility = TakeVisibility(Trace::NowUs());

		Trace::Write("interior.sim", Trace::Args{}
										 .Str("action", "status")
										 .Int("stage", stage)
										 .Cell("cell", a_cell)
										 .Str("state", Trace::CellStateName(Trace::CellState(a_cell)))
										 .Str("blocker", a_blocker)
										 .Int("steps", static_cast<std::int64_t>(newSteps))
										 .Int("stepsBusyTotal", static_cast<std::int64_t>(g_stepsBusy.load()))
										 .Int("stepThread", g_stepThread.load())
										 .Int("worldActiveBefore", g_worldActive.load())
										 .Int("actors", static_cast<std::int64_t>(g_actors))
										 .Int("highActors", static_cast<std::int64_t>(g_highActors))
										 .Int("promotedTotal", static_cast<std::int64_t>(g_promoted))
										 .Int("blockedUnloadsTotal", static_cast<std::int64_t>(Preload::BlockedUnloads()))
										 .Bool("attached", g_attached.load() != nullptr)
										 .Int("gameInteriorStepsTotal", static_cast<std::int64_t>(g_gameInteriorSteps.load()))
										 .Int("cellUpdates", static_cast<std::int64_t>(g_cellUpdates.exchange(0)))
										 .Int("graphUpdates", static_cast<std::int64_t>(g_graphUpdates.exchange(0)))
										 .Int("updatesBusyTotal", static_cast<std::int64_t>(g_updatesBusy.load()))
										 .Int("portalGraphsKeptTotal", static_cast<std::int64_t>(g_portalGraphsKept.load()))
										 .Int("renderMode", Portal::Active() ? 1 : 0)
										 .Num("frameMs", frameMs)
										 .Raw("visible", visibility)
										 .Raw("portal", Portal::StatusJson()));
	}
}

namespace InteriorSim
{
	void Tick()
	{
		const auto& settings = Settings::Get();
		if (!g_initialized) {
			g_initialized = true;
			g_stage = (std::min)(settings.interiorSimStage, kStages - 1);
		}
		const auto now = Trace::NowUs();
		if (g_lastTickUs) {
			g_frameUsSum += now - g_lastTickUs;
			++g_frames;
		}
		g_lastTickUs = now;

		auto*      cell = Preload::Interior();
		auto*      playerCell = PlayerCell();
		const auto stage = g_stage.load();
		const auto cellState = Trace::CellState(cell);
		if (cellState != kUnattached && cellState != kAttached) {
			g_loadedSinceUs = 0;
		} else if (g_loadedSinceUs == 0) {
			g_loadedSinceUs = now;
		}
		const auto blocker = stage > 0 ? Blocker(cell, playerCell) : std::string{ "off" };
		const bool ready = blocker.empty();
		g_playerWorld = ready ? playerCell->GetbhkWorld() : nullptr;
		g_interiorWorld = ready ? cell->GetbhkWorld() : nullptr;

		CheckAttachment();
		UpdateRender(cell, ready, now);
		Portal::Tick();
		if (ready && stage == kStagePortal && now >= g_nextPromoteUs) {
			g_nextPromoteUs = now + kPromoteIntervalUs;
			PromoteActors(cell);
		}
		if (stage > 0 && now >= g_nextStatusUs) {
			g_nextStatusUs = now + static_cast<std::int64_t>(settings.interiorSimStatusSeconds * 1'000'000.0f);
			Status(cell, blocker);
		}
	}

	void Cycle()
	{
		const auto next = (g_stage.load() + 1) % kStages;
		g_stage = next;
		g_nextStatusUs = Trace::NowUs() + 3'000'000;  // first status after the stage had time to act
		g_nextAttachUs = 0;
		g_renderFailure.clear();
		auto* cell = Preload::Interior();
		Trace::Write("interior.sim", Trace::Args{}.Str("action", "stage").Int("stage", next).Str("name", kStageNames[next]).Cell("cell", cell), nullptr, true);
		Hud::Show("sim.stage", std::format("CLR: interior sim {}/{}: {} ({})", next, kStages - 1, kStageNames[next], Hud::CellLabel(cell)));
	}

	void Detach(std::string_view a_why)
	{
		if (!Trace::IsMainThread()) {
			MainThread::Post([why = std::string{ a_why }]() { DetachNow(why); });
			return;
		}
		DetachNow(a_why);
	}

	void CellDetaching(const RE::TESObjectCELL* a_cell, std::string_view a_where)
	{
		const auto* loaded = a_cell ? a_cell->GetRuntimeData().loadedData : nullptr;
		const auto* root = loaded ? loaded->cell3D.get() : nullptr;
		if (root && root == g_hostRoot.load()) {
			Detach(a_where);
		}
	}

	void NoteVisible(const void* a_object)
	{
		if ((g_watchedMask.load(std::memory_order_relaxed) & MaskBit(a_object)) == 0) {
			return;
		}
		const auto count = g_watchedCount.load(std::memory_order_acquire);
		for (std::uint32_t i = 0; i < count; ++i) {
			if (g_watched[i].load(std::memory_order_relaxed) == a_object) {
				g_visibleCalls[i].fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
	}

	void SetTransition(bool a_active)
	{
		g_transition = a_active;
		if (a_active) {
			g_playerWorld = nullptr;
		} else {
			g_transitionEndUs = Trace::NowUs();
		}
	}

	bool InTransition()
	{
		return g_transition.load();
	}

	std::unique_lock<std::recursive_mutex> GuardWorlds()
	{
		return std::unique_lock{ g_worldLock };
	}

	void AfterWorldStep(RE::bhkWorld* a_world, std::int32_t a_flag, StepFn a_step)
	{
		if (g_stage.load() < 1 || g_transition.load() || Physics::InWarmStart() || a_world != g_playerWorld.load()) {
			return;
		}
		std::unique_lock lock{ g_worldLock, std::try_to_lock };
		if (!lock.owns_lock()) {
			++g_stepsBusy;  // the main thread is attaching, buffering or unloading interiors
			return;
		}
		auto*      cell = Preload::Interior();
		const auto state = Trace::CellState(cell);
		if (!cell || (state != kUnattached && !(state == kAttached && g_attached.load() == cell))) {
			return;
		}
		auto* world = cell->GetbhkWorld();
		if (!world || world == a_world) {
			return;
		}
		if (const auto gameSteps = g_gameInteriorSteps.load(); gameSteps != g_seenGameInteriorSteps) {
			g_seenGameInteriorSteps = gameSteps;  // the game steps the attached interior's world itself
			return;
		}
		auto&      active = world->toggleCollision;  // bhkWorld::vfunc[0x32] returns at once when it is false
		const bool wasActive = active;
		g_worldActive = wasActive ? 1 : 0;
		if (!wasActive) {
			if (!Settings::Get().interiorSimForceWorldActive) {
				return;
			}
			active = true;
		}
		a_step(world, a_flag);
		active = wasActive;
		++g_steps;
		g_stepThread = REX::W32::GetCurrentThreadId();
	}

	void NoteWorldStep(const RE::bhkWorld* a_world)
	{
		if (a_world && a_world == g_interiorWorld.load(std::memory_order_relaxed)) {
			g_gameInteriorSteps.fetch_add(1, std::memory_order_relaxed);
		}
	}

	bool OwnsAttachedCell(const RE::TESObjectCELL* a_cell)
	{
		return a_cell && (g_attached.load() == a_cell || g_attaching.load() == a_cell);
	}

	void AfterCellsUpdate()
	{
		using UpdateReferences = void (*)(RE::TESObjectCELL*, float);
		static REL::Relocation<UpdateReferences> update{ REL::ID(18890) };
		static REL::Relocation<float*>           time{ REL::ID(400333) };  // AE#13291 adds the frame's time to it, then passes it to the cells
		if (!g_attached.load(std::memory_order_relaxed)) {
			return;
		}
		std::unique_lock lock{ g_attachLock, std::try_to_lock };
		if (!lock.owns_lock()) {
			++g_updatesBusy;
			return;
		}
		auto*       cell = g_attached.load();
		const auto* tes = RE::TES::GetSingleton();
		if (!cell || !tes || tes->interiorCell || g_transition.load()) {
			return;
		}
		update(cell, *time);
		++g_cellUpdates;
	}

	void AfterAnimatedRefs(void* a_jobs)
	{
		// AE#18899 would queue jobs that run after AE#13325 returns, when the main thread may be detaching
		// the cell: always the direct AE#18900, under the lock.
		(void)a_jobs;
		using UpdateGraphs = void (*)(RE::TESObjectCELL*);
		static REL::Relocation<UpdateGraphs> update{ REL::ID(18900) };
		if (!g_attached.load(std::memory_order_relaxed)) {
			return;
		}
		std::unique_lock lock{ g_attachLock, std::try_to_lock };
		if (!lock.owns_lock()) {
			++g_updatesBusy;
			return;
		}
		auto*       cell = g_attached.load();
		const auto* tes = RE::TES::GetSingleton();
		if (!cell || !tes || tes->interiorCell || g_transition.load()) {
			return;
		}
		update(cell);
		++g_graphUpdates;
	}

	void BeforeCellDetach(const RE::TESObjectCELL* a_cell)
	{
		const auto* loaded = a_cell ? a_cell->GetRuntimeData().loadedData : nullptr;
		auto*       root = loaded ? loaded->cell3D.get() : nullptr;
		if (root && g_attachRoot && root->parent == g_attachRoot.get()) {
			// Only the game's own detach gets here: DetachCell, which holds this lock, took the 3D off first.
			const std::scoped_lock lock{ g_attachLock };
			g_attachRoot->DetachChild(root);
			Trace::Write("interior.sim", Trace::Args{}.Str("action", "cellDetachedByGame").Cell("cell", const_cast<RE::TESObjectCELL*>(a_cell)), nullptr, true);
		}
	}

	bool KeepPortalGraph()
	{
		if (!t_attaching) {
			return false;
		}
		g_portalGraphsKept.fetch_add(1, std::memory_order_relaxed);
		return true;
	}
}

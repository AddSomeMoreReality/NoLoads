#include "InteriorSim.h"

#include "Detectors.h"
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
	constexpr std::uint32_t  kStages = 7;
	constexpr std::uint32_t  kRenderAligned = 3;
	constexpr std::uint32_t  kRenderLifted = 4;
	constexpr std::uint32_t  kRenderCopyLifted = 5;  // clones of the interior's references instead of the cell's own nodes
	constexpr std::uint32_t  kRenderPortal = 6;      // a second camera draws the interior into a quad in the doorway (Portal)
	constexpr std::uintptr_t kWorldActiveOffset = 0xC5F9;  // bhkWorld::vfunc[0x32] returns at once when this byte is 0
	constexpr std::int64_t   kPromoteIntervalUs = 1'000'000;
	constexpr std::int64_t   kAttachRetryUs = 1'000'000;
	constexpr float          kUnitsPerMeter = 70.0f;

	// control doors on the street, units
	constexpr std::size_t kControls = 2;
	constexpr float       kControlsAhead = 250.0f;    // from the arrival marker, away from the door (0.8.4: 500 reached the houses across)
	constexpr float       kControlsSpacing = 300.0f;  // between neighbours, sideways
	constexpr float       kControlsAboveGround = 5.0f;
	constexpr float       kMaxControlLift = 150.0f;

	// the doorway behind the exterior door, units, around the door's bound center
	constexpr float       kNearbyRadius = 3000.0f;  // references by their position: buildings sit far from their doors
	constexpr float       kDoorwayHalfWidth = 150.0f;
	constexpr float       kDoorwayHalfHeight = 200.0f;
	constexpr float       kDoorwayDepth = 300.0f;
	constexpr float       kDoorPlaneSlack = 8.0f;           // a shape this close to the door plane is its frame
	constexpr float       kDoorProbeDepth = 40.0f;          // a point this far behind the door: what the doorway shows
	constexpr float       kDoorwayRayHeights[] = { -60.0f, 0.0f, 60.0f };  // rays into the doorway, around the door's center
	constexpr float       kDoorwayMaxHiddenRadius = 200.0f;  // larger shapes are walls
	constexpr std::size_t kMaxDoorwayTraced = 40;

	// the door rectangle guessed from the door's bound sphere radius when it has no CPU vertex copy
	constexpr float kGuessDoorWidth = 0.9f;
	constexpr float kGuessDoorHeight = 1.6f;

	// culling counters
	constexpr float       kMaxWatchedRefRadius = 500.0f;  // interior references watched: small ones, near the door
	constexpr std::size_t kWatchedInteriorRefs = 3;
	constexpr std::size_t kWatchedInteriorActors = 2;

	constexpr std::array<std::string_view, kStages> kStageNames{
		"off",
		"physics",
		"physics + NPC high",
		"physics + NPC high + render at its door",
		"physics + NPC high + render lifted",
		"physics + NPC high + render a copy lifted, registered for light",
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
	RE::NiPointer<RE::NiNode>                                    g_container;  // our node in the exterior scene
	RE::NiPointer<RE::NiNode>                                    g_cell3D;     // the cell's own 3D (also in copy mode, to see it change)
	std::vector<RE::NiPointer<RE::NiAVObject>>                   g_hidden;     // shown objects we hid: the exterior and interior doors
	std::vector<RE::NiPointer<RE::NiAVObject>>                   g_controls;   // control door copies outside the container
	std::vector<RE::NiPointer<RE::NiAVObject>>                   g_unculled;   // were app-culled before we showed them
	std::vector<std::pair<RE::NiPointer<RE::BSFadeNode>, float>> g_fades;      // fade nodes we set to opaque
	std::uint32_t                                                g_renderMode{ 0 };  // 0 or the render stage
	RE::NiPointer<RE::NiNode>                                    g_host;             // the exterior cell's root node holding the container
	std::atomic<const void*>                                     g_hostRoot{ nullptr };  // the same, compared from the grid hooks
	std::int64_t                                                 g_nextAttachUs{ 0 };
	std::string                                                  g_renderFailure;

	// Every clone stays alive for the session: freeing a clone of the interior (with the NPCs'
	// skeletons, BSFlattenedBoneTree) crashed 0.8.2 in its destructor on entering the tavern.
	std::vector<RE::NiPointer<RE::NiAVObject>> g_graveyard;

	// culling: OnVisible calls (from any thread) for the objects we hung into the scene, and for a
	// baseline the game draws itself. The culling process calls OnVisible for every object that
	// passed the frustum and occlusion tests (BSCullingProcess::Process, AE#76553).
	constexpr std::size_t                               kMaxWatched = 32;
	std::array<std::atomic<const void*>, kMaxWatched>   g_watched{};
	std::array<std::atomic<std::uint32_t>, kMaxWatched> g_visibleCalls{};
	std::atomic<std::uint32_t>                          g_watchedCount{ 0 };
	std::atomic<std::uint64_t>                          g_watchedMask{ 0 };
	std::vector<std::string>                            g_watchedNames;  // main thread
	std::vector<std::string>                            g_watchedTypes;  // their classes: only a few vtables are hooked
	std::int64_t                                        g_visibleSinceUs{ 0 };

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

	// Why the stages cannot run right now, or empty when they can.
	std::string Blocker(RE::TESObjectCELL* a_cell, RE::TESObjectCELL* a_playerCell)
	{
		if (!a_cell) {
			return "no [Preload] sInteriorCell";
		}
		if (!a_playerCell || a_playerCell == a_cell || !a_playerCell->IsExteriorCell()) {
			return "player is not outside";
		}
		const auto state = Trace::CellState(a_cell);
		if (state != kUnattached) {
			return std::format("interior is {}", Trace::CellStateName(state));
		}
		return {};
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
		Hud::Show("sim.render", "CLR: interior render: " + a_why);
	}

	struct TreeStats
	{
		std::int64_t nodes{ 0 };
		std::int64_t geometries{ 0 };
		std::int64_t culled{ 0 };
		std::int64_t fadeNodes{ 0 };
		std::int64_t fadeBelowOne{ 0 };
	};

	// Counts the subtree; with a_reveal also clears app-culling and makes fade nodes opaque,
	// remembering what it changed (a_remember) to undo it on detach.
	void Walk(RE::NiAVObject* a_object, TreeStats& a_stats, bool a_reveal, bool a_remember)
	{
		++a_stats.nodes;
		if (a_object->AsGeometry()) {
			++a_stats.geometries;
		}
		if (a_object->GetAppCulled()) {
			++a_stats.culled;
			if (a_reveal) {
				a_object->SetAppCulled(false);
				if (a_remember) {
					g_unculled.emplace_back(a_object);
				}
			}
		}
		if (auto* fade = a_object->AsFadeNode()) {
			++a_stats.fadeNodes;
			auto& value = fade->GetRuntimeData().currentFade;
			if (value < 0.99f) {
				++a_stats.fadeBelowOne;
				if (a_reveal) {
					if (a_remember) {
						g_fades.emplace_back(fade, value);
					}
					value = 1.0f;
				}
			}
		}
		if (auto* node = a_object->AsNode()) {
			for (auto& child : node->GetChildren()) {
				if (child) {
					Walk(child.get(), a_stats, a_reveal, a_remember);
				}
			}
		}
	}

	// Clones keep their Havok objects; they must never reach a physics world.
	void StripCollision(RE::NiAVObject* a_object)
	{
		a_object->collisionObject.reset();
		if (auto* node = a_object->AsNode()) {
			for (auto& child : node->GetChildren()) {
				if (child) {
					StripCollision(child.get());
				}
			}
		}
	}

	RE::NiAVObject* CloneOf(RE::NiAVObject* a_source)
	{
		auto* clone = netimmerse_cast<RE::NiAVObject*>(a_source->Clone());
		if (clone) {
			g_graveyard.emplace_back(clone);
			StripCollision(clone);
		}
		return clone;
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

	RE::NiUpdateData NoCollisionUpdate()
	{
		// kDisableCollision: do not push the new world transforms into the interior's Havok bodies.
		RE::NiUpdateData update{};
		update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
		return update;
	}

	// ShadowSceneNode::AttachObject (AE#106330) queues an object; AE#106335 later walks its geometry
	// and registers it with the lights, then empties the queue (no detach call exists). Lighting
	// shaders draw only with their light data, so unregistered geometry may never be drawn.
	void RegisterForLight(RE::NiAVObject* a_object)
	{
		if (auto* ssn = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]) {
			ssn->AttachObject(a_object);
		}
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

	// Calls per second since the last report, then the counters start again.
	struct Visibility
	{
		std::string json;
		std::string text;  // short, for the HUD
	};

	Visibility TakeVisibility(std::int64_t a_now)
	{
		Visibility result;
		const auto seconds = (std::max)(static_cast<double>(a_now - g_visibleSinceUs) / 1'000'000.0, 0.001);
		g_visibleSinceUs = a_now;
		const auto count = (std::min)(static_cast<std::size_t>(g_watchedCount.load()), g_watchedNames.size());
		for (std::size_t i = 0; i < count; ++i) {
			const auto perSecond = g_visibleCalls[i].exchange(0) / seconds;
			const auto& name = g_watchedNames[i];
			result.json += (result.json.empty() ? "" : ",") + Trace::Args{}.Str("what", name).Str("type", g_watchedTypes[i]).Num("perSecond", perSecond).Json();
			if (!name.ends_with(" shape")) {
				result.text += std::format("{}{} {:.0f}", result.text.empty() ? "" : ", ", name, perSecond);
			}
		}
		result.json = "[" + result.json + "]";
		return result;
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

	// References with 3D around a_point (by their position) in the scene cells and in the worldspace's
	// persistent cell, plus what Havok rays from the street into the doorway hit. 0.8.4 searched the
	// scene cells only and missed the Bannered Mare's building (0002CC60), hit by F6 37 units behind the door.
	std::vector<RE::NiPointer<RE::TESObjectREFR>> NearbyRefs(const RE::NiPoint3& a_point, const RE::TESObjectREFR* a_skip,
		const RE::NiPoint3& a_outward, std::string& a_rayHits)
	{
		auto  cells = Detectors::SceneCells();
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* playerCell = player ? player->GetParentCell() : nullptr;
		auto* world = playerCell ? playerCell->GetRuntimeData().worldSpace : nullptr;
		if (world && world->persistentCell && std::ranges::find(cells, world->persistentCell) == cells.end()) {
			cells.push_back(world->persistentCell);
		}
		std::vector<RE::NiPointer<RE::TESObjectREFR>> refs;
		const auto add = [&](RE::TESObjectREFR* a_ref) {
			if (a_ref && a_ref != a_skip && !a_ref->As<RE::Actor>() && !a_ref->IsDisabled() && a_ref->Get3D() &&
				std::ranges::none_of(refs, [&](const auto& a_have) { return a_have.get() == a_ref; })) {
				refs.emplace_back(a_ref);
			}
		};
		for (auto* cell : cells) {
			cell->ForEachReferenceInRange(a_point, kNearbyRadius, [&](RE::TESObjectREFR* a_ref) {
				add(a_ref);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		for (const float height : kDoorwayRayHeights) {
			const auto   origin = a_point + a_outward * kDoorwayHalfWidth + RE::NiPoint3{ 0.0f, 0.0f, height };
			RE::NiPoint3 hit;
			if (const auto ref = Detectors::PickRef(origin, a_outward * -1.0f, hit)) {
				a_rayHits += std::format("{}{:08X}@{:.0f}", a_rayHits.empty() ? "" : ",", ref->GetFormID(), (hit - a_point).Dot(a_outward));
				add(ref.get());
			}
		}
		return refs;
	}

	// The largest of a_refs whose 3D bound holds a_point: the building the door is in.
	RE::NiAVObject* BuildingAround(const RE::NiPoint3& a_point, const std::vector<RE::NiPointer<RE::TESObjectREFR>>& a_refs)
	{
		RE::NiAVObject* best = nullptr;
		for (const auto& ref : a_refs) {
			auto* root = ref->Get3D();
			if (root && !root->GetAppCulled() && root->worldBound.center.GetDistance(a_point) < root->worldBound.radius &&
				(!best || root->worldBound.radius > best->worldBound.radius)) {
				best = root;
			}
		}
		return best;
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

	// Something of the interior is in the exterior scene: its 3D in our container, or the portal quad.
	bool Rendering()
	{
		return g_container || Portal::Active();
	}

	void DetachNow(std::string_view a_why)
	{
		if (!Rendering()) {
			return;
		}
		Portal::Stop();
		for (auto& object : g_unculled) {
			object->SetAppCulled(true);
		}
		for (auto& [fade, value] : g_fades) {
			fade->GetRuntimeData().currentFade = value;
		}
		const auto restored = g_unculled.size() + g_fades.size();
		g_unculled.clear();
		g_fades.clear();
		if (g_container && g_cell3D && g_cell3D->parent == g_container.get()) {
			g_container->DetachChild(g_cell3D.get());
			auto update = NoCollisionUpdate();
			g_cell3D->Update(update);  // world transforms back in the interior's own coordinates
		}
		if (auto* parent = g_container ? g_container->parent : nullptr) {
			parent->DetachChild(g_container.get());
		}
		for (auto& control : g_controls) {
			if (auto* parent = control->parent) {
				parent->DetachChild(control.get());
			}
		}
		for (auto& object : g_hidden) {
			object->SetAppCulled(false);
		}
		Trace::Write("interior.sim", Trace::Args{}
										 .Str("action", "detach")
										 .Str("why", a_why)
										 .Int("mode", g_renderMode)
										 .Int("restored", static_cast<std::int64_t>(restored)),
			nullptr, true);
		ClearWatch();
		if (g_container) {
			g_graveyard.emplace_back(g_container.get());  // copies inside it are never freed
		}
		g_container.reset();
		g_cell3D.reset();
		g_controls.clear();
		g_hidden.clear();
		g_renderMode = 0;
		g_host.reset();
		g_hostRoot = nullptr;
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

	// "name (class)" of each child: where the container went among the cell's own nodes.
	std::string ChildrenOf(RE::NiNode* a_node)
	{
		std::string list;
		for (auto& child : a_node->GetChildren()) {
			const auto* rtti = child ? child->GetRTTI() : nullptr;
			list += std::format("{}{}", list.empty() ? "" : ", ",
				child ? std::format("{} ({})", child->name.c_str(), rtti && rtti->name ? rtti->name : "?") : std::string{ "-" });
		}
		return list;
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

	// Control copies of the exterior door on the street in front of it, numbered left to right for a
	// player with the back to the door:
	//   1 next to the real door, in the exterior cell's node (as the game hangs references);
	//   2 in our container (as the interior).
	// 0.8.4 had five: only the one in the exterior cell's node was drawn; under ObjectLODRoot, under
	// ShadowSceneNode and under WorldRoot the culling process never reached them (the one registered
	// with ShadowSceneNode::AttachObject left a dark door-shaped shadow, nothing else).
	std::string HangControls(RE::NiAVObject* a_door, const RE::NiPoint3& a_ground, const RE::NiPoint3& a_outward)
	{
		struct Place
		{
			std::string_view what;
			RE::NiNode*      parent;
			bool             light;
		};
		const std::array<Place, kControls> places{ {
			{ "next to the real door", a_door->parent, false },
			{ "in the container", g_container.get(), false },
		} };
		const RE::NiPoint3 right{ a_outward.y, -a_outward.x, 0.0f };  // the player's right, back to the door
		// as high above the street as the real door stands above it (its origin may not be at its foot)
		const float above = std::clamp(a_door->world.translate.z - a_ground.z, 0.0f, kMaxControlLift) + kControlsAboveGround;

		std::string json;
		for (std::size_t i = 0; i < places.size(); ++i) {
			const auto& place = places[i];
			const float slot = static_cast<float>(i) - static_cast<float>(places.size() - 1) / 2.0f;
			Trace::Args args;
			args.Int("number", static_cast<std::int64_t>(i + 1)).Str("what", place.what);
			auto* copy = place.parent ? CloneOf(a_door) : nullptr;
			if (copy) {
				RE::NiTransform world = a_door->world;
				world.translate = a_ground + a_outward * kControlsAhead + right * (slot * kControlsSpacing);
				world.translate.z = a_ground.z + above;
				copy->local = place.parent->world.Invert() * world;
				copy->SetAppCulled(false);
				place.parent->AttachChild(copy);
				auto update = NoCollisionUpdate();
				copy->Update(update);
				if (place.light) {
					RegisterForLight(copy);
				}
				if (place.parent != g_container.get()) {
					g_controls.emplace_back(copy);
				}
				WatchTree(std::format("door {}", i + 1), copy);
				args.Str("parent", Ancestry(place.parent)).Pos("world", copy->world.translate).Num("boundRadius", copy->worldBound.radius);
			} else {
				args.Str("parent", "none");
			}
			json += (json.empty() ? "" : ",") + args.Json();
		}
		return "[" + json + "]";
	}

	// What stands in the doorway once the exterior door is hidden (24.10.2026: the player saw
	// "another door" there; 0.8.4 F6 hit the building wrhousebanneredmare01.nif right behind the door,
	// whose shape WRHouseBanneredMare01:16 has the door texture wrdoor01.dds). Hidden: shapes with a
	// door texture whose bound holds the point just behind the door, and small shapes whose center
	// is behind the door plane near its axis. All shapes near the doorway go to the trace.
	std::string HideDoorway(const std::vector<RE::NiPointer<RE::TESObjectREFR>>& a_refs, const RE::NiAVObject* a_doorNode, const RE::NiPoint3& a_outward,
		std::size_t& a_hidden)
	{
		struct Near
		{
			RE::BSGeometry* shape;
			RE::FormID      ref;
			std::string     model;
			float           ahead, side, up, radius;
			bool            hide{ false };
		};
		const auto         center = a_doorNode->worldBound.center;
		const auto         probe = center - a_outward * kDoorProbeDepth;
		const RE::NiPoint3 right{ a_outward.y, -a_outward.x, 0.0f };

		std::vector<Near> nearby;
		for (const auto& ref : a_refs) {
			auto*       root = ref->Get3D();
			auto*       base = ref->GetBaseObject();
			const auto* model = base ? skyrim_cast<RE::TESModel*>(base) : nullptr;
			const char* modelPath = model ? model->GetModel() : nullptr;
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
				const auto  rel = a_geometry->worldBound.center - center;
				const float radius = a_geometry->worldBound.radius;
				const Near  shape{ .shape = a_geometry,
					 .ref = ref->GetFormID(),
					 .model = modelPath ? modelPath : "",
					 .ahead = rel.Dot(a_outward),  // < 0: behind the door, inside the building
					 .side = rel.Dot(right),
					 .up = rel.z,
					 .radius = radius };
				const bool holdsProbe = a_geometry->worldBound.center.GetDistance(probe) < radius;
				if (holdsProbe || (std::abs(shape.side) < kDoorwayHalfWidth + radius && std::abs(shape.up) < kDoorwayHalfHeight + radius &&
									  shape.ahead > -kDoorwayDepth - radius && shape.ahead < kDoorwayHalfWidth + radius && radius < kDoorwayMaxHiddenRadius)) {
					nearby.push_back(shape);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		std::ranges::sort(nearby, {}, &Near::radius);

		std::string json;
		for (std::size_t i = 0; i < nearby.size(); ++i) {
			auto& shape = nearby[i];
			const auto texture = Detectors::TextureFile(shape.shape);
			const bool doorTexture = texture.find("door") != std::string::npos && shape.shape->worldBound.center.GetDistance(probe) < shape.radius;
			const bool smallBehind = shape.radius < kDoorwayMaxHiddenRadius && shape.ahead < -kDoorPlaneSlack && shape.ahead > -kDoorwayDepth &&
			                         std::abs(shape.side) < kDoorwayHalfWidth && std::abs(shape.up) < kDoorwayHalfHeight;
			shape.hide = (doorTexture || smallBehind) && !shape.shape->GetAppCulled();
			if (shape.hide) {
				HideShown(shape.shape);
				++a_hidden;
			}
			if (i < kMaxDoorwayTraced) {
				json += (json.empty() ? "" : ",") + Trace::Args{}
														.Form("ref", RE::TESForm::LookupByID(shape.ref))
														.Str("model", shape.model)
														.Str("shape", shape.shape->name.c_str())
														.Str("texture", texture)
														.Num("ahead", shape.ahead)
														.Num("side", shape.side)
														.Num("up", shape.up)
														.Num("radius", shape.radius)
														.Bool("hidden", shape.hide)
														.Json();
			}
		}
		return "[" + json + "]";
	}

	// Copy mode: clones of the interior's references except actors (cloned skeletons crashed 0.8.2)
	// and the door we look through, under one node in the interior's coordinates.
	RE::NiNode* CopyInterior(RE::TESObjectCELL* a_cell, const RE::TESObjectREFR* a_skip, std::size_t& a_copied)
	{
		auto* copy = RE::NiNode::Create(0);
		g_graveyard.emplace_back(copy);
		copy->name = "CLR InteriorSim copy";
		std::vector<RE::NiPointer<RE::NiAVObject>> roots;
		a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (a_ref && a_ref != a_skip && !a_ref->As<RE::Actor>() && !a_ref->IsDisabled()) {
				if (auto* root = a_ref->Get3D()) {
					roots.emplace_back(root);
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		for (auto& root : roots) {
			if (auto* clone = CloneOf(root.get())) {
				clone->local = root->local;  // under the cell's per-type nodes, which sit at the origin
				copy->AttachChild(clone, true);
				++a_copied;
			}
		}
		return copy;
	}

	// A few geometries of the content: are they placed where expected and do they have GPU buffers?
	std::string GeometrySamples(RE::NiAVObject* a_root, std::size_t a_max)
	{
		std::string samples;
		std::size_t count = 0;
		RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
			if (count++ >= a_max) {
				return RE::BSVisit::BSVisitControl::kStop;
			}
			const auto& data = a_geometry->GetGeometryRuntimeData();
			const auto* renderer = data.rendererData;
			const auto* property = data.shaderProperty.get();
			const auto* rtti = property ? property->GetRTTI() : nullptr;
			samples += (samples.empty() ? "" : ",") + Trace::Args{}
														  .Str("name", a_geometry->name.c_str())
														  .Pos("world", a_geometry->world.translate)
														  .Pos("boundCenter", a_geometry->worldBound.center)
														  .Num("boundRadius", a_geometry->worldBound.radius)
														  .Bool("culled", a_geometry->GetAppCulled())
														  .Bool("rendererData", renderer != nullptr)
														  .Bool("vertexBuffer", renderer && renderer->vertexBuffer)
														  .Str("shader", rtti && rtti->name ? rtti->name : "none")
														  .Json();
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return "[" + samples + "]";
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

	// Stage 6: nothing of the interior goes into the exterior scene; Portal draws it through the door.
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
		if (!Portal::Start(portal, why)) {
			Fail(why);
			return false;
		}
		HideShown(outsideNode);
		if (inside) {
			HideShown(inside->Get3D());  // it stands on the door plane, where the portal camera's near plane is
		}
		g_host.reset(host);
		g_hostRoot = host;
		g_cell3D.reset(root);
		g_renderMode = kRenderPortal;
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
										 .Int("mode", kRenderPortal)
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
		Hud::Show("sim.render", std::format("CLR: {} portal in its door ({:.0f} m): door {:.1f} x {:.1f} m{}; F11 saves the portal picture",
									Hud::CellLabel(a_cell), door->distance / kUnitsPerMeter, rect.width / kUnitsPerMeter, rect.height / kUnitsPerMeter,
									rect.points ? "" : " (size guessed)"));
		return true;
	}

	bool Attach(RE::TESObjectCELL* a_cell, std::uint32_t a_mode)
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
		if (!outsideNode) {
			Fail("the exterior door has no 3D");
			return false;
		}
		std::string source;
		const auto  insideTransform = InsideDoorTransform(inside.get(), source);
		if (!insideTransform) {
			Fail("the interior door has no usable 3D");
			return false;
		}
		// Hung where the game hangs the exterior cell's own references: in 0.8.4 only the copy of the door
		// in that cell's node was drawn; the culling never reached what was added under ObjectLODRoot.
		auto* parent = CellRootOf(outsideNode);
		if (!parent) {
			Fail("the exterior door is not in a cell's node under ObjectLODRoot");
			return false;
		}

		auto       transform = InteriorToExterior(outsideNode->world, *insideTransform);
		const auto lift = a_mode == kRenderAligned ? 0.0f : Settings::Get().interiorSimLift;
		transform.translate.z += lift;

		const bool  copyMode = a_mode == kRenderCopyLifted;
		std::size_t copied = 0;
		RE::NiAVObject* content = copyMode ? CopyInterior(a_cell, inside.get(), copied) : root;

		g_container.reset(RE::NiNode::Create(2));
		g_container->name = "CLR InteriorSim";
		g_container->local = parent->world.Invert() * transform;
		g_container->SetAppCulled(false);
		g_container->AttachChild(content, true);
		const auto parentChildren = ChildrenOf(parent);
		parent->AttachChild(g_container.get());  // at the end: the empty slots of a cell's root belong to reference types
		g_host.reset(parent);
		g_hostRoot = parent;

		TreeStats revealed;
		Walk(content, revealed, true, !copyMode);
		auto update = NoCollisionUpdate();
		g_container->Update(update);
		if (copyMode) {
			RegisterForLight(g_container.get());
		}
		g_cell3D.reset(root);
		g_renderMode = a_mode;
		g_renderFailure.clear();

		// User requests (24.10.2026): see inside through the doorway - hide the exterior door, the
		// interior's own door that now stands in the doorway (stages 3 and 4; the copy skips it), and
		// what the building has behind its door.
		RE::NiPoint3 ground, outward;
		Street(inside.get(), outsideNode, ground, outward);
		std::string rayHits;
		const auto  nearby = NearbyRefs(outsideNode->worldBound.center, outside.get(), outward, rayHits);
		std::string doorway = "[]";
		std::size_t doorwayHidden = 0;
		if (Settings::Get().interiorSimHideDoor) {
			HideShown(outsideNode);
			if (!copyMode && inside) {
				HideShown(inside->Get3D());
			}
			doorway = HideDoorway(nearby, outsideNode, outward, doorwayHidden);
		}

		// Culling counters: what the game draws itself (the building, the door's node, the cell's root),
		// then ours.
		ClearWatch();
		g_visibleSinceUs = Trace::NowUs();
		WatchTree("building", BuildingAround(outsideNode->worldBound.center, nearby));
		Watch("door's node", outsideNode->parent);
		Watch("cell root", parent);
		Watch("container", g_container.get());
		WatchInterior(content, outsideNode->world.translate + RE::NiPoint3{ 0.0f, 0.0f, lift });
		const auto controls = HangControls(outsideNode, ground, outward);

		RE::NiPoint3 insideDoorNow{};
		if (!copyMode && inside) {
			if (const auto* node = inside->Get3D()) {
				insideDoorNow = node->world.translate;
			}
		}

		Trace::Write("interior.sim", Trace::Args{}
										 .Str("action", "attach")
										 .Int("mode", a_mode)
										 .Bool("copy", copyMode)
										 .Int("copiedReferences", static_cast<std::int64_t>(copied))
										 .Form("door", outside.get())
										 .Form("insideDoor", inside.get())
										 .Str("insideDoorSource", source)
										 .Pos("outsideDoorWorld", outsideNode->world.translate)
										 .Pos("insideDoorWorldNow", insideDoorNow)
										 .Num("lift", lift)
										 .Str("parent", Ancestry(parent))
										 .Str("parentChildren", parentChildren)
										 .Pos("parentWorld", parent->world.translate)
										 .Pos("translate", transform.translate)
										 .Num("doorDistance", door->distance)
										 .Int("nodes", revealed.nodes)
										 .Int("geometries", revealed.geometries)
										 .Int("unculled", static_cast<std::int64_t>(g_unculled.size()))
										 .Int("fadesRaised", static_cast<std::int64_t>(g_fades.size()))
										 .Int("hidden", static_cast<std::int64_t>(g_hidden.size()))
										 .Pos("contentBoundCenter", content->worldBound.center)
										 .Num("contentBoundRadius", content->worldBound.radius)
										 .Raw("controls", controls)
										 .Pos("street", ground)
										 .Pos("outward", outward)
										 .Int("nearbyRefs", static_cast<std::int64_t>(nearby.size()))
										 .Str("doorwayRayHits", rayHits)
										 .Int("doorwayHidden", static_cast<std::int64_t>(doorwayHidden))
										 .Raw("doorway", doorway)
										 .Str("containerAncestry", Ancestry(g_container.get()))
										 .Raw("samples", GeometrySamples(content, 6)),
			nullptr, true);
		Hud::Show("sim.render", std::format("CLR: {} {} {} ({:.0f} m): {} geometries, hidden: doors {}, doorway {}; "
											"control doors 1-2 on the street, left to right with your back to the door",
									Hud::CellLabel(a_cell), copyMode ? "copy" : "rendered", a_mode == kRenderAligned ? "at its door" : "above its door",
									door->distance / kUnitsPerMeter, revealed.geometries, g_hidden.size() - doorwayHidden, doorwayHidden));
		return true;
	}

	void UpdateRender(RE::TESObjectCELL* a_cell, bool a_ready, std::int64_t a_now)
	{
		const auto  stage = g_stage.load();
		const auto  want = a_ready && stage >= kRenderAligned ? stage : 0u;
		const auto* loaded = a_cell ? a_cell->GetRuntimeData().loadedData : nullptr;
		const auto* current = loaded ? loaded->cell3D.get() : nullptr;
		if (Rendering() && (want != g_renderMode || g_cell3D.get() != current)) {
			DetachNow(want == 0 ? "stage or state" : "mode or 3D changed");
		}
		if (Rendering() && !Detectors::NearestDoorTo(a_cell)) {
			DetachNow("its door left the loaded cells");
		}
		if (Rendering() && (!g_host || !g_host->parent)) {
			DetachNow("its exterior cell left the scene");  // the grid hooks should have caught it first
		}
		if (want != 0 && !Rendering() && a_now >= g_nextAttachUs) {
			g_nextAttachUs = a_now + kAttachRetryUs;
			if (want == kRenderPortal) {
				AttachPortal(a_cell);
			} else {
				Attach(a_cell, want);
			}
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
		auto       text = std::format("CLR: sim {} [{}]", Hud::CellLabel(a_cell), stage);
		if (!a_blocker.empty()) {
			text += ": waiting, " + a_blocker;
		} else {
			text += std::format(": physics +{} steps", newSteps);
			if (stage >= 2) {
				text += std::format(" | NPC high {}/{}", g_highActors, g_actors);
			}
			if (stage == kRenderPortal) {
				text += Portal::Active() ? " | " + Portal::StatusText() : " | no portal";
			} else if (stage >= kRenderAligned) {
				text += g_renderMode ? " | rendered" : " | not rendered";
			}
		}
		text += std::format(" | frame {:.1f} ms", frameMs);
		Hud::Show("sim", std::move(text));
		const auto visibility = TakeVisibility(Trace::NowUs());
		if (g_watchedCount.load() > 0) {
			Hud::Show("sim.cull", "CLR: drawn per second: " + visibility.text);
		}

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
										 .Int("renderMode", g_renderMode)
										 .Int("clonesKept", static_cast<std::int64_t>(g_graveyard.size()))
										 .Num("frameMs", frameMs)
										 .Raw("visible", visibility.json)
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
		const auto blocker = stage > 0 ? Blocker(cell, playerCell) : std::string{ "off" };
		const bool ready = blocker.empty();
		g_playerWorld = ready ? playerCell->GetbhkWorld() : nullptr;

		UpdateRender(cell, ready, now);
		if (ready && stage >= 2 && now >= g_nextPromoteUs) {
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
		Hud::Show("sim.stage", std::format("CLR: interior sim {}/{}: {} ({})", next, kStages - 1, kStageNames[next], Hud::CellLabel(cell)), true);
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
		auto* cell = Preload::Interior();
		if (!cell || Trace::CellState(cell) != kUnattached) {
			return;
		}
		auto* world = cell->GetbhkWorld();
		if (!world || world == a_world) {
			return;
		}
		auto&      active = *reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(world) + kWorldActiveOffset);
		const auto wasActive = active;
		g_worldActive = wasActive;
		if (!wasActive) {
			if (!Settings::Get().interiorSimForceWorldActive) {
				return;
			}
			active = 1;
		}
		a_step(world, a_flag);
		active = wasActive;
		++g_steps;
		g_stepThread = REX::W32::GetCurrentThreadId();
	}
}

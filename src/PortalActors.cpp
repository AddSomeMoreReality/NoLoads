#include "PortalState.h"

namespace Portal::detail
{
	constexpr std::int64_t  kActorRefreshUs = 1'000'000;
	constexpr int           kActorRevealDepth = 3;      // AppCulled nodes this deep under an actor's root are shown for the pass
	constexpr std::size_t   kTracedActors = 8;
	constexpr std::size_t   kTracedAncestors = 6;       // parents of an actor's root listed in the trace
	constexpr std::size_t   kMaxActorAncestors = 8;     // parents walked up from an actor's root to the interior's root
	constexpr float         kMaxAnimationStep = 0.1f;           // seconds: the longest step given to the actors' animation
	constexpr const char*   kPoseBone = "NPC Spine [Spn0]";     // a bone whose local transform tells whether the pose moved
	constexpr float         kHavokToSkyrim = 69.99125f;         // Havok metres -> game units

	std::vector<float>                          g_poseSignatures;    // per interior actor: the pose bone at the last pass
	std::vector<std::uint32_t>                  g_seenGameUpdates;   // per interior actor: g_gameAnimationUpdates at the last pass
	thread_local std::uint32_t t_actorScope{ 0 };

	// per interior actor, since the last status: the lowest and highest the actor stood (0.8.16: NPCs sank into the floor)
	std::vector<float> g_actorZMin;
	std::vector<float> g_actorZMax;

	// ------------------------------------------------------------------
	// Actors
	// ------------------------------------------------------------------
	void RefreshActors(std::int64_t a_now)
	{
		if (a_now < g_nextActorRefreshUs || !g_door.cell) {
			return;
		}
		g_nextActorRefreshUs = a_now + kActorRefreshUs;
		std::vector<RE::NiPointer<RE::NiAVObject>> roots;
		std::vector<RE::NiPointer<RE::Actor>>      actors;
		g_door.cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			// 0.8.18: an NPC who walked out into the street stayed in the cell's list (shown, synced and
			// lit by the portal every frame while she stood at the door).
			if (auto* root = actor && !actor->IsDisabled() && actor->GetParentCell() == g_door.cell ? actor->Get3D() : nullptr) {
				roots.emplace_back(root);
				actors.emplace_back(actor);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		g_actorRoots = std::move(roots);
		if (actors == g_interiorActors) {
			return;
		}
		// A new list: published for AdjustAnimationUpdate, the per-actor state of the last pass forgotten.
		g_interiorActors = std::move(actors);
		for (std::size_t i = 0; i < kMaxAnimatedActors; ++i) {
			g_animatedActors[i].store(i < g_interiorActors.size() ? g_interiorActors[i].get() : nullptr, std::memory_order_relaxed);
		}
		g_poseSignatures.assign(g_interiorActors.size(), 0.0f);
		g_seenGameUpdates.assign(g_interiorActors.size(), 0);
		g_actorZMin.assign(g_interiorActors.size(), FLT_MAX);
		g_actorZMax.assign(g_interiorActors.size(), -FLT_MAX);
		for (std::size_t i = 0; i < g_seenGameUpdates.size() && i < kMaxAnimatedActors; ++i) {
			g_seenGameUpdates[i] = g_gameAnimationUpdates[i].load(std::memory_order_relaxed);
		}
	}

	// Whether a_actor is one of the portal's interior actors (any thread; compared, never dereferenced).
	std::size_t PortalActorIndex(const RE::Actor* a_actor)
	{
		for (std::size_t i = 0; i < kMaxAnimatedActors; ++i) {
			const auto* actor = g_animatedActors[i].load(std::memory_order_relaxed);
			if (!actor || actor == a_actor) {
				return actor ? i : kMaxAnimatedActors;
			}
		}
		return kMaxAnimatedActors;
	}

	// The mark the game leaves on actors outside the main camera's frustum: no foot IK feedback, so their
	// height stops following the character controller, kFarAway on the controller (AE#37992), and an
	// animation update that moves no bone (AE#38054).
	void ClearNotVisible(RE::NiAVObject* a_root)
	{
		if (a_root && a_root->GetFlags().all(RE::NiAVObject::Flag::kNotVisible)) {
			a_root->GetFlags().reset(RE::NiAVObject::Flag::kNotVisible);
			g_notVisibleCleared.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void ForgetActors()
	{
		g_forceAnimation.store(false, std::memory_order_relaxed);
		g_actorsSeen.store(false, std::memory_order_relaxed);
		g_fullProcessing.store(false, std::memory_order_relaxed);
		g_gameAnimation.store(false, std::memory_order_relaxed);
		for (auto& actor : g_animatedActors) {
			actor.store(nullptr, std::memory_order_relaxed);
		}
		g_actorRoots.clear();
		g_interiorActors.clear();
		g_poseSignatures.clear();
		g_seenGameUpdates.clear();
		g_actorZMin.clear();
		g_actorZMax.clear();
	}

	// A sum over one bone's local transform: does the pose move?
	float PoseSignature(RE::NiAVObject* a_root)
	{
		static const RE::BSFixedString bone{ kPoseBone };
		const auto*                    node = a_root->GetObjectByName(bone);
		if (!node) {
			return 0.0f;
		}
		float sum = node->local.translate.x + node->local.translate.y + node->local.translate.z;
		for (const auto& row : node->local.rotate.entry) {
			sum += row[0] * 1.0f + row[1] * 2.0f + row[2] * 3.0f;
		}
		return sum;
	}

	// The game updates the animation of high-process actors with BOOL_BITS::kProcessMe itself (AE#41377,
	// Actor::UpdateAnimation, vfunc 0x7D), but before that Actor::ModifyAnimationUpdateData (AE#38054)
	// marks an actor whose 3D is kNotVisible as unseen, and BShkbAnimationGraph::Update (AE#63587) then
	// moves no bone of it (AE#63575): the interior's actors kept their bind pose (A-pose), or the pose
	// of the player's last visit. AdjustAnimationUpdate makes their updates full ones. Here, before each
	// pass: their 3D is put where the actors are, the animation of an actor the game skipped since the
	// last pass is advanced, and the world transforms are computed again (without pushing them into Havok).
	void AnimateActors()
	{
		const auto now = Trace::NowUs();
		const auto delta = g_lastAnimateUs ? std::clamp(static_cast<float>(now - g_lastAnimateUs) / 1'000'000.0f, 0.0f, kMaxAnimationStep) : 0.0f;
		g_lastAnimateUs = now;
		for (std::size_t i = 0; i < g_interiorActors.size() && i < g_actorZMin.size(); ++i) {
			const auto z = g_interiorActors[i] ? g_interiorActors[i]->GetPosition().z : 0.0f;
			g_actorZMin[i] = (std::min)(g_actorZMin[i], z);
			g_actorZMax[i] = (std::max)(g_actorZMax[i], z);
		}
		const auto& settings = Settings::Get();
		if (!settings.portalAnimateActors || delta <= 0.0f) {
			return;
		}
		for (std::size_t i = 0; i < g_interiorActors.size(); ++i) {
			auto& actor = g_interiorActors[i];
			auto* root = actor ? actor->Get3D() : nullptr;
			if (!root || !root->parent) {
				continue;
			}
			if (settings.portalSyncActorPositions && root->world.translate.GetDistance(actor->GetPosition()) > 1.0f) {
				actor->Update3DPosition(false);
				++g_positionsSynced;
			}
			const auto gameUpdates = i < kMaxAnimatedActors ? g_gameAnimationUpdates[i].load(std::memory_order_relaxed) : 0;
			if (gameUpdates != g_seenGameUpdates[i]) {
				g_seenGameUpdates[i] = gameUpdates;
				++g_animatedByGame;
			} else if (!g_door.attached) {  // an attached cell's actors stand when the game stands: a pause, a fade
				t_ownAnimation = true;
				actor->UpdateAnimation(delta);
				t_ownAnimation = false;
				++g_animated;
			}
			RE::NiUpdateData update{};
			update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
			root->Update(update);
			const auto pose = PoseSignature(root);
			g_poseChanged += pose != g_poseSignatures[i] ? 1 : 0;
			g_poseSignatures[i] = pose;
			++g_poseChecks;
		}
	}

	// What hides an actor of the interior from the portal: 0.8.8 found no AppCulled roots, 0.8.9 no
	// culled nodes near them (2 deep inside each) and fades raised for the culling. So the trace shows
	// the nodes above the root, up to the interior's root, and what the culling made of its first shape.
	std::string AncestorsJson(const RE::NiAVObject* a_root)
	{
		std::string json;
		std::size_t count = 0;
		for (const auto* parent = a_root->parent; parent && count < kTracedAncestors; parent = parent->parent, ++count) {
			const auto* rtti = parent->GetRTTI();
			json += (json.empty() ? "" : ",") + Trace::Args{}
													  .Str("name", parent->name.c_str())
													  .Str("type", rtti && rtti->name ? rtti->name : "?")
													  .Bool("culled", parent->GetAppCulled())
													  .Bool("interiorRoot", parent == g_door.scene.get())
													  .Num("boundRadius", parent->worldBound.radius)
													  .Json();
			if (parent == g_door.scene.get()) {
				break;
			}
		}
		return "[" + json + "]";
	}

	// The first shape under a_object that no hidden node hides (0.8.10 took the hidden weapon blood).
	RE::BSGeometry* FirstShownShape(RE::NiAVObject* a_object)
	{
		if (!a_object || a_object->GetAppCulled()) {
			return nullptr;
		}
		if (auto* geometry = a_object->AsGeometry()) {
			return geometry;
		}
		if (auto* node = a_object->AsNode()) {
			for (auto& child : node->GetChildren()) {
				if (auto* shape = FirstShownShape(child.get())) {
					return shape;
				}
			}
		}
		return nullptr;
	}

	// Nodes of the interior that the game's culling marks in a pass of its own (kPreProcessedNode), and
	// those of them it found not visible (kNotVisible): BSCullingProcess::Process (AE#76553) skips them
	// unless the context ignores that pass. 0.8.10: the culling reached the actors' node every pass but
	// none of the actors under it.
	std::string PreprocessedJson()
	{
		std::size_t preprocessed = 0;
		std::size_t notVisible = 0;
		RE::BSVisit::TraverseScenegraphObjects(g_door.scene.get(), [&](RE::NiAVObject* a_object) {
			const auto flags = a_object->GetFlags();
			if (flags.all(RE::NiAVObject::Flag::kPreProcessedNode)) {
				++preprocessed;
				notVisible += flags.all(RE::NiAVObject::Flag::kNotVisible) ? 1 : 0;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return Trace::Args{}.Int("preprocessed", static_cast<std::int64_t>(preprocessed)).Int("notVisible", static_cast<std::int64_t>(notVisible)).Json();
	}

	std::string ActorsJson()
	{
		std::string json;
		for (std::size_t i = 0; i < g_actorRoots.size() && i < kTracedActors; ++i) {
			auto&       root = g_actorRoots[i];
			RE::BSGeometry* shape = FirstShownShape(root.get());
			auto*           actor = i < g_interiorActors.size() ? g_interiorActors[i].get() : nullptr;
			const auto      bits = actor ? actor->GetActorRuntimeData().boolBits.underlying() : 0u;
			Trace::Args firstShape;
			if (shape) {
				ShapeState(firstShape, shape);
			}
			const auto* rtti = root->GetRTTI();
			std::size_t culled = 0;
			std::size_t nodes = 0;
			RE::BSVisit::TraverseScenegraphObjects(root.get(), [&](RE::NiAVObject* a_object) {
				++nodes;
				culled += a_object->GetAppCulled() ? 1 : 0;
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			auto*        fade = root->AsFadeNode();
			auto*        controller = actor ? actor->GetCharController() : nullptr;
			RE::NiPoint3 velocity{};
			if (actor) {
				actor->GetLinearVelocity(velocity);
			}
			RE::NiPoint3 controllerPos{};
			if (controller) {
				RE::hkVector4 position;
				controller->GetPosition(position, false);
				alignas(16) float xyzw[4]{};
				_mm_store_ps(xyzw, position.quad);
				controllerPos = { xyzw[0] * kHavokToSkyrim, xyzw[1] * kHavokToSkyrim, xyzw[2] * kHavokToSkyrim };
			}
			auto*      process = actor ? actor->GetActorRuntimeData().currentProcess : nullptr;
			const auto movement = i < kMaxAnimatedActors ? g_movementUpdates[i].exchange(0, std::memory_order_relaxed) : 0u;
			const auto zMin = i < g_actorZMin.size() ? g_actorZMin[i] : FLT_MAX;
			const auto zMax = i < g_actorZMax.size() ? g_actorZMax[i] : -FLT_MAX;
			if (i < g_actorZMin.size()) {
				g_actorZMin[i] = FLT_MAX;
				g_actorZMax[i] = -FLT_MAX;
			}
			const auto* interiorWorld = g_door.cell ? g_door.cell->GetbhkWorld() : nullptr;
			alignas(16) float supportNormal[4]{};
			if (controller) {
				_mm_store_ps(supportNormal, controller->supportNorm.quad);
			}
			json += (json.empty() ? "" : ",") + Trace::Args{}
													  .Str("name", root->name.c_str())
													  .Str("actor", actor ? actor->GetName() : "")
													  .Str("type", rtti && rtti->name ? rtti->name : "?")
													  .Bool("culled", root->GetAppCulled())
													  .Str("flags", std::format("0x{:08X}", root->GetFlags().underlying()))
													  .Str("boolBits", std::format("0x{:08X}", bits))
													  .Bool("processMe", (bits & std::to_underlying(RE::Actor::BOOL_BITS::kProcessMe)) != 0)
													  .Pos("actorPos", actor ? actor->GetPosition() : RE::NiPoint3{})
													  .Num("rootOffset", actor ? root->world.translate.GetDistance(actor->GetPosition()) : -1.0f)
													  .Num("fadeAmount", root->fadeAmount)
													  .Num("currentFade", fade ? fade->GetRuntimeData().currentFade : -1.0f)
													  .Int("nodes", static_cast<std::int64_t>(nodes))
													  .Int("culledNodes", static_cast<std::int64_t>(culled))
													  .Pos("boundCenter", root->worldBound.center)
													  .Num("boundRadius", root->worldBound.radius)
													  .Bool("hasParent", root->parent != nullptr)
													  .Int("movementUpdates", movement)
													  .Bool("charController", controller != nullptr)
													  .Pos("controllerPos", controllerPos)
													  .Pos("velocity", velocity)
													  .Str("package", process && process->currentPackage.package ? std::format("{:08X}", process->currentPackage.package->GetFormID()) : "")
													  .Int("processLevel", actor ? static_cast<std::int64_t>(std::to_underlying(actor->GetProcessLevel())) : -1)
													  .Num("lodDistance", controller ? controller->lodDistance : -2.0f)
													  .Int("controllerState", controller ? static_cast<std::int64_t>(controller->context.currentState) : -1)
													  .Num("fallTime", controller ? controller->fallTime : -1.0f)
													  .Bool("controllerInInteriorWorld", controller && interiorWorld && controller->GetHavokWorld() == interiorWorld)
													  .Str("controllerFlags", controller ? Portal::ControllerFlagsText(*controller) : "")
													  .Int("supportedState", controller ? static_cast<std::int64_t>(std::to_underlying(controller->surfaceInfo.supportedState.get())) : -1)
													  .Num("supportNormalZ", supportNormal[2])
													  .Num("zMin", zMin == FLT_MAX ? 0.0f : zMin)
													  .Num("zMax", zMax == -FLT_MAX ? 0.0f : zMax)
													  .Raw("ancestors", AncestorsJson(root.get()))
													  .Raw("firstShape", shape ? firstShape.Json() : "null")
													  .Json();
		}
		return "[" + json + "]";
	}

	void RevealNodes(RE::NiAVObject* a_object, int a_depth, ShownActors& a_shown)
	{
		if (a_object->GetAppCulled()) {
			a_object->SetAppCulled(false);
			a_shown.unculled.push_back(a_object);
		}
		if (auto* node = a_depth > 0 ? a_object->AsNode() : nullptr) {
			for (auto& child : node->GetChildren()) {
				if (child) {
					RevealNodes(child.get(), a_depth - 1, a_shown);
				}
			}
		}
	}

	void ShowActors(ShownActors& a_shown)
	{
		for (auto& root : g_actorRoots) {
			// The nodes above the root, up to the interior's: a hidden one hides every actor under it,
			// and their bounds follow the actors that walked since the last pass. Only when the root
			// hangs in the interior's 3D: never the exterior scene's nodes.
			std::vector<RE::NiNode*> ancestors;
			for (auto* parent = root->parent; parent && ancestors.size() < kMaxActorAncestors; parent = parent->parent) {
				ancestors.push_back(parent);
				if (parent == g_door.scene.get()) {
					break;
				}
			}
			if (!ancestors.empty() && ancestors.back() == g_door.scene.get()) {
				for (auto* parent : ancestors) {
					if (parent->GetAppCulled()) {
						parent->SetAppCulled(false);
						a_shown.unculled.push_back(parent);
					}
					if (parent != g_door.scene.get()) {
						parent->UpdateWorldBound();  // the actors' node has kFixedBound: a no-op there (0.8.10)
					}
				}
			}
			RevealNodes(root.get(), kActorRevealDepth, a_shown);
			if (root->fadeAmount < 1.0f) {
				a_shown.fadeAmounts.emplace_back(root.get(), root->fadeAmount);
				root->fadeAmount = 1.0f;
			}
			RE::BSVisit::TraverseScenegraphObjects(root.get(), [&](RE::NiAVObject* a_object) {
				if (auto* fade = a_object->AsFadeNode(); fade && fade->GetRuntimeData().currentFade < 1.0f) {
					a_shown.currentFades.emplace_back(fade, fade->GetRuntimeData().currentFade);
					fade->GetRuntimeData().currentFade = 1.0f;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
	}

	void HideActors(const ShownActors& a_shown)
	{
		for (auto* object : a_shown.unculled) {
			object->SetAppCulled(true);
		}
		for (const auto& [object, value] : a_shown.fadeAmounts) {
			object->fadeAmount = value;
		}
		for (const auto& [fade, value] : a_shown.currentFades) {
			fade->GetRuntimeData().currentFade = value;
		}
	}
}

namespace Portal
{
	void AdjustAnimationUpdate(const RE::Actor* a_actor, RE::BSAnimationUpdateData& a_data)
	{
		if (!g_forceAnimation.load(std::memory_order_relaxed)) {
			return;
		}
		if (const auto i = PortalActorIndex(a_actor); i < kMaxAnimatedActors) {
			a_data.unk2A = true;  // a full update: BShkbAnimationGraph::Update skips no frame and moves every bone
			a_data.unk2C = true;  // seen: AE#63575 gives the bones of an unseen actor no motion
			g_forcedAnimationUpdates.fetch_add(1, std::memory_order_relaxed);
			if (!t_ownAnimation) {
				g_gameAnimationUpdates[i].fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	void AddAnimatedActors(const RE::BSTArray<RE::ActorHandle>& a_handles, void* a_list)
	{
		using Push = void (*)(void*, RE::NiPointer<RE::Actor>*);
		static REL::Relocation<Push> push{ REL::ID(41496) };
		if (!g_gameAnimation.load(std::memory_order_relaxed) || !a_list) {
			return;
		}
		for (const auto& handle : a_handles) {
			auto actor = handle.get();
			if (!actor || PortalActorIndex(actor.get()) >= kMaxAnimatedActors) {
				continue;
			}
			// AE#41466's own tests, but for the cell: already in the list when it is attached.
			const auto bits = actor->GetActorRuntimeData().boolBits;
			auto*      cell = actor->GetParentCell();
			auto*      root = actor->Get3D();
			if (!cell || cell->IsAttached() || !root || !root->parent || !actor->GetActorRuntimeData().currentProcess ||
				!bits.all(RE::Actor::BOOL_BITS::kProcessMe) || bits.all(RE::Actor::BOOL_BITS::kInTempChangeList) || actor->IsDisabled() || actor->IsDeleted()) {
				continue;
			}
			push(a_list, &actor);
			g_gameAnimationAdded.fetch_add(1, std::memory_order_relaxed);
		}
	}

	std::string ControllerFlagsText(const RE::bhkCharacterController& a_controller)
	{
		using Flag = RE::CHARACTER_FLAGS;
		static constexpr std::pair<Flag, std::string_view> kNames[]{
			{ Flag::kTryStep, "tryStep" },
			{ Flag::kSupport, "support" },
			{ Flag::kCheckSupport, "checkSupport" },
			{ Flag::kNoSim, "noSim" },
			{ Flag::kFarAway, "farAway" },
			{ Flag::kQuickSimulate, "quickSimulate" },
			{ Flag::kOnStairs, "onStairs" },
			{ Flag::kStuckQuad, "stuck" },
			{ Flag::kNoGravityOnGround, "noGravityOnGround" },
			{ Flag::kNoCharacterCollisions, "noCharacterCollisions" },
			{ Flag::kPossiblePathObstacle, "possiblePathObstacle" },
		};
		auto text = std::format("0x{:08X}", a_controller.flags.underlying());
		for (const auto& [flag, name] : kNames) {
			if (a_controller.flags.all(flag)) {
				text += ' ';
				text += name;
			}
		}
		return text;
	}

	void NoteMovementUpdate(const RE::Actor* a_actor)
	{
		if (const auto i = PortalActorIndex(a_actor); i < kMaxAnimatedActors) {
			g_movementUpdates[i].fetch_add(1, std::memory_order_relaxed);
		}
	}

	void NoteFootFeedback(const RE::Actor* a_actor)
	{
		if (PortalActorIndex(a_actor) < kMaxAnimatedActors) {
			g_footFeedback.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void AfterActorCulled(RE::Actor* a_actor)
	{
		if (g_actorsSeen.load(std::memory_order_relaxed) && PortalActorIndex(a_actor) < kMaxAnimatedActors) {
			ClearNotVisible(a_actor->Get3D());
		}
	}

	ActorScope::ActorScope(const RE::Actor* a_actor)
	{
		if (g_virtualCamera.load(std::memory_order_relaxed) && PortalActorIndex(a_actor) < kMaxAnimatedActors) {
			++t_actorScope;
			_entered = true;
		}
	}

	ActorScope::~ActorScope()
	{
		if (_entered) {
			--t_actorScope;
		}
	}

	RE::NiCamera* ScopedCamera()
	{
		if (t_actorScope == 0) {
			return nullptr;
		}
		auto* camera = g_virtualCamera.load(std::memory_order_relaxed);
		if (camera) {
			g_worldCameraCarried.fetch_add(1, std::memory_order_relaxed);
		}
		return camera;
	}

	bool DistanceThroughDoor(const RE::TESObjectREFR* a_first, const RE::TESObjectREFR* a_second, float& a_squared)
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		if (!space || !g_distanceThroughDoor.load(std::memory_order_relaxed)) {
			return false;
		}
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* other = a_first == player ? a_second : a_second == player ? a_first : nullptr;
		if (!other || other->GetParentCell() != space->cell) {
			return false;
		}
		const auto carried = space->toInterior * player->GetPosition();
		a_squared = carried.GetSquaredDistance(other->GetPosition());
		g_distancesCarried.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool GrantFullProcessing(const RE::Actor* a_actor)
	{
		if (!g_fullProcessing.load(std::memory_order_relaxed) || PortalActorIndex(a_actor) >= kMaxAnimatedActors) {
			return false;
		}
		g_fullProcessingGranted.fetch_add(1, std::memory_order_relaxed);
		return true;
	}
}

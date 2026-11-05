#include "HooksCommon.h"

#include "Portal.h"
#include "Probe.h"

namespace
{
	// BSEffectShader::SetupTechnique (vfunc 2, AE#107524): soft effects bind the main depth buffer's
	// shader view here. The portal counts the effects of its pass (0.8.10: no fire through the portal).
	struct EffectSetupTechnique
	{
		static bool thunk(RE::BSShader* a_this, std::uint32_t a_technique)
		{
			return func(a_this, Portal::BeforeEffectTechnique(a_technique));
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// BSEffectShaderProperty::GetRenderPasses (vfunc 0x2A, AE#106876): an effect with kEffectLighting
	// picks up to 5 lights here (AE#107784). The portal counts its calls for the watched effects; until
	// 0.8.28 it kept its lights away from effects ([Portal] bLightEffects=0).
	struct EffectGetRenderPasses
	{
		static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSShaderProperty* a_this, RE::BSGeometry* a_geometry, std::uint32_t a_renderMode,
			RE::BSShaderAccumulator* a_accumulator)
		{
			Portal::BeforeEffectPasses(a_geometry);
			auto* passes = func(a_this, a_geometry, a_renderMode, a_accumulator);
			Portal::AfterEffectPasses(a_geometry, passes && passes->head);
			return passes;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Actor::ModifyAnimationUpdateData (vfunc 0x79, AE#38054): TESObjectREFR::UpdateAnimation (AE#20119)
	// fills the update's flags here before the animation graphs run. An actor whose 3D is kNotVisible
	// gets an update that moves no bone: the portal's interior actors stood in A-pose (0.8.5-0.8.12).
	struct ModifyAnimationUpdateData
	{
		static void thunk(RE::Actor* a_this, RE::BSAnimationUpdateData& a_data)
		{
			{
				Portal::ActorScope scope{ a_this };
				func(a_this, a_data);
			}
			Portal::AdjustAnimationUpdate(a_this, a_data);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#37277: the high-process update (AE#41369) gives an actor further from the camera than its fade
	// distance only the reduced update. The portal's interior actors are always that far (interior
	// coordinates, the camera in the street): no AI, no movement, no animation events (0.8.5-0.8.13).
	struct ReducedProcessing
	{
		static bool thunk(RE::Actor* a_actor)
		{
			if (Portal::GrantFullProcessing(a_actor)) {
				return false;
			}
			return func(a_actor);
		}

		static inline decltype(&thunk) func;
	};

	// AE#41466: picks the high-process actors whose animation the game updates this frame, only those of
	// attached cells (AE#41370 then gives each a job, AE#41453). a_lists: the list of those, and a
	// second list of some of them (AE#41454).
	struct SelectAnimatedActors
	{
		struct Lists
		{
			void* animated;
			void* second;
		};

		static std::uint32_t thunk(RE::BSTArray<RE::ActorHandle>* a_handles, Lists* a_lists)
		{
			const auto result = func(a_handles, a_lists);
			if (a_handles && a_lists) {
				Portal::AddAnimatedActors(*a_handles, a_lists->animated);
			}
			return result;
		}

		static inline decltype(&thunk) func;
	};

	// AE#37350: the actor's movement update, a job per high-process actor (AE#41306 -> AE#41444).
	struct MovementUpdate
	{
		static std::uint64_t thunk(RE::Actor* a_actor, float a_delta)
		{
			Portal::NoteMovementUpdate(a_actor);
			Portal::ActorScope scope{ a_actor };
			return func(a_actor, a_delta);
		}

		static inline decltype(&thunk) func;
	};

	// Character::Update (vfunc 0xAD, AE#37348 until overridden): everything an NPC does in a frame that
	// measures against the camera runs inside it or inside the movement update above.
	struct CharacterUpdate
	{
		static void thunk(RE::Character* a_this, float a_delta)
		{
			Portal::ActorScope scope{ a_this };
			func(a_this, a_delta);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#36609: the camera of the world, read by the game's distance and fade checks (AE#19384, AE#37286,
	// AE#37361, AE#37507, AE#37690, AE#38054, AE#38308, AE#43851, AE#44213). See docs/DISTANCE_CHECKS.md.
	struct WorldCamera
	{
		static RE::NiCamera* thunk()
		{
			if (auto* camera = Portal::ScopedCamera()) {
				return camera;
			}
			return func();
		}

		static inline decltype(&thunk) func;
	};

	// AE#19823 (TESObjectREFR::GetDistance) and AE#19824 (its square): 10 and 98 call sites. For references
	// in different worlds they answer FLT_MAX. See docs/DISTANCE_CHECKS.md.
	struct ReferenceDistance
	{
		static float thunk(const RE::TESObjectREFR* a_this, const RE::TESObjectREFR* a_other, bool a_disabled, bool a_anyWorld)
		{
			float squared = 0.0f;
			if (Portal::DistanceThroughDoor(a_this, a_other, squared)) {
				return std::sqrt(squared);
			}
			return func(a_this, a_other, a_disabled, a_anyWorld);
		}

		static inline decltype(&thunk) func;
	};

	struct ReferenceDistanceSquared
	{
		static float thunk(const RE::TESObjectREFR* a_this, const RE::TESObjectREFR* a_other, bool a_disabled, bool a_anyWorld)
		{
			float squared = 0.0f;
			if (Portal::DistanceThroughDoor(a_this, a_other, squared)) {
				return squared;
			}
			return func(a_this, a_other, a_disabled, a_anyWorld);
		}

		static inline decltype(&thunk) func;
	};

	// HighActorCuller vfunc 1 (AE#40335): marks an actor's 3D kNotVisible by the main camera's frustum.
	struct HighActorCulled
	{
		static void thunk(void* a_this, RE::Actor* a_actor)
		{
			func(a_this, a_actor);
			Portal::AfterActorCulled(a_actor);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#43690: moves an actor's position towards its character controller, by the foot IK. Without it
	// (kNotVisible, AE#41375, AE#41447) a walking actor's X and Y follow the controller and Z stays:
	// AE#43691 keeps Z for actors in the foot IK database (AE#401101), and only this function moves it.
	struct FootFeedback
	{
		static bool thunk(RE::Actor* a_actor)
		{
			Portal::NoteFootFeedback(a_actor);
			return func(a_actor);
		}

		static inline decltype(&thunk) func;
	};

	// BSXAudio2GameSound::SetEmitterPositionImpl (vfunc 0x0F, AE#68007): every sound's place in the world,
	// moving or not, on the audio thread. It works out listenerDistance from it.
	struct SoundEmitterPosition
	{
		static constexpr std::uint32_t kAtListener = 1u << 10;  // AE#68007 puts such a sound at the listener itself

		static void thunk(RE::BSGameSound* a_this, const RE::NiPoint3& a_position)
		{
			RE::NiPoint3 position = a_position;
			const bool   carried = (a_this->soundType.underlying() & kAtListener) == 0 && !a_this->IsMusic() && Portal::SoundThroughDoor(position);
			func(a_this, position);
			Probe::NoteSound(a_this, position, carried);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#37348 (Actor::Update, vfunc 0xAD) + 0x1F3 calls AE#78274(controller, camera position, 3D root):
	// the character controller's LOD distance (bhkCharacterController + 0x29C) = from the player's
	// camera (AE#50785) to the bound of the actor's 3D. The movement and animation code reads it
	// (AE#37363, AE#37690, AE#37919, AE#38009): far actors move the cheap way. The portal's NPCs, 27 000
	// units off in the interior's own coordinates, sank into the floor while walking (0.8.16). For the
	// actors of the portal's interior the camera is carried through the door first (Portal::IntoInterior).
	struct CharControllerLodDistance
	{
		static void thunk(RE::bhkCharacterController* a_controller, const RE::NiPoint3& a_camera, RE::NiAVObject* a_root)
		{
			RE::NiPoint3 camera = a_camera;
			(void)Portal::IntoInterior(a_root, camera, Portal::Carried::kControllerLod);
			func(a_controller, camera, a_root);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#19384 + 0xE4 calls AE#105486(fade node, its fadeAmount, camera {x, y, z, LOD adjust}): the fade of
	// the animated references of attached cells (AE#18894, AE#18900), from the world camera (AE#36609).
	// It writes the node's fade (+0x130) and its distance from the camera (+0x144); a reference left
	// unfaded is not updated, and BSPSysLODModifier (AE#108480) scales the emission of the particles
	// under the node by that distance. With the portal's interior attached (0.8.16-0.8.18) the camera was
	// 27 000 units off: the hearth's flames emitted no particle, the candles burned with none. For the
	// references of the portal's interior the camera is carried through the door first.
	struct FadeNodeDistance
	{
		static void thunk(RE::BSFadeNode* a_node, float a_fadeAmount, const float* a_camera)
		{
			RE::NiPoint3 point{ a_camera[0], a_camera[1], a_camera[2] };
			if (Portal::IntoInterior(a_node, point, Portal::Carried::kFade)) {
				const float camera[4]{ point.x, point.y, point.z, a_camera[3] };
				func(a_node, a_fadeAmount, camera);
				return;
			}
			func(a_node, a_fadeAmount, a_camera);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#74651 = NiPSysEmitter vfunc 0x2A, every emitter class: makes a_count * AE#388630 particles.
	struct EmitParticles
	{
		static void thunk(RE::NiPSysModifier* a_this, float a_time, std::uint16_t a_count, const float* a_ages)
		{
			Portal::BeforeEmission(a_this, a_count);
			func(a_this, a_time, a_count, a_ages);
		}

		static inline decltype(&thunk) func;
	};

	// AE#36559 (RenderPlayerView) + 0x17E calls AE#69353 (JobList::Finish) for its scene list jobs, between
	// RenderWaterEffects and ProcessAllQueuedLights: no job runs then and the world's accumulation has not begun.
	struct SceneListFence
	{
		static void thunk(void* a_jobList)
		{
			func(a_jobList);
			Portal::OnSceneListFence();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#77463: the quads of NiParticles, built on the CPU to face RendererShadowState's camera, called by
	// the batch renderer's generic draw (AE#107637). Flames and smoke are particles, not billboard nodes. A
	// detour, not a call hook: the mirrors mod hooks that call site and insists on finding it native.
	struct PackParticles
	{
		static void thunk(std::uint32_t a_count, RE::NiParticles* a_particles, void* a_vertices)
		{
			Portal::PackedCamera saved;
			Portal::BeforeParticlePack(saved);
			func(a_count, a_particles, a_vertices);
			Portal::AfterParticlePack(saved);
		}

		static inline decltype(&thunk) func;
	};

	// ImageSpaceEffectHDR::Render (vfunc 1, AE#107183): eye adaptation, tone mapping, bloom, cinematic. After
	// it the bound render target holds the frame in LDR, and the adaptation targets this frame's (T12).
	struct ImageSpaceHdrRender
	{
		static void thunk(RE::ImageSpaceEffect* a_this, RE::BSTriShape* a_shape, RE::ImageSpaceEffectParam* a_param)
		{
			func(a_this, a_shape, a_param);
			Portal::AfterImageSpaceHdr();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#36558 + 0x11D calls RenderPlayerView (AE#36559: this, int, bool). After it kMAIN holds the frame
	// before the image space (F11 writes it, 0.8.25).
	struct PlayerView
	{
		static void thunk(void* a_this, std::uint32_t a_arg2, bool a_arg3)
		{
			func(a_this, a_arg2, a_arg3);
			Portal::AfterPlayerView();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#52638 (the subtitle manager's update) + 0x122 calls AE#19784 for the speaker: its position and the
	// space it stands in, which AE#18517 measures against the player's (0.8.25).
	struct SubtitleSpeakerPosition
	{
		struct WorldPosition
		{
			RE::NiPoint3  position;
			std::uint32_t pad0C;
			RE::TESForm*  space;  // the interior cell, or the exterior cell's worldspace
		};
		static_assert(sizeof(WorldPosition) == 0x18);

		static WorldPosition* thunk(RE::TESObjectREFR* a_speaker, WorldPosition* a_out)
		{
			auto* result = func(a_speaker, a_out);
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			if (result && cell && Portal::SpeakerThroughDoor(a_speaker, result->position)) {
				auto* world = cell->IsInteriorCell() ? nullptr : cell->GetRuntimeData().worldSpace;
				result->space = world ? static_cast<RE::TESForm*>(world) : cell;
			}
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace Hooks::Portals
{
	void InstallVfuncs()
	{
		REL::Relocation<std::uintptr_t> effectShader{ RE::VTABLE_BSEffectShader[0] };
		WriteVfunc<EffectSetupTechnique>(effectShader, 0x2);
		REL::Relocation<std::uintptr_t> effectProperty{ RE::VTABLE_BSEffectShaderProperty[0] };
		WriteVfunc<EffectGetRenderPasses>(effectProperty, 0x2A);
		REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[0] };  // the interior's NPCs; Actor's own vtable has the same function
		WriteVfunc<ModifyAnimationUpdateData>(character, 0x79);
		WriteVfunc<CharacterUpdate>(character, 0xAD);  // Actor::Update
		REL::Relocation<std::uintptr_t> highActorCuller{ RE::VTABLE_HighActorCuller[0] };
		WriteVfunc<HighActorCulled>(highActorCuller, 0x1);
		REL::Relocation<std::uintptr_t> gameSound{ RE::VTABLE_BSXAudio2GameSound[0] };
		WriteVfunc<SoundEmitterPosition>(gameSound, 0x0F);
		REL::Relocation<std::uintptr_t> imageSpaceHdr{ RE::VTABLE_ImageSpaceEffectHDR[0] };
		WriteVfunc<ImageSpaceHdrRender>(imageSpaceHdr, 0x1);
	}

	void AttachDetours(DetourTransaction& a_transaction)
	{
		if (!REL::Module::IsAE()) {
			return;
		}
		a_transaction.Attach<ReducedProcessing>("Actor::ReducedProcessing", REL::Relocation<std::uintptr_t>{ REL::ID(37277) }.address());
		a_transaction.Attach<SelectAnimatedActors>("ProcessLists::SelectAnimatedActors", REL::Relocation<std::uintptr_t>{ REL::ID(41466) }.address());
		a_transaction.Attach<MovementUpdate>("Actor::UpdateMovement", REL::Relocation<std::uintptr_t>{ REL::ID(37350) }.address());
		a_transaction.Attach<EmitParticles>("NiPSysEmitter::EmitParticles", REL::Relocation<std::uintptr_t>{ REL::ID(74651) }.address());
		a_transaction.Attach<PackParticles>("NiParticles::PackQuads", RELOCATION_ID(75657, 77463));
		a_transaction.Attach<WorldCamera>("Main::WorldCamera", REL::Relocation<std::uintptr_t>{ REL::ID(36609) }.address());
		a_transaction.Attach<FootFeedback>("Actor::FootFeedback", REL::Relocation<std::uintptr_t>{ REL::ID(43690) }.address());
		a_transaction.Attach<ReferenceDistance>("TESObjectREFR::GetDistance", RELOCATION_ID(19396, 19823));
		a_transaction.Attach<ReferenceDistanceSquared>("TESObjectREFR::GetDistanceSquared", REL::Relocation<std::uintptr_t>{ REL::ID(19824) }.address());
	}

	bool InstallCalls()
	{
		if (!REL::Module::IsAE()) {
			return false;
		}
		const bool lod = WriteCall<CharControllerLodDistance>("Actor::Update -> CharController LOD distance", REL::ID(37348), 0x1F3, REL::ID(78274));
		const bool fade = WriteCall<FadeNodeDistance>("animated reference fade -> fade node distance", REL::ID(19384), 0xE4, REL::ID(105486));
		const bool view = WriteCall<PlayerView>("Main -> RenderPlayerView", REL::ID(36558), 0x11D, REL::ID(36559));
		const bool subtitles = WriteCall<SubtitleSpeakerPosition>("subtitle update -> speaker position", REL::ID(52638), 0x122, REL::ID(19784));
		const bool fence = WriteCall<SceneListFence>("RenderPlayerView -> scene list fence", REL::ID(36559), 0x17E, REL::ID(69353));
		logs::info("call hooks installed: controller LOD {}, fade {}, pass at the scene list fence {}, player view {}, subtitles {}", lod, fade, fence, view, subtitles);
		return lod && fade;
	}
}

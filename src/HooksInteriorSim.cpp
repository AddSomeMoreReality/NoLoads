#include "HooksCommon.h"

#include "InteriorSim.h"
#include "Portal.h"

namespace
{
	// bhkWorld::vfunc[0x32] (also bhkWorldM's): one step of a Bethesda physics world; the time step
	// comes from globals (AE#77921, the warm start loop, calls it with 1). After the game stepped the
	// player's world, InteriorSim may step the kept interior's world the same way.
	struct BhkWorldStep
	{
		static std::uint64_t thunk(RE::bhkWorld* a_world, std::int32_t a_flag)
		{
			InteriorSim::NoteWorldStep(a_world);
			const auto result = func(a_world, a_flag);
			InteriorSim::AfterWorldStep(a_world, a_flag, func);
			return result;
		}

		static inline decltype(&thunk) func;
	};

	// NiAVObject::OnVisible (vfunc 0x34): the culling process calls it for every object that passed the
	// frustum and occlusion tests (BSCullingProcess::Process, AE#76553); nodes then cull their children,
	// shapes add themselves to the render passes. InteriorSim counts the calls for what it hung into
	// the scene. One instance per vtable: Slot only tells them apart.
	template <std::size_t Slot>
	struct OnVisible
	{
		static void thunk(RE::NiAVObject* a_this, RE::NiCullingProcess& a_process, std::int32_t a_alphaGroupIndex)
		{
			InteriorSim::NoteVisible(a_this);
			Portal::NoteVisible(a_this);
			func(a_this, a_process, a_alphaGroupIndex);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// AE#13291: TES's per-frame update of the loaded cells (a job, AE#39067): it adds the frame's time to
	// the cells' time (AE#400333) and updates the player's interior (AE#18889), or the grid's cells
	// (AE#15963 -> AE#18890) and the world's persistent cell. InteriorSim adds the interior it attached.
	struct UpdateCells
	{
		static void thunk(RE::TES* a_this, float a_delta)
		{
			func(a_this, a_delta);
			InteriorSim::AfterCellsUpdate();
		}

		static inline decltype(&thunk) func;
	};

	// AE#13325: the animation graphs of the loaded cells' animated references (AE#39045), the same choice
	// of cells (AE#18899 queues jobs into a_jobs, AE#18900 updates at once).
	struct UpdateAnimatedRefs
	{
		static void thunk(RE::TES* a_this, void* a_jobs)
		{
			func(a_this, a_jobs);
			InteriorSim::AfterAnimatedRefs(a_jobs);
		}

		static inline decltype(&thunk) func;
	};

	// AE#19130: the cell detach (kDetaching -> kUnattached), from the game's transitions, unloads and ours.
	struct CellDetach
	{
		static void thunk(RE::TESObjectCELL* a_cell, bool a_unk)
		{
			InteriorSim::BeforeCellDetach(a_cell);
			func(a_cell, a_unk);
		}

		static inline decltype(&thunk) func;
	};

	// AE#106385: ShadowSceneNode::SetPortalGraph (the rooms and portals the world's culling uses), called
	// by the cell attach (AE#18976) with the interior's own graph.
	struct SetPortalGraph
	{
		static void thunk(RE::ShadowSceneNode* a_this, void* a_graph)
		{
			if (InteriorSim::KeepPortalGraph()) {
				return;
			}
			func(a_this, a_graph);
		}

		static inline decltype(&thunk) func;
	};
}

namespace Hooks::Interior
{
	void InstallVfuncs()
	{
		REL::Relocation<std::uintptr_t> niNode{ RE::VTABLE_NiNode[0] };
		REL::Relocation<std::uintptr_t> fadeNode{ RE::VTABLE_BSFadeNode[0] };
		REL::Relocation<std::uintptr_t> multiBoundNode{ RE::VTABLE_BSMultiBoundNode[0] };
		REL::Relocation<std::uintptr_t> multiBoundRoom{ RE::VTABLE_BSMultiBoundRoom[0] };
		REL::Relocation<std::uintptr_t> triShape{ RE::VTABLE_BSTriShape[0] };
		REL::Relocation<std::uintptr_t> dynamicTriShape{ RE::VTABLE_BSDynamicTriShape[0] };
		REL::Relocation<std::uintptr_t> subIndexTriShape{ RE::VTABLE_BSSubIndexTriShape[0] };
		WriteVfunc<OnVisible<0>>(niNode, 0x34);
		WriteVfunc<OnVisible<1>>(fadeNode, 0x34);
		WriteVfunc<OnVisible<2>>(multiBoundNode, 0x34);
		WriteVfunc<OnVisible<3>>(multiBoundRoom, 0x34);
		WriteVfunc<OnVisible<4>>(triShape, 0x34);
		WriteVfunc<OnVisible<5>>(dynamicTriShape, 0x34);
		WriteVfunc<OnVisible<6>>(subIndexTriShape, 0x34);
	}

	void AttachDetours(DetourTransaction& a_transaction)
	{
		if (!REL::Module::IsAE()) {
			return;
		}
		a_transaction.Attach<BhkWorldStep>("bhkWorld::vfunc[0x32]", REL::Relocation<std::uintptr_t>{ REL::ID(77851) }.address());
		a_transaction.Attach<UpdateCells>("TES::UpdateCells", REL::Relocation<std::uintptr_t>{ REL::ID(13291) }.address());
		a_transaction.Attach<UpdateAnimatedRefs>("TES::UpdateAnimatedRefs", REL::Relocation<std::uintptr_t>{ REL::ID(13325) }.address());
		a_transaction.Attach<SetPortalGraph>("ShadowSceneNode::SetPortalGraph", REL::Relocation<std::uintptr_t>{ REL::ID(106385) }.address());
		a_transaction.Attach<CellDetach>("TESObjectCELL::Detach", REL::Relocation<std::uintptr_t>{ REL::ID(19130) }.address());
	}
}

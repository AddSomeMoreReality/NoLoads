#include "HooksCommon.h"

#include "ActorJumps.h"
#include "InteriorSim.h"
#include "Physics.h"
#include "Preload.h"
#include "Snapshot.h"

namespace
{
	using Hooks::AddPlayerPos;
	using Hooks::AddTesState;
	using Hooks::HookScope;

	// ------------------------------------------------------------------
	// Detours on regular functions (Microsoft Detours relocates the prologue).
	// ------------------------------------------------------------------
	struct SetCurrentCell
	{
		static void thunk(RE::TES* a_this, RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_position, float a_unk4)
		{
			Trace::Args args;
			args.Cell("cell", a_cell)
				.Pos("pos", a_position)
				.Num("unk4", a_unk4)
				.Hex("unk4_bits", std::bit_cast<std::uint32_t>(a_unk4));  // H8: 2147352576.0 in every 0.3.0 call
			HookScope scope{ "TES.SetCurrentCell", AddTesState(args), _ReturnAddress() };
			InteriorSim::Detach("TES.SetCurrentCell");  // an attached interior goes back to the buffer first, as the game expects
			{
				const auto guard = InteriorSim::GuardWorlds();
				func(a_this, a_cell, a_position, a_unk4);
			}
			Trace::Args leave;
			scope.Leave(AddTesState(leave));
			Preload::CheckStates("TES.SetCurrentCell");
		}

		static inline decltype(&thunk) func;
	};

	struct PurgeBufferedCells
	{
		static void thunk(RE::TES* a_this)
		{
			Trace::Args args;
			HookScope   scope{ "TES.PurgeBufferedCells", AddTesState(args), _ReturnAddress() };
			InteriorSim::Detach("TES.PurgeBufferedCells");
			{
				const auto guard = InteriorSim::GuardWorlds();
				func(a_this);
			}
			scope.Leave();
			Preload::CheckStates("TES.PurgeBufferedCells");
			Snapshot::Take("PurgeBufferedCells");
		}

		static inline decltype(&thunk) func;
	};

	struct QueueCellLoad
	{
		static void thunk(RE::ExteriorCellLoader* a_this, RE::TESWorldSpace* a_world, std::int16_t a_x, std::int16_t a_y, bool a_unk)
		{
			Trace::Args args;
			args.FormID("world", a_world ? a_world->GetFormID() : 0).XY("xy", a_x, a_y).Bool("unk", a_unk);
			if (Trace::IsMainThread()) {
				// H5: grid/queued centers and the player position at the moment of the preload
				AddPlayerPos(AddTesState(args));
			}
			HookScope scope{ "ExteriorCellLoader.QueueCellLoad", args, _ReturnAddress() };
			func(a_this, a_world, a_x, a_y, a_unk);
			scope.Leave();
		}

		static inline decltype(&thunk) func;
	};

	struct CenterOnCell
	{
		static bool thunk(RE::PlayerCharacter* a_this, const char* a_cellName, RE::TESObjectCELL* a_cell)
		{
			HookScope scope{ "PlayerCharacter.CenterOnCell",
				Trace::Args{}.Str("name", a_cellName ? a_cellName : "").Cell("cell", a_cell), _ReturnAddress() };
			const bool result = func(a_this, a_cellName, a_cell);
			scope.Leave(Trace::Args{}.Bool("result", result));
			return result;
		}

		static inline decltype(&thunk) func;
	};

	struct MoveTo
	{
		static void thunk(RE::TESObjectREFR* a_this, const RE::ObjectRefHandle& a_target, RE::TESObjectCELL* a_cell,
			RE::TESWorldSpace* a_world, const RE::NiPoint3& a_position, const RE::NiPoint3& a_rotation)
		{
			if (!a_this || !a_this->IsPlayerRef()) {
				return func(a_this, a_target, a_cell, a_world, a_position, a_rotation);
			}

			const auto target = a_target.get();
			HookScope  scope{ "TESObjectREFR.MoveTo",
                Trace::Args{}
                    .Form("target", target.get())
                    .Cell("cell", a_cell)
                    .Form("world", a_world)
                    .Pos("pos", a_position)
                    .Pos("rot", a_rotation),
                _ReturnAddress() };
			func(a_this, a_target, a_cell, a_world, a_position, a_rotation);
			scope.Leave();
		}

		static inline decltype(&thunk) func;
	};

	// ------------------------------------------------------------------
	// Havok (AE IDs found with tools/name_functions.py from the 0.6.0 profile; no SE IDs known).
	// ------------------------------------------------------------------

	// hkpWorld::stepDeltaTime(hkReal) -> hkpStepResult (Havok 2010 signature). Runs every frame:
	// only counted here, details go to the loading screen report and the probe.
	struct HavokStep
	{
		static std::int32_t thunk(void* a_world, float a_deltaTime)
		{
			const auto start = Trace::NowUs();
			const auto result = func(a_world, a_deltaTime);
			Physics::OnStep(a_world, a_deltaTime, Trace::NowUs() - start);
			return result;
		}

		static inline decltype(&thunk) func;
	};

	// TES as BSTEventSink<PositionPlayerEvent> (TES+0x68, vfunc 1). In the 0.6.0 profile 98% of
	// kPost -> kFinish is under this call: TES steps the physics world (warm start).
	struct TesPositionPlayer
	{
		static RE::BSEventNotifyControl thunk(void* a_sink, const RE::PositionPlayerEvent* a_event, void* a_source)
		{
			const auto before = Physics::Total();
			HookScope  scope{ "TES.ProcessEvent.PositionPlayer",
                Trace::Args{}
                    .Int("type", a_event ? std::to_underlying(a_event->type.get()) : -1)
                    .Num("warmStart", Physics::WarmStartValue()),
                _ReturnAddress() };
			Physics::SetInWarmStart(true);
			const auto result = func(a_sink, a_event, a_source);
			Physics::SetInWarmStart(false);
			const auto after = Physics::Total();
			scope.Leave(Trace::Args{}
							.Int("steps", static_cast<std::int64_t>(after.steps - before.steps))
							.Num("simSeconds", after.simSeconds - before.simSeconds)
							.Int("stepUs", after.stepUs - before.stepUs));
			return result;
		}

		static inline decltype(&thunk) func;
	};

	// The bPreloadLinkedInteriors manager: PlayerCharacter::Update -> AE#40845 (0x18E bytes in .pdata).
	bool FromLinkedPreload(const void* a_returnAddress)
	{
		static const auto begin = REL::Relocation<std::uintptr_t>{ REL::ID(40845) }.address();
		const auto        address = reinterpret_cast<std::uintptr_t>(a_returnAddress);
		return address >= begin && address < begin + 0x18E;
	}

	// TES: take an interior out of TES::interiorBuffer and unload it (AE#13370). Only the
	// bPreloadLinkedInteriors manager calls it, for interiors whose doors are not among the nearest
	// ones: in 0.8.0 it unloaded the kept interior on the frame after every load.
	struct TesUnloadBufferedInterior
	{
		static void thunk(RE::TES* a_this, RE::TESObjectCELL* a_cell)
		{
			if (Preload::BlockUnload(a_cell)) {
				return;
			}
			const auto guard = InteriorSim::GuardWorlds();
			func(a_this, a_cell);
		}

		static inline decltype(&thunk) func;
	};

	// TES: put an interior at the front of TES::interiorBuffer; a new one pushes the last one out
	// and unloads it (AE#13434).
	struct TesBufferInterior
	{
		static void thunk(RE::TES* a_this, RE::TESObjectCELL* a_cell)
		{
			const auto guard = InteriorSim::GuardWorlds();
			func(a_this, a_cell);
		}

		static inline decltype(&thunk) func;
	};

	// The cell unload itself (AE#13721: sets kUnloading and frees the loaded data). Called from ~25
	// places: buffer eviction (AE#13434), SetCurrentCell, the grid, CellLoaderTask::Cancel, ...
	struct CellUnload
	{
		static std::uint64_t thunk(void* a_manager, RE::TESObjectCELL* a_cell)
		{
			if (Preload::BlockCellUnload(a_cell, _ReturnAddress())) {
				return 0;
			}
			return func(a_manager, a_cell);
		}

		static inline decltype(&thunk) func;
	};

	// ExteriorCellLoader: cancel the queued load of a cell, by form ID (AE#18547). Also called by
	// CellLoaderTask itself; only the bPreloadLinkedInteriors manager's calls are skipped for the kept interior.
	struct LoaderCancel
	{
		static std::uint64_t thunk(RE::ExteriorCellLoader* a_this, RE::FormID a_cell)
		{
			if (FromLinkedPreload(_ReturnAddress()) && Preload::BlockCancel(a_cell)) {
				return 0;
			}
			return func(a_this, a_cell);
		}

		static inline decltype(&thunk) func;
	};

	// TESObjectREFR::SetPosition (AE#19790): who moves an NPC far in one call (0.8.25, ActorJumps).
	struct ReferenceSetPosition
	{
		static void thunk(RE::TESObjectREFR* a_this, const RE::NiPoint3& a_position)
		{
			ActorJumps::BeforeSetPosition(a_this, a_position);
			func(a_this, a_position);
		}

		static inline decltype(&thunk) func;
	};
}

namespace Hooks::Tes
{
	void AttachDetours(DetourTransaction& a_transaction)
	{
		a_transaction.Attach<SetCurrentCell>("TES::SetCurrentCell", RELOCATION_ID(13171, 13316));
		a_transaction.Attach<PurgeBufferedCells>("TES::PurgeBufferedCells", RELOCATION_ID(13159, 13299));
		a_transaction.Attach<QueueCellLoad>("ExteriorCellLoader::QueueCellLoad", RELOCATION_ID(18150, 18541));
		a_transaction.Attach<CenterOnCell>("PlayerCharacter::CenterOnCell_Impl", RELOCATION_ID(39365, 40437));
		a_transaction.Attach<MoveTo>("TESObjectREFR::MoveTo_Impl", RELOCATION_ID(56227, 56626));
		if (REL::Module::IsAE()) {
			a_transaction.Attach<HavokStep>("hkpWorld::stepDeltaTime", REL::Relocation<std::uintptr_t>{ REL::ID(61410) }.address());
			a_transaction.Attach<TesPositionPlayer>("TES::ProcessEvent(PositionPlayerEvent)", REL::Relocation<std::uintptr_t>{ REL::ID(13419) }.address());
			a_transaction.Attach<TesUnloadBufferedInterior>("TES::UnloadBufferedInterior", REL::Relocation<std::uintptr_t>{ REL::ID(13370) }.address());
			a_transaction.Attach<TesBufferInterior>("TES::BufferInterior", REL::Relocation<std::uintptr_t>{ REL::ID(13434) }.address());
			a_transaction.Attach<LoaderCancel>("ExteriorCellLoader::CancelCellLoad", REL::Relocation<std::uintptr_t>{ REL::ID(18547) }.address());
			a_transaction.Attach<CellUnload>("UnloadCell", REL::Relocation<std::uintptr_t>{ REL::ID(13721) }.address());
			a_transaction.Attach<ReferenceSetPosition>("TESObjectREFR::SetPosition", RELOCATION_ID(19363, 19790));
		}
	}

	void BufferInteriorOriginal(RE::TES* a_tes, RE::TESObjectCELL* a_cell)
	{
		TesBufferInterior::func(a_tes, a_cell);
	}
}

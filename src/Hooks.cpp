#include "Hooks.h"

#include "Detectors.h"
#include "Events.h"
#include "Hud.h"
#include "InteriorSim.h"
#include "MainThread.h"
#include "Physics.h"
#include "Portal.h"
#include "Preload.h"
#include "Probe.h"
#include "Settings.h"
#include "Snapshot.h"
#include "Trace.h"

#include <detours.h>

namespace
{
	Trace::Args& AddTesState(Trace::Args& a_args)
	{
		if (auto* tes = RE::TES::GetSingleton()) {
			a_args.Cell("tesInterior", tes->interiorCell)
				.Form("tesWorld", tes->GetRuntimeData2().worldSpace)
				.XY("grid", tes->currentGridX, tes->currentGridY)
				.XY("queued", tes->currentQueuedX, tes->currentQueuedY);
		}
		return a_args;
	}

	Trace::Args& AddPlayerPos(Trace::Args& a_args)
	{
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			a_args.Pos("playerPos", player->GetPosition()).Cell("playerCell", player->GetParentCell());
		}
		return a_args;
	}

	// Writes hook.<name>.enter on construction and hook.<name>.leave with dur_us on Leave().
	class HookScope
	{
	public:
		HookScope(std::string_view a_name, const Trace::Args& a_args, const void* a_caller) :
			_name(a_name),
			_caller(a_caller)
		{
			Trace::Write(std::format("hook.{}.enter", _name), a_args, _caller);
			_start = Trace::NowUs();
		}

		void Leave(Trace::Args a_args = {})
		{
			a_args.Int("dur_us", Trace::NowUs() - _start);
			Trace::Write(std::format("hook.{}.leave", _name), a_args, _caller);
		}

	private:
		std::string_view _name;
		const void*      _caller;
		std::int64_t     _start{ 0 };
	};

	// ------------------------------------------------------------------
	// TESObjectDOOR::Activate. May run on a worker thread, not the main one.
	// ------------------------------------------------------------------
	struct DoorActivate
	{
		static bool thunk(RE::TESObjectDOOR* a_this, RE::TESObjectREFR* a_targetRef, RE::TESObjectREFR* a_activatorRef,
			std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_targetCount)
		{
			const auto* caller = _ReturnAddress();

			const bool byPlayer = a_activatorRef && a_activatorRef->IsPlayerRef();
			const auto* teleport = a_targetRef ? a_targetRef->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
			const auto* teleportData = teleport ? teleport->teleportData : nullptr;
			if (!byPlayer && !teleportData) {
				return func(a_this, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);
			}

			Trace::Args args;
			args.Form("door", a_targetRef)
				.Form("activator", a_activatorRef)
				.Cell("doorCell", a_targetRef ? a_targetRef->GetParentCell() : nullptr)
				.Int("arg3", a_arg3)
				.Int("count", a_targetCount)
				.Bool("teleport", teleportData != nullptr);
			if (teleportData) {
				const auto linked = teleportData->linkedDoor.get();
				auto*      destCell = linked ? linked->GetParentCell() : nullptr;
				args.Form("linkedDoor", linked.get())
					.Cell("destCell", destCell)
					.Pos("destPos", teleportData->position)
					.Int("flags", teleportData->flags);
				if (byPlayer) {
					logs::info("[Door] {} -> {}", Trace::FormString(a_targetRef->GetParentCell()), Trace::FormString(destCell));
				}
			}

			auto*     player = RE::PlayerCharacter::GetSingleton();
			HookScope scope{ "TESObjectDOOR.Activate", args, caller };

			if (teleportData && byPlayer && Settings::Get().blockDoorsInCombat && player && player->IsInCombat()) {
				// Not on the main thread here, so the UI call is deferred to the Update tick.
				MainThread::Post([]() {
					RE::SendHUDMessage::ShowHUDMessage("Нельзя уйти посреди боя");
				});
				scope.Leave(Trace::Args{}.Bool("result", false).Bool("blocked", true));
				return false;
			}

			const bool result = func(a_this, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);

			Trace::Args leave;
			leave.Bool("result", result);
			if (byPlayer && player) {
				// H1: is the teleport only queued when Activate returns?
				leave.Bool("queuedValid", player->GetPlayerRuntimeData().queuedTargetLoc.isValid);
			}
			if (byPlayer && teleportData && result) {
				Events::OnPlayerDoorActivated();
			}
			scope.Leave(leave);
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// ------------------------------------------------------------------
	// GridCellArray (exterior grid, uGridsToLoad x uGridsToLoad). Main thread.
	// ------------------------------------------------------------------
	Trace::Args GridArgs(RE::GridCellArray* a_grid)
	{
		Trace::Args args;
		args.Int("length", a_grid->length);
		if (auto* tes = RE::TES::GetSingleton()) {
			args.Form("world", tes->GetRuntimeData2().worldSpace)
				.XY("grid", tes->currentGridX, tes->currentGridY)
				.XY("queued", tes->currentQueuedX, tes->currentQueuedY);
			if (const auto* ae = tes->GetAERuntimeData()) {
				args.Num("gridBufX", ae->gridBufferDistanceX)
					.Num("gridBufY", ae->gridBufferDistanceY);
			}
		}
		return args;
	}

	Trace::Args GridItemArgs(RE::GridCellArray* a_grid, std::uint32_t a_x, std::uint32_t a_y)
	{
		return Trace::Args{}.XY("item", a_x, a_y).Cell("cell", a_grid->GetCell(a_x, a_y));
	}

	Trace::Args GridMoveArgs(RE::GridCellArray* a_grid, std::uint32_t a_fromX, std::uint32_t a_fromY, std::uint32_t a_toX, std::uint32_t a_toY)
	{
		return Trace::Args{}
		    .XY("from", a_fromX, a_fromY)
		    .XY("to", a_toX, a_toY)
		    .Cell("fromCell", a_grid->GetCell(a_fromX, a_fromY))
		    .Cell("toCell", a_grid->GetCell(a_toX, a_toY));
	}

	struct GridDetachAll
	{
		static void thunk(RE::GridCellArray* a_this)
		{
			Preload::CheckStates("GridCellArray.DetachAll.enter");
			InteriorSim::Detach("GridCellArray.DetachAll");
			HookScope scope{ "GridCellArray.DetachAll", GridArgs(a_this), _ReturnAddress() };
			{
				const auto guard = InteriorSim::GuardWorlds();
				func(a_this);
			}
			scope.Leave();
			Preload::CheckStates("GridCellArray.DetachAll.leave");
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GridKillAll
	{
		static void thunk(RE::GridCellArray* a_this)
		{
			InteriorSim::Detach("GridCellArray.KillAll");
			HookScope scope{ "GridCellArray.KillAll", GridArgs(a_this), _ReturnAddress() };
			func(a_this);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GridSetCenter
	{
		static bool thunk(RE::GridCellArray* a_this, std::int32_t a_x, std::int32_t a_y)
		{
			HookScope  scope{ "GridCellArray.SetCenter", GridArgs(a_this).XY("xy", a_x, a_y), _ReturnAddress() };
			const auto t0 = Trace::NowUs();
			const bool result = func(a_this, a_x, a_y);
			const auto us = Trace::NowUs() - t0;
			// Player position shows how far into the new cell the grid shift happened (H5).
			scope.Leave(AddPlayerPos(GridArgs(a_this).Bool("result", result)));

			auto* tes = RE::TES::GetSingleton();
			logs::info("[Grid] SetCenter({}, {}) in {} -> {} ({} us)",
				a_x, a_y, Trace::FormString(tes ? tes->GetRuntimeData2().worldSpace : nullptr), result, us);
			Snapshot::Take("SetCenter");
			Preload::CheckStates("GridCellArray.SetCenter");
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GridShift
	{
		static void thunk(RE::GridCellArray* a_this, std::int32_t a_x, std::int32_t a_y)
		{
			HookScope scope{ "GridCellArray.Shift", GridArgs(a_this).XY("xy", a_x, a_y), _ReturnAddress() };
			func(a_this, a_x, a_y);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GridDetach
	{
		static void thunk(RE::GridCellArray* a_this, std::uint32_t a_x, std::uint32_t a_y)
		{
			InteriorSim::CellDetaching(a_this->GetCell(a_x, a_y), "GridCellArray.Detach");
			HookScope scope{ "GridCellArray.Detach", GridItemArgs(a_this, a_x, a_y), _ReturnAddress() };
			func(a_this, a_x, a_y);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GridClearItem
	{
		static void thunk(RE::GridCellArray* a_this, std::uint32_t a_x, std::uint32_t a_y)
		{
			InteriorSim::CellDetaching(a_this->GetCell(a_x, a_y), "GridCellArray.ClearItem");
			HookScope scope{ "GridCellArray.ClearItem", GridItemArgs(a_this, a_x, a_y), _ReturnAddress() };
			func(a_this, a_x, a_y);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GridMoveItem
	{
		static void thunk(RE::GridCellArray* a_this, std::uint32_t a_fromX, std::uint32_t a_fromY, std::uint32_t a_toX, std::uint32_t a_toY)
		{
			HookScope scope{ "GridCellArray.MoveItem", GridMoveArgs(a_this, a_fromX, a_fromY, a_toX, a_toY), _ReturnAddress() };
			func(a_this, a_fromX, a_fromY, a_toX, a_toY);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct GridSwapItem
	{
		static void thunk(RE::GridCellArray* a_this, std::uint32_t a_fromX, std::uint32_t a_fromY, std::uint32_t a_toX, std::uint32_t a_toY)
		{
			HookScope scope{ "GridCellArray.SwapItem", GridMoveArgs(a_this, a_fromX, a_fromY, a_toX, a_toY), _ReturnAddress() };
			func(a_this, a_fromX, a_fromY, a_toX, a_toY);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// ------------------------------------------------------------------
	// CellLoaderTask. Run executes on IO threads: only the task's own fields are read here
	// (form IDs, no editor IDs), and the task is not touched after the original call.
	// ------------------------------------------------------------------
	Trace::Args TaskArgs(RE::CellLoaderTask* a_task)
	{
		const auto& data = a_task->GetRuntimeData();
		return Trace::Args{}
		    .Hex("task", reinterpret_cast<std::uintptr_t>(a_task))
		    .FormID("world", data.world ? data.world->GetFormID() : 0)
		    .FormID("cell", data.cell ? data.cell->GetFormID() : 0)
		    .XY("xy", data.x, data.y)
		    .Int("state", data.state)
		    .Bool("interior", data.interior)
		    .Int("unk3D", data.unk3D);
	}

	struct TaskRun
	{
		static void thunk(RE::CellLoaderTask* a_this)
		{
			HookScope scope{ "CellLoaderTask.Run", TaskArgs(a_this), _ReturnAddress() };
			func(a_this);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TaskFinish
	{
		static void thunk(RE::CellLoaderTask* a_this)
		{
			HookScope scope{ "CellLoaderTask.Finish", TaskArgs(a_this), _ReturnAddress() };
			func(a_this);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TaskCancel
	{
		static void thunk(RE::CellLoaderTask* a_this, std::uint32_t a_previousState, void* a_unk)
		{
			HookScope scope{ "CellLoaderTask.Cancel", TaskArgs(a_this).Int("previousState", a_previousState), _ReturnAddress() };
			func(a_this, a_previousState, a_unk);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TaskGetDescription
	{
		static bool thunk(RE::CellLoaderTask* a_this, char* a_dest, std::uint32_t a_maxCount)
		{
			HookScope  scope{ "CellLoaderTask.GetDescription", TaskArgs(a_this), _ReturnAddress() };
			const bool result = func(a_this, a_dest, a_maxCount);
			Trace::Args leave;
			leave.Bool("result", result);
			if (a_dest && a_maxCount > 0) {
				leave.Str("desc", std::string_view{ a_dest, ::strnlen(a_dest, a_maxCount) });
			}
			scope.Leave(leave);
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TaskPostProcess
	{
		static void thunk(RE::CellLoaderTask* a_this)
		{
			HookScope scope{ "CellLoaderTask.PostProcess", TaskArgs(a_this), _ReturnAddress() };
			func(a_this);
			scope.Leave();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// ------------------------------------------------------------------
	// PlayerCharacter::Update (Actor::Update, 0xAD): frame counter for the trace.
	// ------------------------------------------------------------------
	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			Trace::NextFrame();
			Events::OnPlayerTick();
			if (_first) {
				_first = false;
				Trace::Write("info.PlayerUpdate.first", Trace::Args{}.Num("delta", a_delta), _ReturnAddress());
			}

			func(a_this, a_delta);
			MainThread::Drain();
			Hud::Tick();
			Preload::Tick();
			Detectors::Tick();
			InteriorSim::Tick();
			Probe::Tick();
			Trace::FlushIfStale();
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static inline bool                             _first{ true };
	};

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
	// picks up to 5 lights here (AE#107784). The portal keeps its lights away from effects (0.8.10-0.8.11:
	// with them, no fire through the portal at all).
	struct EffectGetRenderPasses
	{
		static RE::BSShaderProperty::RenderPassArray* thunk(RE::BSShaderProperty* a_this, RE::BSGeometry* a_geometry, std::uint32_t a_renderMode,
			RE::BSShaderAccumulator* a_accumulator)
		{
			const bool held = Portal::BeforeEffectPasses(a_geometry);
			auto*      passes = func(a_this, a_geometry, a_renderMode, a_accumulator);
			Portal::AfterEffectPasses(held, a_geometry, passes && passes->head);
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
			func(a_this, a_data);
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
			return func(a_actor, a_delta);
		}

		static inline decltype(&thunk) func;
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
			float      saved = 0.0f;
			const bool unscaled = Portal::BeforeEmission(a_this, saved);
			func(a_this, a_time, a_count, a_ages);
			if (unscaled) {
				Portal::AfterEmission(saved);
			}
		}

		static inline decltype(&thunk) func;
	};

	// A call instruction in the game's code redirected to a_thunk, after checking that it is one and
	// calls a_target. AE only (offsets from 1.6.1170's machine code).
	template <class Hook>
	bool WriteCall(std::string_view a_name, REL::ID a_function, std::ptrdiff_t a_offset, REL::ID a_target)
	{
		const auto  site = REL::Relocation<std::uintptr_t>{ a_function }.address() + a_offset;
		const auto  target = REL::Relocation<std::uintptr_t>{ a_target }.address();
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(site);
		const auto  called = site + 5 + *reinterpret_cast<const std::int32_t*>(site + 1);
		if (bytes[0] != 0xE8 || called != target) {
			logs::error("call hook {}: no call to AE#{} at AE#{}+0x{:X}", a_name, a_target.id(), a_function.id(), a_offset);
			return false;
		}
		Hook::func = SKSE::GetTrampoline().write_call<5>(site, Hook::thunk);
		return true;
	}

	void InstallCalls()
	{
		if (!REL::Module::IsAE()) {
			return;
		}
		const bool lod = WriteCall<CharControllerLodDistance>("Actor::Update -> CharController LOD distance", REL::ID(37348), 0x1F3, REL::ID(78274));
		const bool fade = WriteCall<FadeNodeDistance>("animated reference fade -> fade node distance", REL::ID(19384), 0xE4, REL::ID(105486));
		logs::info("call hooks installed: controller LOD {}, fade {}", lod, fade);
	}

	template <class Hook>
	void WriteVfunc(REL::Relocation<std::uintptr_t>& a_vtbl, std::size_t a_index)
	{
		Hook::func = a_vtbl.write_vfunc(a_index, Hook::thunk);
	}

	template <class Hook>
	LONG AttachDetour(std::uintptr_t a_address)
	{
		Hook::func = reinterpret_cast<decltype(Hook::func)>(a_address);
		return DetourAttach(reinterpret_cast<PVOID*>(&Hook::func), reinterpret_cast<PVOID>(&Hook::thunk));
	}

	template <class Hook>
	LONG AttachDetour(REL::RelocationID a_id)
	{
		return AttachDetour<Hook>(REL::Relocation<std::uintptr_t>{ a_id }.address());
	}

	void InstallDetours()
	{
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());

		std::vector<std::pair<std::string_view, LONG>> results{
			{ "TES::SetCurrentCell", AttachDetour<SetCurrentCell>(RELOCATION_ID(13171, 13316)) },
			{ "TES::PurgeBufferedCells", AttachDetour<PurgeBufferedCells>(RELOCATION_ID(13159, 13299)) },
			{ "ExteriorCellLoader::QueueCellLoad", AttachDetour<QueueCellLoad>(RELOCATION_ID(18150, 18541)) },
			{ "PlayerCharacter::CenterOnCell_Impl", AttachDetour<CenterOnCell>(RELOCATION_ID(39365, 40437)) },
			{ "TESObjectREFR::MoveTo_Impl", AttachDetour<MoveTo>(RELOCATION_ID(56227, 56626)) },
		};
		if (REL::Module::IsAE()) {
			results.emplace_back("hkpWorld::stepDeltaTime", AttachDetour<HavokStep>(REL::Relocation<std::uintptr_t>{ REL::ID(61410) }.address()));
			results.emplace_back("TES::ProcessEvent(PositionPlayerEvent)", AttachDetour<TesPositionPlayer>(REL::Relocation<std::uintptr_t>{ REL::ID(13419) }.address()));
			results.emplace_back("bhkWorld::vfunc[0x32]", AttachDetour<BhkWorldStep>(REL::Relocation<std::uintptr_t>{ REL::ID(77851) }.address()));
			results.emplace_back("TES::UnloadBufferedInterior", AttachDetour<TesUnloadBufferedInterior>(REL::Relocation<std::uintptr_t>{ REL::ID(13370) }.address()));
			results.emplace_back("TES::BufferInterior", AttachDetour<TesBufferInterior>(REL::Relocation<std::uintptr_t>{ REL::ID(13434) }.address()));
			results.emplace_back("ExteriorCellLoader::CancelCellLoad", AttachDetour<LoaderCancel>(REL::Relocation<std::uintptr_t>{ REL::ID(18547) }.address()));
			results.emplace_back("UnloadCell", AttachDetour<CellUnload>(REL::Relocation<std::uintptr_t>{ REL::ID(13721) }.address()));
			results.emplace_back("Main::RenderWorld", Portal::AttachRenderWorldDetour());
			results.emplace_back("Actor::ReducedProcessing", AttachDetour<ReducedProcessing>(REL::Relocation<std::uintptr_t>{ REL::ID(37277) }.address()));
			results.emplace_back("ProcessLists::SelectAnimatedActors", AttachDetour<SelectAnimatedActors>(REL::Relocation<std::uintptr_t>{ REL::ID(41466) }.address()));
			results.emplace_back("Actor::UpdateMovement", AttachDetour<MovementUpdate>(REL::Relocation<std::uintptr_t>{ REL::ID(37350) }.address()));
			results.emplace_back("TES::UpdateCells", AttachDetour<UpdateCells>(REL::Relocation<std::uintptr_t>{ REL::ID(13291) }.address()));
			results.emplace_back("TES::UpdateAnimatedRefs", AttachDetour<UpdateAnimatedRefs>(REL::Relocation<std::uintptr_t>{ REL::ID(13325) }.address()));
			results.emplace_back("ShadowSceneNode::SetPortalGraph", AttachDetour<SetPortalGraph>(REL::Relocation<std::uintptr_t>{ REL::ID(106385) }.address()));
			results.emplace_back("TESObjectCELL::Detach", AttachDetour<CellDetach>(REL::Relocation<std::uintptr_t>{ REL::ID(19130) }.address()));
			results.emplace_back("NiPSysEmitter::EmitParticles", AttachDetour<EmitParticles>(REL::Relocation<std::uintptr_t>{ REL::ID(74651) }.address()));
		}

		bool ok = true;
		for (const auto& [name, error] : results) {
			if (error != NO_ERROR) {
				logs::error("detour {} failed: {}", name, error);
				ok = false;
			}
		}
		if (!ok) {
			DetourTransactionAbort();
			logs::error("detours not installed");
			return;
		}

		if (const auto error = DetourTransactionCommit(); error != NO_ERROR) {
			logs::error("detour commit failed: {}", error);
			return;
		}
		logs::info("detours installed");
	}
}

namespace Hooks
{
	void Install()
	{
		REL::Relocation<std::uintptr_t> door{ RE::VTABLE_TESObjectDOOR[0] };
		WriteVfunc<DoorActivate>(door, 0x37);  // TESBoundObject::Activate

		REL::Relocation<std::uintptr_t> grid{ RE::VTABLE_GridCellArray[0] };
		WriteVfunc<GridDetachAll>(grid, 0x1);
		WriteVfunc<GridKillAll>(grid, 0x2);
		WriteVfunc<GridSetCenter>(grid, 0x3);
		WriteVfunc<GridShift>(grid, 0x4);
		WriteVfunc<GridDetach>(grid, 0x5);
		WriteVfunc<GridClearItem>(grid, 0x6);
		WriteVfunc<GridMoveItem>(grid, 0x7);
		WriteVfunc<GridSwapItem>(grid, 0x8);

		REL::Relocation<std::uintptr_t> task{ RE::VTABLE_CellLoaderTask[0] };
		WriteVfunc<TaskRun>(task, 0x1);
		WriteVfunc<TaskFinish>(task, 0x2);
		WriteVfunc<TaskCancel>(task, 0x3);
		WriteVfunc<TaskGetDescription>(task, 0x4);
		WriteVfunc<TaskPostProcess>(task, 0x6);

		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		WriteVfunc<PlayerUpdate>(player, 0xAD);  // Actor::Update

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

		REL::Relocation<std::uintptr_t> effectShader{ RE::VTABLE_BSEffectShader[0] };
		WriteVfunc<EffectSetupTechnique>(effectShader, 0x2);
		REL::Relocation<std::uintptr_t> effectProperty{ RE::VTABLE_BSEffectShaderProperty[0] };
		WriteVfunc<EffectGetRenderPasses>(effectProperty, 0x2A);
		REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[0] };  // the interior's NPCs; Actor's own vtable has the same function
		WriteVfunc<ModifyAnimationUpdateData>(character, 0x79);

		logs::info("vfunc hooks installed");

		InstallDetours();
		InstallCalls();
	}
}

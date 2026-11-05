#include "HooksCommon.h"

#include "Events.h"
#include "InteriorSim.h"
#include "MainThread.h"
#include "Preload.h"
#include "Settings.h"
#include "Snapshot.h"

namespace
{
	using Hooks::AddPlayerPos;
	using Hooks::HookScope;

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

			// 0.8.22-0.8.24: Idolaf came in through a door thousands of units away, within a second of the
			// player. Where he stood and who sent him, for every NPC at a teleport door.
			if (!byPlayer && a_activatorRef && a_targetRef) {
				std::array<void*, 16> frames{};
				const auto            count = ::RtlCaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
				std::string           stack;
				for (std::size_t i = 0; i < count; ++i) {
					stack += (stack.empty() ? "" : ",") + Trace::JsonString(Trace::DescribeAddress(reinterpret_cast<std::uintptr_t>(frames[i]), true));
				}
				args.Pos("activatorPos", a_activatorRef->GetPosition())
					.Pos("doorPos", a_targetRef->GetPosition())
					.Num("doorDistance", a_activatorRef->GetPosition().GetDistance(a_targetRef->GetPosition()))
					.Raw("stack", "[" + stack + "]");
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
}

namespace Hooks::Cells
{
	void InstallVfuncs()
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
	}
}

#pragma once

namespace Hooks
{
	// vfunc hooks: TESObjectDOOR::Activate, GridCellArray, CellLoaderTask, PlayerCharacter::Update.
	// Detours: TES::SetCurrentCell, TES::PurgeBufferedCells, ExteriorCellLoader::QueueCellLoad,
	// PlayerCharacter::CenterOnCell_Impl, TESObjectREFR::MoveTo_Impl.
	void Install();

	// Every hook the portal relies on is in. Without AE#19130's detour an attached interior crashes the
	// game when it leaves (0.8.17), so nothing attaches unless this is true.
	[[nodiscard]] bool Ready();
}

#pragma once

namespace Hooks
{
	// vfunc hooks: TESObjectDOOR::Activate, GridCellArray, CellLoaderTask, PlayerCharacter::Update.
	// Detours: TES::SetCurrentCell, TES::PurgeBufferedCells, ExteriorCellLoader::QueueCellLoad,
	// PlayerCharacter::CenterOnCell_Impl, TESObjectREFR::MoveTo_Impl.
	void Install();
}

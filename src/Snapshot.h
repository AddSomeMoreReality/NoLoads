#pragma once

namespace Snapshot
{
	// Writes a "snapshot" trace record with TES, grid, loader and player state.
	// Reads game state, so off the main thread it is deferred to the next PlayerCharacter::Update tick.
	void Take(std::string_view a_reason);
}

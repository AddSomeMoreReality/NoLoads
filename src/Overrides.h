#pragma once

// Experiment: switch game settings in memory (Skyrim.ini is not touched) to see which of them
// make a transition faster: fWarmStartMaxTime (kPost -> kFinish), the door fade-out and the fade-in
// after the loading screen. [Overrides] sSet1..sSet9 are lists "name:Section=value | ...";
// the hotkey cycles: game values -> set 1 -> set 2 -> ... -> game values.
namespace Overrides
{
	// Main thread, after the data is loaded: parses the sets and remembers the game values.
	void Init();

	// Main thread: applies the next set and shows it on the HUD.
	void Cycle();

	// 0 = game values, otherwise the 1-based number of the active set.
	[[nodiscard]] std::uint32_t Active();
}

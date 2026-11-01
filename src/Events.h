#pragma once

namespace Events
{
	// Needs the player and the event source holders, so call at kDataLoaded or later.
	void Register();

	// Any thread: the player activated a load door. Starts the "door -> picture" timeline
	// (wait -> fade-out -> loading screen -> fade-in) reported when the fade-in ends.
	void OnPlayerDoorActivated();

	// Main thread, every PlayerCharacter::Update: traces "frame.hitch" for long frames in the first
	// seconds after a loading screen (the autosave after a door runs there).
	void OnPlayerTick();
}

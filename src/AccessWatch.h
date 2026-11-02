#pragma once

// Who reads the player's position, and from where? A static scan (tools/re/scan_player_access.py) only
// sees code that loads the player itself; much of the game gets the player through an argument or a
// stored pointer. This capture asks the processor instead. The hotkey sets hardware breakpoints in every
// thread, on four addresses: the player's position, the player's 3D, the world camera and the player
// camera. For [AccessWatch] fSeconds every instruction that reads or writes them is counted, with its
// callers, and <log directory>\<plugin>.access-<n>.txt lists them, most frequent first. Threads
// created during a capture are not watched, and the game may stutter while it runs.
namespace AccessWatch
{
	// Once, after the log directory is known.
	void Init(const std::filesystem::path& a_directory);

	// Main thread (hotkey): starts a capture, unless one is running or [AccessWatch] bEnabled=0.
	void Start();

	// Main thread, every PlayerCharacter::Update: ends a capture that has run its time and writes the report.
	void Tick();
}

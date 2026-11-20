#pragma once

// Simulation probe (H11, H12): which cells are rendered, physics-simulated and AI-processed
// relative to the player. Every [Probe] fIntervalSeconds and after each transition it traces "probe":
// - cells: the player's cell, the exterior grid, interiors behind load doors in the loaded area and
//   interiors in TES::interiorBuffer, each with its state, whether its 3D is in the scene graph
//   (rendering) and its Havok world (physics);
// - actors from ProcessLists (high / middle-high / middle-low / low) with cell, position and AI package;
// - Havok worlds stepped since the previous probe (hkpWorld::stepDeltaTime counts).
namespace Probe
{
	// Main thread, every PlayerCharacter::Update.
	void Tick();

	// Thread-safe: probe on the next tick (e.g. right after a transition or on the hotkey).
	void Request(std::string_view a_reason);

	// The audio thread, from the sound position hook: remembers the sounds playing, for the probe's "sounds".
	// 0.8.23: the bard played and sang in silence, with the player inside.
	void NoteSound(const RE::BSGameSound* a_sound, const RE::NiPoint3& a_position, bool a_carried);
}

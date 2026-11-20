#pragma once

// Experiment (requested 23.10.2026): run the kept interior ([Preload] sInteriorCell) while the player
// is in the exterior as fully as the exterior cells around the player. A hotkey cycles the stages:
//   0 - off: the game's own handling (kUnattached, physics frozen, NPCs at middle-high, not rendered);
//   1 - its Havok world is stepped right after the player's world, with the same time step;
//   2 - plus its actors are moved to high processing (Actor::MoveToHigh) every second;
//   3 - plus its 3D is put into the exterior scene, turned so that its door lies on the exterior door;
//   4 - the same, lifted [InteriorSim] fLiftHeight above, to see it from the street;
//   5 - a clone of its 3D, lifted (0.8.1 showed nothing with the cell's own nodes).
// The 3D hangs under the root node of the exterior cell the door is in (0.8.4: nothing added under
// ObjectLODRoot is drawn). Stages 3..5 also put two copies of the exterior door on the street in
// front of it as controls (in the cell's node and in our container), and hide the exterior door and
// what stands behind it in the doorway ([InteriorSim] bHideExteriorDoor). Hooks on OnVisible count
// how often the culling process reaches each hung object. The 3D is taken out of the scene before
// every loading screen, before its exterior cell detaches and whenever the interior changes state.
namespace InteriorSim
{
	// Main thread, every PlayerCharacter::Update.
	void Tick();

	// Main thread: next stage (hotkey).
	void Cycle();

	// Takes the interior's 3D out of the exterior scene (queued to the main thread from elsewhere).
	void Detach(std::string_view a_why);

	// GridCellArray hooks, before the game detaches or clears a_cell: takes the 3D out when it hangs
	// in that cell's nodes, which the game may free with the cell.
	void CellDetaching(const RE::TESObjectCELL* a_cell, std::string_view a_where);

	using StepFn = std::uint64_t (*)(RE::bhkWorld*, std::int32_t);

	// bhkWorld::vfunc[0x32] (AE#77851) detour, after the game stepped a_world: steps the kept
	// interior's world with a_step (the original function) when a_world is the player's world.
	// On 1.6.1170 this runs on the physics thread, not the main one (0.8.0: every step was off-main),
	// so the interior step happens there, right after the player's world, with the same globals.
	void AfterWorldStep(RE::bhkWorld* a_world, std::int32_t a_flag, StepFn a_step);

	// NiAVObject::OnVisible (vfunc 0x34) hooks, any thread: counts the call when a_object is watched.
	void NoteVisible(const void* a_object);

	// Main thread, Loading Menu open / close: no interior steps while the game moves the player.
	void SetTransition(bool a_active);
	[[nodiscard]] bool InTransition();

	// The main thread holds this around the game's calls that attach, buffer or unload interiors;
	// the physics thread only try-locks it around the interior step, so it never waits on it.
	[[nodiscard]] std::unique_lock<std::recursive_mutex> GuardWorlds();
}

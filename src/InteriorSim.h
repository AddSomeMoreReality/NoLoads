#pragma once

// Experiment (requested 23.10.2026): run the kept interior ([Preload] sInteriorCell) while the player
// is in the exterior as fully as the exterior cells around the player. A hotkey switches the stages:
//   0 - off: the game's own handling (kUnattached, physics frozen, NPCs at middle-high, not rendered);
//   1 - the portal: its Havok world is stepped right after the player's world, with the same time step,
//       its actors are moved to high processing (Actor::MoveToHigh) every second, and Portal draws it
//       through its exterior door, which is hidden (with the interior's own door).
// Stages 1-5 of 0.8.0-0.8.11 (physics only, NPC high, its 3D in the exterior scene at its door, lifted,
// a copy) left the cycle on 25.10.2026: docs/INTERIOR_SIM_STAGES.md. Hooks on OnVisible count how
// often the culling process reaches the quad and the interior's nodes. The portal stops before every
// loading screen, before the exterior door's cell detaches and whenever the interior changes state.
// 0.8.16 ([InteriorSim] bAttachInterior): with the portal, the interior is attached the way
// TES::SetCurrentCell (AE#13316) attaches an interior: AE#18976 (its 3D under a node of ours in no
// scene, as of 0.8.17; its Havok world active), then AE#19063 (its references, kAttached). The game then treats it as a loaded cell
// for whatever tests the cell's state (animation jobs, AE#41466); the two per-frame walks over the
// loaded cells, which take the player's interior or the grid, get it added: AE#13291 (the references'
// controllers, AE#18890) and AE#13325 (their animation graphs, AE#18899/18900). It is detached as
// AE#13317 detaches an interior the player leaves (AE#19130, AE#19046) before every loading screen,
// cell change, buffer purge and save.
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

	// The same detour, before the game steps a_world: counts the game's own steps of the interior's
	// world (once attached, the game might step it itself; ours is then skipped).
	void NoteWorldStep(const RE::bhkWorld* a_world);

	// Any thread: a_cell is the interior InteriorSim attached (or is attaching, detaching): its state is
	// ours, not the game's.
	[[nodiscard]] bool OwnsAttachedCell(const RE::TESObjectCELL* a_cell);

	// Detour of AE#13291 (TES: the loaded cells' per-frame update, its time added to AE#400333), after
	// the original, on the thread the game runs it: the attached interior's references as a grid cell's (AE#18890).
	void AfterCellsUpdate();

	// Detour of AE#13325 (TES: the animation graphs of the loaded cells' animated references), after
	// the original: the attached interior's too (AE#18899 into a_jobs, or AE#18900 at once without).
	void AfterAnimatedRefs(void* a_jobs);

	// Detour of AE#19130 (the cell detach), before the original: if a_cell's 3D hangs under our node,
	// it is taken off first. AE#19130 detaches it from TES::objRoot by the child's own index (AE#70292
	// does not check the parent): under our node that index belongs to another child of objRoot.
	void BeforeCellDetach(const RE::TESObjectCELL* a_cell);

	// Detour of AE#106385 (ShadowSceneNode::SetPortalGraph): true while AE#18976 attaches the interior.
	// It would give the world's scene the interior's rooms and portals, as when the player enters it.
	[[nodiscard]] bool KeepPortalGraph();

	// NiAVObject::OnVisible (vfunc 0x34) hooks, any thread: counts the call when a_object is watched.
	void NoteVisible(const void* a_object);

	// Main thread, Loading Menu open / close: no interior steps while the game moves the player.
	void SetTransition(bool a_active);
	[[nodiscard]] bool InTransition();

	// The main thread holds this around the game's calls that attach, buffer or unload interiors;
	// the physics thread only try-locks it around the interior step, so it never waits on it.
	[[nodiscard]] std::unique_lock<std::recursive_mutex> GuardWorlds();
}

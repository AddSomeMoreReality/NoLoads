#pragma once

// Detectors (requested 23.10.2026): load doors near the player: every reference with ExtraTeleport in
// the loaded cells (the player's interior, or the attached exterior grid), each instance with its
// position, rotation, destination cell, arrival marker and linked door. Traced as detector.doors when
// the nearest ones change; the nearest go to the HUD. The window detector (off since 0.8.4) is gone (T02).
// With [Detectors] bEnabled=0 (since 0.8.4) the doors are still scanned, silently: [Preload] and
// [InteriorSim] need them; the hotkey then reports only what the player looks at.
namespace Detectors
{
	struct Door
	{
		RE::ObjectRefHandle ref;
		RE::FormID          id{ 0 };
		RE::NiPoint3        pos;
		RE::NiPoint3        angle;  // radians, TESObjectREFR::data.angle
		RE::FormID          destCell{ 0 };
		bool                destInterior{ false };
		RE::ObjectRefHandle linked;  // the door on the other side
		float               distance{ 0.0f };
	};

	// Main thread, every PlayerCharacter::Update.
	void Tick();

	// Main thread: summary of the nearest doors on the HUD and in the trace (hotkey).
	void Report();

	// Main thread: the nearest door in the loaded cells that leads into a_cell.
	[[nodiscard]] std::optional<Door> NearestDoorTo(const RE::TESObjectCELL* a_cell);

	// World positions of the shape's vertices, from the renderer's CPU copy (static meshes keep one);
	// false when there is none.
	[[nodiscard]] bool WorldPoints(RE::BSGeometry* a_geometry, std::vector<RE::NiPoint3>& a_out);
}

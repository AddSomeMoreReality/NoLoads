#pragma once

// Detectors (requested 23.10.2026): load doors and windows near the player.
// - doors: every reference with ExtraTeleport in the loaded cells (the player's interior, or the
//   attached exterior grid), each instance with its position, rotation, destination cell, arrival
//   marker and linked door;
// - windows: geometry whose diffuse texture (or the model) path contains [Detectors] sWindowKeywords.
//   When the shape keeps its vertices in memory it is split into connected pieces (one piece is
//   about one window) with a world center, size and facing; otherwise the shape's bound is used.
// Traced as detector.doors / detector.windows when the nearest ones change; the nearest go to the HUD.
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

	// Main thread: summary of the nearest doors and windows on the HUD and in the trace (hotkey).
	void Report();

	// Main thread: the nearest door in the loaded cells that leads into a_cell.
	[[nodiscard]] std::optional<Door> NearestDoorTo(const RE::TESObjectCELL* a_cell);

	// The cells whose references are in the scene: the player's interior, or the attached exterior grid.
	[[nodiscard]] std::vector<RE::TESObjectCELL*> SceneCells();

	// File name (lower case) of the shape's diffuse texture, empty when it has none.
	[[nodiscard]] std::string TextureFile(RE::BSGeometry* a_geometry);

	// World positions of the shape's vertices, from the renderer's CPU copy (static meshes keep one);
	// false when there is none.
	[[nodiscard]] bool WorldPoints(RE::BSGeometry* a_geometry, std::vector<RE::NiPoint3>& a_out);

	// Main thread: Havok line-of-sight ray in the player's world from a_origin along a_direction (unit),
	// through references whose 3D is hidden; the first other reference hit and the hit point.
	[[nodiscard]] RE::NiPointer<RE::TESObjectREFR> PickRef(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_direction, RE::NiPoint3& a_point);
}

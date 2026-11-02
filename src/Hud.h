#pragma once

// Top-left HUD messages ([General] bHotkeyMessages): only answers to hotkeys, the mode switched to or
// the record written (0.8.25, at the user's request). Status, transitions and cell changes go to the
// trace only.
// The game shows its notifications one after another, so 0.8.0 (detectors and a load/unload loop
// posting several messages a second) built a backlog of minutes. Messages still wait in our own
// queue: one is passed to the game every kGap, and a newer message of the same channel replaces the
// waiting one.
namespace Hud
{
	// Any thread.
	void Show(std::string_view a_channel, std::string a_text);

	// Main thread, every PlayerCharacter::Update: passes at most one waiting message to the game.
	void Tick();

	// Cell label for messages: display name, else editor ID, plus (x,y) for exterior cells.
	[[nodiscard]] std::string CellLabel(RE::TESObjectCELL* a_cell);
}

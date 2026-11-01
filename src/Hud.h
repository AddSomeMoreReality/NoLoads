#pragma once

// Top-left HUD notifications ([General] bShowCellNotifications).
// The game shows its notifications one after another, so 0.8.0 (detectors and a load/unload loop
// posting several messages a second) built a backlog of minutes. Messages now wait in our own queue:
// one is passed to the game every kGap, and a newer message of the same channel replaces the
// waiting one.
namespace Hud
{
	// Any thread. a_urgent puts the message at the head of the queue (hotkey answers).
	void Show(std::string_view a_channel, std::string a_text, bool a_urgent = false);

	// Main thread, every PlayerCharacter::Update: passes at most one waiting message to the game.
	void Tick();

	// Cell label for messages: display name, else editor ID, plus (x,y) for exterior cells.
	[[nodiscard]] std::string CellLabel(RE::TESObjectCELL* a_cell);
}

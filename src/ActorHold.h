#pragma once

// T11 (0.8.27): the actors of cells a portal shows stay at high processing through transitions.
//
// The game picks an actor's process level in AE#37501: AE#37503 says which level the actor should have,
// and if that differs from AIProcess::processLevel, Actor vfunc 0xF1-0xF4 (MoveToHigh .. MoveToMiddleHigh)
// moves it. Both the detach of a cell (AE#19046 -> AE#41292 -> AE#41468) and the per-frame process
// lists update (AE#41294) go through there. Two side effects made the portal's actors jump (run 0.8.25):
//   - in middle-high their paths run "virtually" (MovementAgentPathFollowerVirtual, AE#92556): every path
//     point passed queues a warp to it (IMovementMessageInterface vfunc 1, AE#41761), applied by AE#41717;
//   - MoveToHigh (AE#37621) puts an actor that walks a path on its package's place (AE#37618, +0x348).
// The scene lines of an actor without AIProcess::high end at once (T10): the bard's song ran out.
//
// A portal registers the cells it shows here. For their actors that are high, AE#37503 answers "high"
// (the function has such a branch of its own at +0x143: keep the current level), and MoveToHigh skips
// AE#37618 for actors that were high in a held cell within the last seconds. Hold and release are
// per owner (one bit per portal), so several portals may share a cell (CLAUDE.md 5.14, 1). Cells are kept
// as FormIDs: exteriors as well as interiors (5.14, 2).
namespace ActorHold
{
	// Main thread. a_owner (0..31) holds or releases a_cell; a cell stays held while any owner holds it.
	void Hold(std::uint32_t a_owner, RE::FormID a_cell, bool a_held, std::string_view a_why);

	// Main thread: no cell held, nothing remembered (a game is loaded).
	void Clear(std::string_view a_why);

	// Any thread, lock-free: some portal holds the cell.
	[[nodiscard]] bool HoldsCell(RE::FormID a_cell);
	[[nodiscard]] bool HoldsActor(const RE::Actor* a_actor);

	// Any thread: a_actor's cell is held or is the watched cell (the Preload interior): its actors are traced.
	[[nodiscard]] bool InScope(const RE::Actor* a_actor);

	// Main thread: the cell whose actors are traced even while it is not held ([Preload] sInteriorCell).
	void SetWatchedCell(RE::FormID a_cell);

	// Main thread: a_actor is high in a held cell now (the snap in MoveToHigh is skipped for a while).
	void NoteHigh(const RE::Actor* a_actor);

	// Any thread: Loading Menu open (true) or close (false), for the records' timing.
	void NoteTransition(bool a_open);

	// Milliseconds since the last Loading Menu open / close, -1 if none yet.
	[[nodiscard]] std::int64_t MsSinceOpen();
	[[nodiscard]] std::int64_t MsSinceClose();

	// Installs the hooks (AE only): AE#37503, AE#37621+0x348, Character vfunc 0xF1-0xF4 and
	// MovementControllerNPC's IMovementMessageInterface vfunc 1.
	void Install();

	// JSON object for interior.sim status: held cells and counters.
	[[nodiscard]] std::string StatusJson();
}

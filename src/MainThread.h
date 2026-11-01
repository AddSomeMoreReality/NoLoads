#pragma once

// Deferred work for the main thread, run from the PlayerCharacter::Update tick.
// Used instead of SKSE::GetTaskInterface()->AddTask: input events arrive on a non-main thread,
// and a task that re-queued itself from the SKSE queue hung the game (run 0.3.0).
namespace MainThread
{
	// Thread-safe.
	void Post(std::function<void()> a_task);

	// Main thread only. Tasks posted while draining run on the next tick.
	void Drain();
}

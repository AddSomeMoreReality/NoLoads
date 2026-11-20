#pragma once

// Experiment: keep one interior cell loaded so entering it is near-instant (the kept exterior area,
// [Preload] sExteriorCell, is gone since T02):
// - the game's own bPreloadLinkedInteriors is switched on in memory (doors near the player),
//   and [Preload] bKeepInterior calls ExteriorCellLoader::QueueInteriorCellLoad (AE#18540) for
//   sInteriorCell every fRefreshSeconds: it queues the load when the cell is unloaded and moves it to
//   the front of TES::interiorBuffer when it is buffered, so other preloaded interiors do not evict it;
// - every state change of a kept cell is traced as preload.change with the place it was seen,
//   to find what unloads them, and shown on the HUD for the interior.
namespace Preload
{
	// Main thread, every PlayerCharacter::Update. Resolves the cells on the first call.
	void Tick();

	// Main thread only (no-op elsewhere). Traces the kept interior cell if its state changed.
	void CheckStates(std::string_view a_where);

	// The kept interior ([Preload] sInteriorCell), or null. Interiors are permanent forms.
	[[nodiscard]] RE::TESObjectCELL* Interior();

	// Any thread, from the hooks on the bPreloadLinkedInteriors manager (AE#40845): true when the
	// game is about to unload (AE#13370) or cancel the queued load of (AE#18547) the kept interior
	// while the player is not in it; the hook then skips the call. 0.8.0 showed the manager unloads
	// every interior whose door is not among the nearest ones on the next frame.
	[[nodiscard]] bool BlockUnload(RE::TESObjectCELL* a_cell);
	[[nodiscard]] bool BlockCancel(RE::FormID a_cell);
	[[nodiscard]] std::uint64_t BlockedUnloads();

	// Any thread, from the hook on the cell unload itself (AE#13721): 0.8.1 showed the kept interior
	// is also pushed out of the 3-slot TES::interiorBuffer by the next interior the game preloads
	// (AE#13434 -> AE#13721), from worker threads. Blocked while it is kept and no loading screen is
	// open; the first call from every caller is traced.
	[[nodiscard]] bool BlockCellUnload(RE::TESObjectCELL* a_cell, const void* a_caller);
}

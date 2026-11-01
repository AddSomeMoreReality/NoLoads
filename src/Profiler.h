#pragma once

// Samples the main thread while the Loading Menu is open: its instruction pointer plus return
// addresses found on its stack, about once per millisecond. The report is traced as "profile",
// grouped by transition phase, to see where loading screen time goes besides the cell load itself.
namespace Profiler
{
	enum class Phase : std::uint8_t
	{
		kOpenToPre,      // Loading Menu open -> PositionPlayerEvent kPre
		kPreToEnter,     // kPre -> ActorCell enter: the cell load
		kEnterToPost,    // ActorCell enter -> kPost
		kPostToFinish,   // kPost -> kFinish
		kFinishToClose,  // kFinish -> Loading Menu close
		kTotal
	};

	inline constexpr std::size_t kStackBufferSize = 64 * 1024;
	inline constexpr std::size_t kMaxStackFrames = 64;

	// Any thread but a_thread itself: suspends it for a moment, copies its stack into a_stack
	// (kStackBufferSize bytes) and walks the copy. a_frames has room for kMaxStackFrames addresses,
	// innermost first. Returns how many were found.
	[[nodiscard]] std::uint8_t CaptureStack(void* a_thread, std::uint8_t* a_stack, std::uintptr_t* a_frames);

	// Main thread, on Loading Menu open. Ignored while a previous report is still being written.
	void Start();
	void SetPhase(Phase a_phase);
	// On Loading Menu close. The sampler thread writes the report and exits.
	void Stop();
}

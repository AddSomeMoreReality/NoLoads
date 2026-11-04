#pragma once

// 0.8.21 froze the game a few seconds after the portal started: no crash log, nothing in the trace
// after the last flush, and the process had to be taken down by restarting Windows. The watchdog
// thread sees such a freeze from outside. While the portal runs, and for 10 s after F11 (0.8.28 froze in
// F11's snapshots with the portal off), no PlayerCharacter::Update for
// [Watchdog] fHangSeconds and a main thread that answers no window message count as a hang. It then
// writes every thread's stack and CPU use to <log directory>\<plugin>.hang.txt and, with
// [Watchdog] bTerminate=1, ends the process. A game paused in a menu still answers messages.
namespace Watchdog
{
	// Once, after the log directory is known. Does nothing with [Watchdog] bEnabled=0.
	void Start(const std::filesystem::path& a_directory);
}

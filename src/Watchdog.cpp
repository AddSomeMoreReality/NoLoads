#include "Watchdog.h"

#include "Portal.h"
#include "Profiler.h"
#include "Settings.h"
#include "Trace.h"

#include <TlHelp32.h>
#include <share.h>

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr auto         kPoll = std::chrono::milliseconds(500);
	constexpr auto         kCpuWindow = std::chrono::seconds(1);  // a thread busy through it is spinning, not waiting
	constexpr auto         kDumpDeadline = std::chrono::seconds(15);
	constexpr std::uint32_t kMessageTimeoutMs = 2000;
	constexpr UINT         kExitCode = 0xC1A0;
	constexpr std::size_t  kMaxThreads = 256;

	struct ThreadDump
	{
		DWORD          id{ 0 };
		HANDLE         handle{ nullptr };
		std::uint64_t  cpuBefore{ 0 };  // 100 ns
		std::uint64_t  cpuAfter{ 0 };
		std::uint8_t   frameCount{ 0 };
		std::uintptr_t frames[Profiler::kMaxStackFrames]{};
	};

	std::filesystem::path             g_path;
	std::array<ThreadDump, kMaxThreads> g_threads;
	std::size_t                       g_threadCount{ 0 };
	std::uint8_t                      g_stack[Profiler::kStackBufferSize];
	std::atomic<bool>                 g_dumped{ false };

	std::uint64_t CpuTime(HANDLE a_thread)
	{
		FILETIME created{}, exited{}, kernel{}, user{};
		if (!::GetThreadTimes(a_thread, &created, &exited, &kernel, &user)) {
			return 0;
		}
		const auto ticks = [](const FILETIME& a_time) { return (static_cast<std::uint64_t>(a_time.dwHighDateTime) << 32) | a_time.dwLowDateTime; };
		return ticks(kernel) + ticks(user);
	}

	void OpenThreads()
	{
		const auto self = ::GetCurrentThreadId();
		const auto process = ::GetCurrentProcessId();
		HANDLE     snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE) {
			return;
		}
		THREADENTRY32 entry{};
		entry.dwSize = sizeof(entry);
		for (auto more = ::Thread32First(snapshot, &entry); more && g_threadCount < kMaxThreads; more = ::Thread32Next(snapshot, &entry)) {
			if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) {
				continue;
			}
			if (HANDLE thread = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID)) {
				g_threads[g_threadCount++] = { .id = entry.th32ThreadID, .handle = thread };
			}
		}
		::CloseHandle(snapshot);
	}

	void WriteThread(std::FILE* a_file, const ThreadDump& a_thread, DWORD a_mainThread)
	{
		const auto cpuMs = (a_thread.cpuAfter - a_thread.cpuBefore) / 10'000;
		const auto head = std::format("thread {}{}: CPU {} ms of {} ms\n", a_thread.id, a_thread.id == a_mainThread ? " (main)" : "", cpuMs,
			std::chrono::duration_cast<std::chrono::milliseconds>(kCpuWindow).count());
		std::fwrite(head.data(), 1, head.size(), a_file);
		for (std::uint8_t i = 0; i < a_thread.frameCount; ++i) {
			const auto line = std::format("  {:#018x} {}\n", a_thread.frames[i], Trace::DescribeAddress(a_thread.frames[i], true));
			std::fwrite(line.data(), 1, line.size(), a_file);
		}
		std::fflush(a_file);
	}

	// On a thread of its own: anything here may block on a lock the hung thread holds, and the
	// watchdog ends the process after kDumpDeadline whether this has finished or not.
	void Dump()
	{
		OpenThreads();
		for (std::size_t i = 0; i < g_threadCount; ++i) {
			g_threads[i].cpuBefore = CpuTime(g_threads[i].handle);
		}
		std::this_thread::sleep_for(kCpuWindow);
		for (std::size_t i = 0; i < g_threadCount; ++i) {
			auto& thread = g_threads[i];
			thread.cpuAfter = CpuTime(thread.handle);
			thread.frameCount = Profiler::CaptureStack(thread.handle, g_stack, thread.frames);
		}

		const auto* main = RE::Main::GetSingleton();
		const auto  mainThread = main ? static_cast<DWORD>(main->threadID) : 0;
		// The hung thread first, then whatever kept the CPU busy.
		std::sort(g_threads.begin(), g_threads.begin() + static_cast<std::ptrdiff_t>(g_threadCount), [&](const ThreadDump& a_lhs, const ThreadDump& a_rhs) {
			if ((a_lhs.id == mainThread) != (a_rhs.id == mainThread)) {
				return a_lhs.id == mainThread;
			}
			return a_lhs.cpuAfter - a_lhs.cpuBefore > a_rhs.cpuAfter - a_rhs.cpuBefore;
		});

		if (std::FILE* file = _wfsopen(g_path.c_str(), L"wb", _SH_DENYWR)) {
			const auto head = std::format("hang: t_us={} frame={} portal pass phase={} threads={}\n\n", Trace::NowUs(), Trace::Frame(), Portal::PassPhaseName(), g_threadCount);
			std::fwrite(head.data(), 1, head.size(), file);
			for (std::size_t i = 0; i < g_threadCount; ++i) {
				WriteThread(file, g_threads[i], mainThread);
			}
			std::fclose(file);
		}
		logs::error("hang: main thread stopped, phase {}; stacks in {}", Portal::PassPhaseName(), g_path.string());
		Trace::Write("watchdog.hang", Trace::Args{}.Str("phase", Portal::PassPhaseName()).Int("threads", static_cast<std::int64_t>(g_threadCount)), nullptr, true);
		g_dumped = true;
	}

	// True unless the window's thread takes messages no more. A game paused in a menu still does.
	bool MainThreadAnswers()
	{
		const auto* main = RE::Main::GetSingleton();
		const auto  window = main ? reinterpret_cast<HWND>(main->wnd) : nullptr;
		DWORD_PTR   result = 0;
		return !window || ::SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, kMessageTimeoutMs, &result) != 0;
	}

	void OnHang()
	{
		std::thread(Dump).detach();
		const auto deadline = Clock::now() + kDumpDeadline;
		while (!g_dumped.load() && Clock::now() < deadline) {
			std::this_thread::sleep_for(kPoll);
		}
		if (Settings::Get().watchdogTerminate) {
			::TerminateProcess(::GetCurrentProcess(), kExitCode);
		}
	}

	void Run()
	{
		const auto limit = std::chrono::duration<float>(Settings::Get().watchdogHangSeconds);
		auto       lastFrame = Trace::Frame();
		auto       lastAdvance = Clock::now();
		for (;;) {
			std::this_thread::sleep_for(kPoll);
			const auto frame = Trace::Frame();
			const auto now = Clock::now();
			if (frame != lastFrame || !Portal::Active()) {
				lastFrame = frame;
				lastAdvance = now;
			} else if (now - lastAdvance >= limit) {
				if (!MainThreadAnswers()) {
					OnHang();
					return;
				}
				lastAdvance = now;
			}
		}
	}
}

namespace Watchdog
{
	void Start(const std::filesystem::path& a_directory)
	{
		if (!Settings::Get().watchdogEnabled) {
			return;
		}
		g_path = a_directory / std::format("{}.hang.txt", SKSE::GetPluginName());
		std::thread(Run).detach();
	}
}

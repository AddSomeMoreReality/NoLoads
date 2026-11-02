#include "AccessWatch.h"

#include "Hud.h"
#include "Settings.h"
#include "Trace.h"

#include <TlHelp32.h>

namespace
{
	constexpr std::size_t kSlots = 4;  // DR0-DR3
	constexpr std::size_t kSites = std::size_t{ 1 } << 14;
	constexpr std::size_t kCallers = 3;
	constexpr std::size_t kReported = 60;  // functions per target in the trace; the file has them all

	constexpr std::uint64_t kDebugStatusHits = 0xF;  // DR6: which of DR0-DR3 fired
	constexpr std::uint64_t kReadOrWrite = 3;        // DR7 RW field
	constexpr std::uint64_t kFourBytes = 3;          // DR7 LEN field

	// One instruction that touched a watched address. Claimed once by a compare-exchange on rip, on
	// whatever thread hit it; the exception handler allocates nothing and takes no lock.
	struct Site
	{
		std::atomic<std::uintptr_t>              rip{ 0 };
		std::array<std::atomic<std::uint64_t>, kSlots> hits{};
		std::array<std::uintptr_t, kCallers>     callers{};
	};

	struct Target
	{
		std::string_view name;
		std::uintptr_t   address{ 0 };
	};

	std::array<Site, kSites>   g_sites{};
	std::array<Target, kSlots> g_targets{};
	std::atomic<std::uint64_t> g_hits{ 0 };
	std::atomic<std::uint64_t> g_lost{ 0 };
	std::atomic<bool>          g_capturing{ false };
	PVOID                      g_handler{ nullptr };
	std::filesystem::path      g_directory;
	std::int64_t               g_startUs{ 0 };
	std::int64_t               g_endUs{ 0 };
	std::uint32_t              g_threads{ 0 };
	std::uint32_t              g_number{ 0 };

	void Callers(CONTEXT a_context, std::array<std::uintptr_t, kCallers>& a_out)
	{
		for (auto& caller : a_out) {
			DWORD64 imageBase = 0;
			auto*   entry = ::RtlLookupFunctionEntry(a_context.Rip, &imageBase, nullptr);
			if (!entry) {
				return;
			}
			PVOID   handlerData = nullptr;
			DWORD64 frame = 0;
			::RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, a_context.Rip, entry, &a_context, &handlerData, &frame, nullptr);
			if (!a_context.Rip) {
				return;
			}
			caller = a_context.Rip;
		}
	}

	void Record(const CONTEXT& a_context, std::uint64_t a_fired)
	{
		const auto rip = static_cast<std::uintptr_t>(a_context.Rip);
		auto       index = (rip * 0x9E3779B97F4A7C15ull) >> 50;
		for (std::size_t probe = 0; probe < 64; ++probe, ++index) {
			auto&          site = g_sites[index % kSites];
			std::uintptr_t held = site.rip.load(std::memory_order_acquire);
			if (held == 0 && site.rip.compare_exchange_strong(held, rip, std::memory_order_acq_rel)) {
				Callers(a_context, site.callers);
				held = rip;
			}
			if (held == rip) {
				for (std::size_t slot = 0; slot < kSlots; ++slot) {
					if ((a_fired & (std::uint64_t{ 1 } << slot)) != 0) {
						site.hits[slot].fetch_add(1, std::memory_order_relaxed);
					}
				}
				g_hits.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
		g_lost.fetch_add(1, std::memory_order_relaxed);
	}

	// A data breakpoint traps after the instruction, so going on needs nothing but clearing the status.
	// The handler stays installed after a capture: a trap may still be on its way on another thread.
	LONG CALLBACK OnException(EXCEPTION_POINTERS* a_info)
	{
		if (a_info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		auto*      context = a_info->ContextRecord;
		const auto fired = context->Dr6 & kDebugStatusHits;
		if (fired == 0) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		if (g_capturing.load(std::memory_order_relaxed)) {
			Record(*context, fired);
		}
		context->Dr6 &= ~kDebugStatusHits;
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	void SetDebugRegisters(CONTEXT& a_context, bool a_on)
	{
		std::uint64_t control = 0;
		std::array<std::uintptr_t, kSlots> addresses{};
		if (a_on) {
			for (std::size_t slot = 0; slot < kSlots; ++slot) {
				if (g_targets[slot].address == 0) {
					continue;
				}
				addresses[slot] = g_targets[slot].address;
				control |= std::uint64_t{ 1 } << (slot * 2);                // local enable
				control |= kReadOrWrite << (16 + slot * 4);
				control |= kFourBytes << (18 + slot * 4);
			}
		}
		a_context.Dr0 = addresses[0];
		a_context.Dr1 = addresses[1];
		a_context.Dr2 = addresses[2];
		a_context.Dr3 = addresses[3];
		a_context.Dr6 = 0;
		a_context.Dr7 = control;
	}

	// Runs on a thread of its own: SetThreadContext does not work on the calling thread, and the main
	// thread must get the breakpoints too. The list is made first: nothing allocates while a thread is
	// suspended, it could hold the heap's lock.
	std::uint32_t ApplyToThreads(bool a_on)
	{
		std::vector<DWORD> ids;
		const auto         snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE) {
			return 0;
		}
		THREADENTRY32 entry{};
		entry.dwSize = sizeof(entry);
		if (::Thread32First(snapshot, &entry)) {
			do {
				if (entry.th32OwnerProcessID == ::GetCurrentProcessId() && entry.th32ThreadID != ::GetCurrentThreadId()) {
					ids.push_back(entry.th32ThreadID);
				}
			} while (::Thread32Next(snapshot, &entry));
		}
		::CloseHandle(snapshot);

		std::uint32_t done = 0;
		for (const auto id : ids) {
			const auto thread = ::OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, id);
			if (!thread) {
				continue;
			}
			if (::SuspendThread(thread) != static_cast<DWORD>(-1)) {
				CONTEXT context{};
				context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
				if (::GetThreadContext(thread, &context)) {
					SetDebugRegisters(context, a_on);
					done += ::SetThreadContext(thread, &context) ? 1 : 0;
				}
				::ResumeThread(thread);
			}
			::CloseHandle(thread);
		}
		return done;
	}

	std::uintptr_t PositionOf(const RE::NiAVObject* a_object)
	{
		return a_object ? reinterpret_cast<std::uintptr_t>(&a_object->world.translate.x) : 0;
	}

	bool FindTargets()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return false;
		}
		const auto* playerCamera = RE::PlayerCamera::GetSingleton();
		g_targets = { {
			{ "player.location", reinterpret_cast<std::uintptr_t>(&player->data.location.x) },
			{ "player.3D", PositionOf(player->Get3D()) },
			{ "worldCamera", PositionOf(RE::Main::WorldRootCamera()) },
			{ "playerCamera", PositionOf(playerCamera ? playerCamera->cameraRoot.get() : nullptr) },
		} };
		return true;
	}

	struct Row
	{
		std::uint64_t  hits{ 0 };
		std::uintptr_t rip{ 0 };
		std::array<std::uintptr_t, kCallers> callers{};
	};

	std::string Line(const Row& a_row)
	{
		auto text = std::format("{:>10}  {}", a_row.hits, Trace::DescribeAddress(a_row.rip, true));
		for (const auto caller : a_row.callers) {
			if (caller) {
				text += " <- " + Trace::DescribeAddress(caller, true);
			}
		}
		return text;
	}

	void Report()
	{
		const auto path = g_directory / std::format("{}.access-{}.txt", SKSE::GetPluginName(), ++g_number);
		std::ofstream file{ path };
		auto*         player = RE::PlayerCharacter::GetSingleton();
		auto*         cell = player ? player->GetParentCell() : nullptr;
		file << std::format("seconds {:.2f}, hits {}, lost {}, threads {}, player cell {}\n", static_cast<double>(g_endUs - g_startUs) / 1e6, g_hits.load(), g_lost.load(), g_threads, Trace::FormString(cell));

		std::string json;
		for (std::size_t slot = 0; slot < kSlots; ++slot) {
			std::vector<Row> rows;
			for (auto& site : g_sites) {
				const auto rip = site.rip.load(std::memory_order_acquire);
				const auto hits = site.hits[slot].load(std::memory_order_relaxed);
				if (rip != 0 && hits != 0) {
					rows.push_back({ hits, rip, site.callers });
				}
			}
			std::ranges::sort(rows, std::greater{}, &Row::hits);
			file << std::format("\n{} at 0x{:X}: {} instructions\n", g_targets[slot].name, g_targets[slot].address, rows.size());
			std::string top;
			std::size_t written = 0;
			for (const auto& row : rows) {
				file << Line(row) << '\n';
				if (written++ < kReported) {
					top += (top.empty() ? "" : ",") + Trace::JsonString(Line(row));
				}
			}
			json += (json.empty() ? "" : ",") + Trace::Args{}.Str("target", g_targets[slot].name).Int("instructions", static_cast<std::int64_t>(rows.size())).Raw("top", "[" + top + "]").Json();
		}
		Trace::Write("access.watch", Trace::Args{}.Int("hits", static_cast<std::int64_t>(g_hits.load())).Int("lost", static_cast<std::int64_t>(g_lost.load())).Int("threads", g_threads).Cell("playerCell", cell).Str("file", path.filename().string()).Raw("targets", "[" + json + "]"), nullptr, true);
		Hud::Show("portal", std::format("CLR: access watch: {} hits, {}", g_hits.load(), path.filename().string()), true);
		logs::info("access watch: {} hits, {} lost, {}", g_hits.load(), g_lost.load(), path.filename().string());
	}

	void Clear()
	{
		for (auto& site : g_sites) {
			site.rip.store(0, std::memory_order_relaxed);
			for (auto& hits : site.hits) {
				hits.store(0, std::memory_order_relaxed);
			}
			site.callers = {};
		}
		g_hits = 0;
		g_lost = 0;
	}
}

namespace AccessWatch
{
	void Init(const std::filesystem::path& a_directory)
	{
		g_directory = a_directory;
	}

	void Start()
	{
		const auto& settings = Settings::Get();
		if (!settings.accessWatchEnabled || g_capturing.load() || g_startUs != 0 || !FindTargets()) {
			return;
		}
		if (!g_handler) {
			g_handler = ::AddVectoredExceptionHandler(1, OnException);
		}
		Clear();
		g_startUs = Trace::NowUs();
		g_endUs = 0;
		g_capturing = true;
		std::uint32_t threads = 0;
		std::thread{ [&threads]() { threads = ApplyToThreads(true); } }.join();
		g_threads = threads;
		Hud::Show("portal", std::format("CLR: access watch for {:.0f} s, {} threads", settings.accessWatchSeconds, threads), true);
	}

	void Tick()
	{
		if (g_startUs == 0) {
			return;
		}
		const auto& settings = Settings::Get();
		const auto  now = Trace::NowUs();
		if (now - g_startUs < static_cast<std::int64_t>(settings.accessWatchSeconds * 1e6f) && g_hits.load(std::memory_order_relaxed) < settings.accessWatchMaxHits) {
			return;
		}
		g_endUs = now;
		std::thread{ []() { (void)ApplyToThreads(false); } }.join();
		g_capturing = false;
		Report();
		g_startUs = 0;
	}
}

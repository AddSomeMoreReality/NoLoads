#include "Profiler.h"

#include "Trace.h"

namespace
{
	using Phase = Profiler::Phase;

	constexpr std::size_t kMaxSamples = 20'000;  // 20 s at 1 kHz
	constexpr std::size_t kMaxFrames = Profiler::kMaxStackFrames;
	constexpr std::size_t kStackCopy = Profiler::kStackBufferSize;
	constexpr auto        kInterval = std::chrono::microseconds(1000);
	constexpr std::size_t kTop = 10;
	constexpr double      kTreeThreshold = 0.02;  // call tree nodes under 2% of the phase are dropped

	constexpr std::array kPhaseNames{ "open->kPre"sv, "kPre->enter"sv, "enter->kPost"sv, "kPost->kFinish"sv, "kFinish->close"sv };

	struct Sample
	{
		Phase          phase;
		std::uint8_t   frameCount;
		std::uintptr_t frames[kMaxFrames];  // innermost first
	};

	std::atomic<bool>  g_busy{ false };  // a sampler thread is running or reporting
	std::atomic<bool>  g_stop{ false };
	std::atomic<Phase> g_phase{ Phase::kOpenToPre };

	std::uintptr_t g_textBegin{ 0 };
	std::uintptr_t g_textEnd{ 0 };

	bool InGameText(std::uintptr_t a_address)
	{
		return a_address >= g_textBegin && a_address < g_textEnd;
	}

	void RebaseRegisters(CONTEXT& a_context, std::uintptr_t a_from, std::size_t a_size, std::uintptr_t a_to)
	{
		DWORD64* registers[] = { &a_context.Rax, &a_context.Rcx, &a_context.Rdx, &a_context.Rbx, &a_context.Rsp,
			&a_context.Rbp, &a_context.Rsi, &a_context.Rdi, &a_context.R8, &a_context.R9, &a_context.R10,
			&a_context.R11, &a_context.R12, &a_context.R13, &a_context.R14, &a_context.R15 };
		for (auto* reg : registers) {
			if (*reg >= a_from && *reg < a_from + a_size) {
				*reg = *reg - a_from + a_to;
			}
		}
	}

	// Walks the copied stack with the modules' unwind data (RtlVirtualUnwind). Registers that point
	// into the original stack are moved onto the copy after every step, so the walk never reads the
	// live stack. SEH only, no objects with destructors in this function.
	std::uint8_t Unwind(CONTEXT& a_context, std::uintptr_t a_originalRsp, std::uint8_t* a_copy, std::size_t a_copied, std::uintptr_t* a_frames)
	{
		const auto   copyBase = reinterpret_cast<std::uintptr_t>(a_copy);
		std::uint8_t count = 0;
		__try {
			RebaseRegisters(a_context, a_originalRsp, a_copied, copyBase);
			a_frames[count++] = a_context.Rip;
			while (count < kMaxFrames) {
				if (a_context.Rsp < copyBase || a_context.Rsp + sizeof(DWORD64) > copyBase + a_copied) {
					break;
				}
				DWORD64 imageBase = 0;
				if (auto* entry = ::RtlLookupFunctionEntry(a_context.Rip, &imageBase, nullptr)) {
					void*   handlerData = nullptr;
					DWORD64 establisherFrame = 0;
					::RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, a_context.Rip, entry, &a_context, &handlerData, &establisherFrame, nullptr);
				} else {  // leaf without unwind data: the return address is on top of the stack
					a_context.Rip = *reinterpret_cast<DWORD64*>(a_context.Rsp);
					a_context.Rsp += sizeof(DWORD64);
				}
				if (a_context.Rip == 0) {
					break;
				}
				RebaseRegisters(a_context, a_originalRsp, a_copied, copyBase);
				a_frames[count++] = a_context.Rip;
			}
		} __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
		}
		return count;
	}

	class Labels
	{
	public:
		// Function-level label: "AE#id" in SkyrimSE.exe, the module name elsewhere.
		const std::string& Get(std::uintptr_t a_address)
		{
			auto it = _cache.find(a_address);
			if (it == _cache.end()) {
				it = _cache.emplace(a_address, Trace::DescribeAddress(a_address, false)).first;
			}
			return it->second;
		}

	private:
		std::unordered_map<std::uintptr_t, std::string> _cache;
	};

	struct Node
	{
		std::uint32_t               count{ 0 };
		std::map<std::string, Node> children;
	};

	// [label, count, [children]] with single-child chains of equal weight merged into "A > B".
	std::string TreeJson(const std::string& a_label, const Node& a_node, std::uint32_t a_minCount, int a_depth)
	{
		std::string label = a_label;
		const Node* node = &a_node;
		while (a_depth < 200) {
			const Node* only = nullptr;
			std::string onlyLabel;
			for (const auto& [childLabel, child] : node->children) {
				if (child.count >= a_minCount) {
					if (only) {
						only = nullptr;
						break;
					}
					only = &child;
					onlyLabel = childLabel;
				}
			}
			if (!only || only->count != node->count) {
				break;
			}
			label += " > " + onlyLabel;
			node = only;
			++a_depth;
		}

		std::vector<std::pair<std::string, const Node*>> kept;
		for (const auto& [childLabel, child] : node->children) {
			if (child.count >= a_minCount) {
				kept.emplace_back(childLabel, &child);
			}
		}
		std::ranges::sort(kept, [](const auto& a_lhs, const auto& a_rhs) { return a_lhs.second->count > a_rhs.second->count; });

		std::string json = std::format("[{},{},[", Trace::JsonString(label), node->count);
		for (std::size_t i = 0; i < kept.size() && a_depth < 200; ++i) {
			json += (i ? "," : "") + TreeJson(kept[i].first, *kept[i].second, a_minCount, a_depth + 1);
		}
		return json + "]]";
	}

	// Follows the heaviest child while it holds at least half of the phase.
	std::string DominantPath(const Node& a_root, std::uint32_t a_total)
	{
		std::string path;
		const Node* node = &a_root;
		while (true) {
			const std::pair<const std::string, Node>* best = nullptr;
			for (const auto& entry : node->children) {
				if (!best || entry.second.count > best->second.count) {
					best = &entry;
				}
			}
			if (!best || best->second.count * 2 < a_total) {
				break;
			}
			path += (path.empty() ? "" : " > ") + best->first;
			node = &best->second;
		}
		return path;
	}

	std::string TopJson(const std::map<std::string, std::uint32_t>& a_counts)
	{
		std::vector<std::pair<std::string, std::uint32_t>> sorted(a_counts.begin(), a_counts.end());
		std::ranges::sort(sorted, [](const auto& a_lhs, const auto& a_rhs) { return a_lhs.second > a_rhs.second; });
		if (sorted.size() > kTop) {
			sorted.resize(kTop);
		}
		std::string json = "[";
		for (const auto& [key, count] : sorted) {
			json += std::format("{}[{},{}]", json.size() > 1 ? "," : "", Trace::JsonString(key), count);
		}
		return json + "]";
	}

	void Report(const std::vector<Sample>& a_samples, std::int64_t a_startUs, std::int64_t a_endUs)
	{
		struct PhaseStats
		{
			std::uint32_t                        samples{ 0 };
			std::map<std::string, std::uint32_t> leafModules;  // where the instruction pointer was
			std::map<std::string, std::uint32_t> self;         // innermost SkyrimSE.exe function
			std::map<std::string, std::uint32_t> inclusive;    // SkyrimSE.exe functions anywhere on the stack
			Node                                 tree;         // outermost frame first
		};
		std::array<PhaseStats, static_cast<std::size_t>(Phase::kTotal)> stats;
		Labels                                                          labels;

		std::vector<const std::string*> path;
		std::set<const std::string*>    seen;
		for (const auto& sample : a_samples) {
			auto& phase = stats[static_cast<std::size_t>(sample.phase)];
			++phase.samples;
			if (sample.frameCount == 0) {
				++phase.leafModules["<no stack>"];
				continue;
			}
			const auto leaf = sample.frames[0];
			++phase.leafModules[InGameText(leaf) ? "SkyrimSE.exe" : labels.Get(leaf)];

			path.clear();
			seen.clear();
			bool selfCounted = false;
			for (std::uint8_t i = 0; i < sample.frameCount; ++i) {
				const auto& label = labels.Get(sample.frames[i]);
				if (InGameText(sample.frames[i])) {
					if (!selfCounted) {
						++phase.self[label];
						selfCounted = true;
					}
					if (seen.insert(&label).second) {
						++phase.inclusive[label];
					}
				}
				if (path.empty() || path.back() != &label) {  // collapse direct recursion
					path.push_back(&label);
				}
			}
			Node* node = &phase.tree;
			++node->count;
			for (auto it = path.rbegin(); it != path.rend(); ++it) {
				node = &node->children[**it];
				++node->count;
			}
		}

		std::string phases = "[";
		for (std::size_t i = 0; i < stats.size(); ++i) {
			const auto& phase = stats[i];
			if (phase.samples == 0) {
				continue;
			}
			const auto minCount = (std::max)(2u, static_cast<std::uint32_t>(phase.samples * kTreeThreshold));
			phases += std::format("{}{{\"phase\":\"{}\",\"samples\":{},\"leafModules\":{},\"self\":{},\"inclusive\":{},\"tree\":{}}}",
				phases.size() > 1 ? "," : "", kPhaseNames[i], phase.samples,
				TopJson(phase.leafModules), TopJson(phase.self), TopJson(phase.inclusive),
				TreeJson("<root>", phase.tree, minCount, 0));
			logs::info("[Profile] {}: {} samples, dominant path: {}", kPhaseNames[i], phase.samples, DominantPath(phase.tree, phase.samples));
		}
		phases += "]";

		Trace::Write("profile", Trace::Args{}
									.Int("samples", static_cast<std::int64_t>(a_samples.size()))
									.Int("interval_us", kInterval.count())
									.Int("window_us", a_endUs - a_startUs)
									.Raw("phases", phases),
			nullptr, true);
	}

	void Run(std::uint32_t a_mainThreadId)
	{
		const auto startUs = Trace::NowUs();
		HANDLE     thread = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, a_mainThreadId);
		if (!thread) {
			Trace::Write("profile.error", Trace::Args{}.Int("lastError", ::GetLastError()));
			g_busy = false;
			return;
		}

		std::vector<Sample> samples;
		samples.reserve(kMaxSamples);  // no allocation while sampling
		const auto stack = std::make_unique<std::uint8_t[]>(kStackCopy);

		auto next = std::chrono::steady_clock::now();
		while (!g_stop.load() && samples.size() < kMaxSamples) {
			auto& sample = samples.emplace_back();
			sample.phase = g_phase.load();
			sample.frameCount = Profiler::CaptureStack(thread, stack.get(), sample.frames);

			next += kInterval;
			const auto now = std::chrono::steady_clock::now();
			if (next < now) {
				next = now;
			}
			while (std::chrono::steady_clock::now() < next && !g_stop.load()) {
				std::this_thread::yield();
			}
		}
		::CloseHandle(thread);

		Report(samples, startUs, Trace::NowUs());
		g_busy = false;
	}
}

namespace Profiler
{
	// While the thread is suspended only syscalls and memcpy run here:
	// no heap, no locks, nothing the suspended thread could be holding.
	std::uint8_t CaptureStack(void* a_thread, std::uint8_t* a_stack, std::uintptr_t* a_frames)
	{
		CONTEXT context{};
		context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		std::size_t    copied = 0;
		std::uintptr_t rsp = 0;

		if (::SuspendThread(a_thread) == static_cast<DWORD>(-1)) {
			return 0;
		}
		if (::GetThreadContext(a_thread, &context)) {
			rsp = context.Rsp;
			MEMORY_BASIC_INFORMATION memory{};
			if (::VirtualQuery(reinterpret_cast<void*>(rsp), &memory, sizeof(memory)) && memory.State == MEM_COMMIT) {
				const auto end = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
				copied = (std::min)(kStackCopy, static_cast<std::size_t>(end - rsp));
				std::memcpy(a_stack, reinterpret_cast<void*>(rsp), copied);
			}
		}
		::ResumeThread(a_thread);

		return context.Rip != 0 && copied != 0 ? Unwind(context, rsp, a_stack, copied, a_frames) : std::uint8_t{ 0 };
	}

	void Start()
	{
		if (g_busy.exchange(true)) {
			Trace::Write("profile.skipped", Trace::Args{}.Str("reason", "previous report still running"));
			return;
		}
		if (g_textEnd == 0) {
			const auto text = REL::Module::get().segment(REL::Segment::textx);
			g_textBegin = text.address();
			g_textEnd = text.address() + text.size();
		}
		g_stop = false;
		g_phase = Phase::kOpenToPre;
		std::thread(Run, ::GetCurrentThreadId()).detach();
	}

	void SetPhase(Phase a_phase)
	{
		g_phase = a_phase;
	}

	void Stop()
	{
		g_stop = true;
	}
}

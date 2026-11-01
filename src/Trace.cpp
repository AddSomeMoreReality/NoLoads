#include "Trace.h"

#include <share.h>

namespace Trace
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr auto kFlushInterval = std::chrono::milliseconds(250);

		const Clock::time_point g_start = Clock::now();

		std::mutex        g_lock;
		std::FILE*        g_file{ nullptr };
		std::uint64_t     g_seq{ 0 };
		bool              g_dirty{ false };
		Clock::time_point g_lastFlush{};

		std::atomic<std::uint64_t>          g_frame{ 0 };
		std::atomic<const REL::Offset2ID*> g_callerTable{ nullptr };

		bool IsUtf8(std::string_view a_str)
		{
			std::size_t i = 0;
			while (i < a_str.size()) {
				const auto c = static_cast<unsigned char>(a_str[i]);
				const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
				if (len == 0 || i + len > a_str.size()) {
					return false;
				}
				for (std::size_t k = 1; k < len; ++k) {
					if ((static_cast<unsigned char>(a_str[i + k]) >> 6) != 0x2) {
						return false;
					}
				}
				i += len;
			}
			return true;
		}

		// Localized game strings (e.g. the "Wilderness" cell name) come in the ANSI code page.
		std::string ToUtf8(std::string_view a_str)
		{
			if (IsUtf8(a_str)) {
				return std::string{ a_str };
			}
			constexpr std::uint32_t kAnsiCodePage = 0;  // CP_ACP, a macro in <windows.h>
			const auto              len = REX::W32::MultiByteToWideChar(kAnsiCodePage, 0, a_str.data(), static_cast<std::int32_t>(a_str.size()), nullptr, 0);
			std::wstring            wide(static_cast<std::size_t>(len > 0 ? len : 0), L'\0');
			REX::W32::MultiByteToWideChar(kAnsiCodePage, 0, a_str.data(), static_cast<std::int32_t>(a_str.size()), wide.data(), len);
			return SKSE::stl::utf16_to_utf8(wide).value_or(std::string{});
		}

		void AppendEscaped(std::string& a_out, std::string_view a_str)
		{
			for (const char c : a_str) {
				switch (c) {
				case '"':
					a_out += "\\\"";
					break;
				case '\\':
					a_out += "\\\\";
					break;
				case '\n':
					a_out += "\\n";
					break;
				case '\r':
					a_out += "\\r";
					break;
				case '\t':
					a_out += "\\t";
					break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) {
						a_out += std::format("\\u{:04x}", static_cast<unsigned>(c));
					} else {
						a_out += c;
					}
					break;
				}
			}
		}

		std::string DescribeCaller(const void* a_address)
		{
			return DescribeAddress(reinterpret_cast<std::uintptr_t>(a_address), true);
		}

		bool IsMainThread(std::uint32_t a_tid)
		{
			const auto* main = RE::Main::GetSingleton();
			return main && main->threadID == a_tid;
		}

		void FlushLocked(Clock::time_point a_now)
		{
			std::fflush(g_file);
			g_dirty = false;
			g_lastFlush = a_now;
		}
	}

	void Args::Key(std::string_view a_key)
	{
		if (!_body.empty()) {
			_body += ',';
		}
		_body += '"';
		_body += a_key;
		_body += "\":";
	}

	Args& Args::Bool(std::string_view a_key, bool a_value)
	{
		Key(a_key);
		_body += a_value ? "true" : "false";
		return *this;
	}

	Args& Args::Int(std::string_view a_key, std::int64_t a_value)
	{
		Key(a_key);
		_body += std::to_string(a_value);
		return *this;
	}

	Args& Args::Num(std::string_view a_key, double a_value)
	{
		Key(a_key);
		_body += std::isfinite(a_value) ? std::format("{}", a_value) : "null";
		return *this;
	}

	Args& Args::Str(std::string_view a_key, std::string_view a_value)
	{
		Key(a_key);
		_body += '"';
		AppendEscaped(_body, ToUtf8(a_value));
		_body += '"';
		return *this;
	}

	Args& Args::Hex(std::string_view a_key, std::uintptr_t a_value)
	{
		return Str(a_key, std::format("{:#x}", a_value));
	}

	Args& Args::FormID(std::string_view a_key, RE::FormID a_id)
	{
		return Str(a_key, std::format("{:08X}", a_id));
	}

	Args& Args::Form(std::string_view a_key, const RE::TESForm* a_form)
	{
		if (!a_form) {
			Key(a_key);
			_body += "null";
			return *this;
		}
		return Str(a_key, FormString(a_form));
	}

	Args& Args::Cell(std::string_view a_key, RE::TESObjectCELL* a_cell)
	{
		Form(a_key, a_cell);
		if (a_cell && a_cell->IsExteriorCell()) {
			if (const auto* coordinates = a_cell->GetCoordinates()) {
				XY(std::format("{}_xy", a_key), coordinates->cellX, coordinates->cellY);
			}
		}
		return *this;
	}

	Args& Args::XY(std::string_view a_key, std::int64_t a_x, std::int64_t a_y)
	{
		Key(a_key);
		_body += std::format("[{},{}]", a_x, a_y);
		return *this;
	}

	Args& Args::Pos(std::string_view a_key, const RE::NiPoint3& a_pos)
	{
		Key(a_key);
		// std::format writes "-nan"/"inf", which is not JSON (0.8.0 window detector)
		const auto number = [](float a_value) { return std::isfinite(a_value) ? std::format("{:.1f}", a_value) : std::string{ "null" }; };
		_body += std::format("[{},{},{}]", number(a_pos.x), number(a_pos.y), number(a_pos.z));
		return *this;
	}

	Args& Args::Raw(std::string_view a_key, std::string_view a_json)
	{
		Key(a_key);
		_body += a_json;
		return *this;
	}

	void Init(const std::filesystem::path& a_directory)
	{
		auto path = a_directory / std::format("{}.trace.jsonl", SKSE::GetPluginName());
		// Shared for reading, so the trace can be watched while the game runs.
		g_file = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
		if (!g_file) {
			// 0.8.10: a game process that hung after Alt+Tab still held the file, and the next run had no trace
			logs::error("failed to open trace file {}", path.string());
			path = a_directory / std::format("{}.trace-{}.jsonl", SKSE::GetPluginName(), GetCurrentProcessId());
			g_file = _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
			if (!g_file) {
				logs::error("failed to open trace file {}", path.string());
				return;
			}
		}
		std::setvbuf(g_file, nullptr, _IOFBF, 1 << 16);
		logs::info("trace: {}", path.string());
	}

	void BuildCallerTable()
	{
		if (g_callerTable.load(std::memory_order_acquire)) {
			return;
		}
		const auto t0 = NowUs();
		const auto table = new REL::Offset2ID();  // lives until process exit
		g_callerTable.store(table, std::memory_order_release);
		logs::info("caller table: {} entries in {} ms", table->size(), (NowUs() - t0) / 1000);
	}

	void Write(std::string_view a_ev, const Args& a_args, const void* a_caller, bool a_flush)
	{
		if (!g_file) {
			return;
		}

		const auto tid = REX::W32::GetCurrentThreadId();
		const bool main = IsMainThread(tid);

		std::string tail;
		if (!a_args.Empty()) {
			tail += ",\"args\":";
			tail += a_args.Json();
		}
		if (a_caller) {
			tail += ",\"caller\":\"";
			tail += DescribeCaller(a_caller);
			tail += '"';
		}
		tail += "}\n";

		const std::scoped_lock lock{ g_lock };
		const auto             now = Clock::now();
		const auto             head = std::format(
            R"({{"seq":{},"t_us":{},"frame":{},"tid":{},"main":{},"ev":"{}")",
            ++g_seq,
            std::chrono::duration_cast<std::chrono::microseconds>(now - g_start).count(),
            g_frame.load(std::memory_order_relaxed),
            tid,
            main,
            a_ev);
		std::fwrite(head.data(), 1, head.size(), g_file);
		std::fwrite(tail.data(), 1, tail.size(), g_file);
		g_dirty = true;

		if (a_flush || now - g_lastFlush >= kFlushInterval) {
			FlushLocked(now);
		}
	}

	void Flush()
	{
		const std::scoped_lock lock{ g_lock };
		if (g_file) {
			FlushLocked(Clock::now());
		}
	}

	void FlushIfStale()
	{
		const std::unique_lock lock{ g_lock, std::try_to_lock };
		if (!lock || !g_file || !g_dirty) {
			return;
		}
		const auto now = Clock::now();
		if (now - g_lastFlush >= kFlushInterval) {
			FlushLocked(now);
		}
	}

	void NextFrame()
	{
		g_frame.fetch_add(1, std::memory_order_relaxed);
	}

	std::uint64_t Frame()
	{
		return g_frame.load(std::memory_order_relaxed);
	}

	std::int64_t NowUs()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - g_start).count();
	}

	bool IsMainThread()
	{
		return IsMainThread(REX::W32::GetCurrentThreadId());
	}

	std::string FormString(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "<none>";
		}
		const char* edid = a_form->GetFormEditorID();
		return std::format("{} [{:08X}]", (edid && *edid) ? edid : "-", a_form->GetFormID());
	}

	std::string JsonString(std::string_view a_str)
	{
		std::string out = "\"";
		AppendEscaped(out, ToUtf8(a_str));
		return out + '"';
	}

	// Nearest lower ID + offset: a hint, not necessarily the real function.
	std::string DescribeAddress(std::uintptr_t a_address, bool a_withOffset)
	{
		const auto& module = REL::Module::get();
		const auto  text = module.segment(REL::Segment::textx);
		if (a_address >= text.address() && a_address < text.address() + text.size()) {
			const auto offset = a_address - module.base();
			if (const auto* table = g_callerTable.load(std::memory_order_acquire)) {
				auto it = std::upper_bound(table->begin(), table->end(), offset, [](std::uint64_t a_offset, const REL::Offset2ID::value_type& a_elem) {
					return a_offset < a_elem.offset;
				});
				if (it != table->begin()) {
					--it;
					const auto prefix = REL::Module::IsAE() ? "AE" : "SE";
					return a_withOffset ? std::format("{}#{}+{:#x}", prefix, it->id, offset - it->offset) : std::format("{}#{}", prefix, it->id);
				}
			}
			return std::format("SkyrimSE.exe+{:#x}", offset);
		}

		HMODULE other = nullptr;
		if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(a_address), &other)) {
			wchar_t    path[MAX_PATH]{};
			const auto len = ::GetModuleFileNameW(other, path, MAX_PATH);
			const auto name = std::filesystem::path(std::wstring_view{ path, len }).filename().string();
			return a_withOffset ? std::format("{}+{:#x}", name, a_address - reinterpret_cast<std::uintptr_t>(other)) : name;
		}
		return a_withOffset ? std::format("{:#x}", a_address) : "<unknown>";
	}

	std::string_view CellStateName(std::uint32_t a_state)
	{
		constexpr std::array names{ "kUnloaded"sv, "kUnloading"sv, "kDataLoaded"sv, "kBuilding3D"sv,
			"kUnattached"sv, "kDetaching"sv, "kAttaching"sv, "kAttached"sv };
		return a_state < names.size() ? names[a_state] : "?"sv;
	}

	std::uint32_t CellState(const RE::TESObjectCELL* a_cell)
	{
		return a_cell ? static_cast<std::uint32_t>(std::to_underlying(a_cell->cellState.get())) : 0;
	}
}

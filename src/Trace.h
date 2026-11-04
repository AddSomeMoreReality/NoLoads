#pragma once

// JSONL trace: one record per line
// {"seq":..,"t_us":..,"frame":..,"tid":..,"main":..,"ev":"..","args":{..},"caller":".."}
namespace Trace
{
	// Builds the JSON object for the "args" field.
	class Args
	{
	public:
		Args& Bool(std::string_view a_key, bool a_value);
		Args& Int(std::string_view a_key, std::int64_t a_value);
		Args& Num(std::string_view a_key, double a_value);
		Args& Str(std::string_view a_key, std::string_view a_value);
		Args& Hex(std::string_view a_key, std::uintptr_t a_value);
		Args& FormID(std::string_view a_key, RE::FormID a_id);
		// "EditorID [FormID]", or null. Reads the editor ID: avoid on IO threads, use FormID there.
		Args& Form(std::string_view a_key, const RE::TESForm* a_form);
		// Form plus "<key>_xy":[x,y] for exterior cells.
		Args& Cell(std::string_view a_key, RE::TESObjectCELL* a_cell);
		Args& XY(std::string_view a_key, std::int64_t a_x, std::int64_t a_y);
		Args& Pos(std::string_view a_key, const RE::NiPoint3& a_pos);
		// a_json must be a complete JSON value.
		Args& Raw(std::string_view a_key, std::string_view a_json);

		[[nodiscard]] bool        Empty() const noexcept { return _body.empty(); }
		[[nodiscard]] std::string Json() const { return "{" + _body + "}"; }

	private:
		void Key(std::string_view a_key);

		std::string _body;
	};

	// Opens <a_directory>\<plugin>.trace.jsonl, truncating the previous run.
	void Init(const std::filesystem::path& a_directory);

	// Builds the offset -> Address Library ID table used for "caller". Call once, on the main thread.
	void BuildCallerTable();

	// Thread-safe. a_caller is a return address inside SkyrimSE.exe (from _ReturnAddress()).
	void Write(std::string_view a_ev, const Args& a_args = {}, const void* a_caller = nullptr, bool a_flush = false);

	void Flush();
	void FlushIfStale();

	// Frame counter, advanced by the PlayerCharacter::Update hook.
	void                        NextFrame();
	[[nodiscard]] std::uint64_t Frame();

	// Microseconds since the plugin was loaded (steady_clock).
	[[nodiscard]] std::int64_t NowUs();

	[[nodiscard]] bool IsMainThread();

	[[nodiscard]] std::string FormString(const RE::TESForm* a_form);

	// Quoted and escaped JSON string.
	[[nodiscard]] std::string JsonString(std::string_view a_str);

	// SkyrimSE.exe code: "AE#<nearest lower Address Library ID>[+offset]"; other modules: "<module>[+offset]".
	[[nodiscard]] std::string DescribeAddress(std::uintptr_t a_address, bool a_withOffset);

	// JSON array of up to 16 return addresses (DescribeAddress) on the calling thread's stack: the first
	// is in the function that calls StackJson, a_skip more frames are skipped above it. Any thread.
	[[nodiscard]] std::string StackJson(std::size_t a_skip, std::size_t a_count);

	// TESObjectCELL::CellState as text: kUnloaded .. kAttached.
	[[nodiscard]] std::string_view CellStateName(std::uint32_t a_state);
	[[nodiscard]] std::uint32_t    CellState(const RE::TESObjectCELL* a_cell);  // kUnloaded for null
}

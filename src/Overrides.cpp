#include "Overrides.h"

#include "Hud.h"
#include "Settings.h"
#include "Trace.h"

namespace
{
	struct Value
	{
		std::string       name;
		RE::Setting*      setting;
		RE::Setting::Data data;
		std::string       text;
	};

	std::vector<std::vector<Value>>                          g_sets;   // g_sets[i] = [Overrides] sSet<i+1>
	std::vector<std::pair<RE::Setting*, RE::Setting::Data>>  g_game;   // game values of every overridden setting
	std::atomic<std::uint32_t>                               g_active{ 0 };

	RE::Setting* FindSetting(const std::string& a_name)
	{
		if (auto* ini = RE::INISettingCollection::GetSingleton()) {
			if (auto* setting = ini->GetSetting(a_name)) {
				return setting;
			}
		}
		if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
			if (auto* setting = prefs->GetSetting(a_name)) {
				return setting;
			}
		}
		auto* game = RE::GameSettingCollection::GetSingleton();
		return game ? game->GetSetting(a_name.c_str()) : nullptr;
	}

	std::string_view Trim(std::string_view a_str)
	{
		const auto first = a_str.find_first_not_of(" \t");
		if (first == std::string_view::npos) {
			return {};
		}
		return a_str.substr(first, a_str.find_last_not_of(" \t") - first + 1);
	}

	// Only numeric settings: a string setting owns its buffer.
	std::optional<RE::Setting::Data> Parse(const RE::Setting& a_setting, const std::string& a_text)
	{
		using Type = RE::Setting::Type;
		RE::Setting::Data data = a_setting.data;
		switch (a_setting.GetType()) {
		case Type::kBool:
			data.b = std::strtol(a_text.c_str(), nullptr, 10) != 0;
			return data;
		case Type::kInteger:
			data.i = static_cast<std::int32_t>(std::strtol(a_text.c_str(), nullptr, 10));
			return data;
		case Type::kUnsignedInteger:
			data.u = static_cast<std::uint32_t>(std::strtoul(a_text.c_str(), nullptr, 10));
			return data;
		case Type::kFloat:
			data.f = std::strtof(a_text.c_str(), nullptr);
			return data;
		default:
			return std::nullopt;
		}
	}

	void Apply(std::uint32_t a_set)
	{
		for (auto& [setting, data] : g_game) {
			setting->data = data;
		}
		if (a_set > 0) {
			for (const auto& value : g_sets[a_set - 1]) {
				value.setting->data = value.data;
			}
		}
		g_active = a_set;
	}

	std::string Describe(std::uint32_t a_set)
	{
		if (a_set == 0) {
			return "game values";
		}
		std::string text;
		for (const auto& value : g_sets[a_set - 1]) {
			text += std::format("{}{}={}", text.empty() ? "" : ", ", value.name.substr(0, value.name.find(':')), value.text);
		}
		return text;
	}
}

namespace Overrides
{
	void Init()
	{
		for (const auto& line : Settings::Get().overrideSets) {
			std::vector<Value> set;
			for (const auto part : std::views::split(std::string_view{ line }, '|')) {
				const auto entry = Trim(std::string_view{ part.begin(), part.end() });
				const auto eq = entry.rfind('=');
				if (entry.empty() || eq == std::string_view::npos) {
					continue;
				}
				std::string name{ Trim(entry.substr(0, eq)) };
				std::string text{ Trim(entry.substr(eq + 1)) };
				auto*       setting = FindSetting(name);
				const auto  data = setting ? Parse(*setting, text) : std::nullopt;
				if (!data) {
					logs::warn("override {}={}: {}", name, text, setting ? "not a numeric setting" : "setting not found");
					continue;
				}
				if (std::ranges::find(g_game, setting, &decltype(g_game)::value_type::first) == g_game.end()) {
					g_game.emplace_back(setting, setting->data);
				}
				set.push_back({ std::move(name), setting, *data, std::move(text) });
			}
			g_sets.push_back(std::move(set));
		}

		Trace::Args args;
		args.Int("sets", static_cast<std::int64_t>(g_sets.size()));
		for (std::uint32_t i = 1; i <= g_sets.size(); ++i) {
			args.Str(std::format("set{}", i), Describe(i));
			logs::info("override set {}: {}", i, Describe(i));
		}
		Trace::Write("overrides.init", args, nullptr, true);
	}

	void Cycle()
	{
		if (g_sets.empty()) {
			return;
		}
		const auto next = (g_active + 1) % static_cast<std::uint32_t>(g_sets.size() + 1);
		Apply(next);

		Trace::Write("overrides.set", Trace::Args{}.Int("set", next).Str("values", Describe(next)), nullptr, true);
		logs::info("override set {}: {}", next, Describe(next));
		Hud::Show("overrides", std::format("CLR: set {}/{}: {}", next, g_sets.size(), Describe(next)), true);
	}

	std::uint32_t Active()
	{
		return g_active;
	}
}

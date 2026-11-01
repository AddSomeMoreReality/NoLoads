// CellLoadResearch: traces how Skyrim AE loads cells and locations during transitions.
// Output: <sLogDirectory>\CellLoadResearch.log (text) and CellLoadResearch.trace.jsonl (trace).

#include "Events.h"
#include "Hooks.h"
#include "InteriorSim.h"
#include "Overrides.h"
#include "Physics.h"
#include "Settings.h"
#include "Trace.h"
#include "Watchdog.h"

namespace
{
	// H10: which loading-related INI settings exist in this runtime.
	constexpr std::array kIniCandidates{
		"uGridsToLoad:General",
		"uExterior Cell Buffer:General",
		"uInterior Cell Buffer:General",
		"bPreloadLinkedInteriors:General",
		"uLargeRefLODGridSize:TerrainManager",
		"iPreloadSizeLimit:General",
		"uGridDistantCount:General",
		"bBackgroundCellLoads:BackgroundLoad",
		"bUseBackgroundFileLoader:General",
	};

	// Same sinks/pattern as SKSE::Init's built-in logger, but in the folder from sLogDirectory.
	void InitLog(const std::filesystem::path& a_directory, bool a_usedFallback)
	{
		const auto path = a_directory / std::format("{}.log", SKSE::GetPluginName());

		std::vector<spdlog::sink_ptr> sinks{
			std::make_shared<spdlog::sinks::msvc_sink_mt>(),
			std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true)
		};

		auto logger = std::make_shared<spdlog::logger>("global", sinks.begin(), sinks.end());
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(logger));
		spdlog::set_pattern("[%T.%e] [%=5t] [%L] %v");

		const auto& settings = Settings::Get();
		logs::info("{} v{}", SKSE::GetPluginName(), SKSE::GetPluginVersion());
		logs::info("ini: {}", settings.IniPath().string());
		logs::info("log: {}", path.string());
		if (a_usedFallback) {
			logs::warn("sLogDirectory '{}' is not usable, fell back to the SKSE default folder", settings.logDirectory.string());
		}
	}

	void TracePluginInfo()
	{
		const auto runtime = REL::Module::get().version();
		if (runtime != SKSE::RUNTIME_SSE_1_6_1170) {
			logs::warn("runtime {} is not 1.6.1170; offsets/IDs were researched for 1.6.1170", runtime);
		} else {
			logs::info("runtime {}", runtime);
		}

		const auto& settings = Settings::Get();
		Trace::Write("info.plugin", Trace::Args{}
										.Str("version", std::format("{}", SKSE::GetPluginVersion()))
										.Str("runtime", std::format("{}", runtime))
										.Bool("snapshotGrid", settings.snapshotGrid)
										.Bool("readCellBuffers", settings.readCellBuffers)
										.Bool("logCellAttachPerRef", settings.logCellAttachPerRef)
										.Bool("blockDoorsInCombat", settings.blockDoorsInCombat),
			nullptr, true);
	}

	std::string SettingValue(const RE::Setting& a_setting)
	{
		using Type = RE::Setting::Type;
		switch (a_setting.GetType()) {
		case Type::kBool:
			return a_setting.data.b ? "1" : "0";
		case Type::kInteger:
			return std::to_string(a_setting.data.i);
		case Type::kUnsignedInteger:
			return std::to_string(a_setting.data.u);
		case Type::kFloat:
			return std::format("{}", a_setting.data.f);
		case Type::kString:
			return a_setting.data.s ? a_setting.data.s : "";
		default:
			return std::format("{:#x}", a_setting.data.u);
		}
	}

	std::filesystem::path g_logDirectory;

	// Every INI and game setting, to look for knobs of the post-load physics (kPost -> kFinish).
	void DumpAllSettings()
	{
		if (g_logDirectory.empty()) {
			return;
		}
		const auto    path = g_logDirectory / std::format("{}.settings.txt", SKSE::GetPluginName());
		std::ofstream out(path, std::ios::trunc);
		std::size_t   count = 0;
		const auto    dump = [&](std::string_view a_collection, const RE::Setting* a_setting) {
            if (a_setting && a_setting->GetName()) {
                out << a_collection << '\t' << a_setting->GetName() << '\t' << SettingValue(*a_setting) << '\n';
                ++count;
            }
		};
		if (auto* ini = RE::INISettingCollection::GetSingleton()) {
			for (const auto* setting : ini->settings) {
				dump("INI", setting);
			}
		}
		if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
			for (const auto* setting : prefs->settings) {
				dump("INIPref", setting);
			}
		}
		if (auto* game = RE::GameSettingCollection::GetSingleton()) {
			for (const auto& entry : game->settings) {
				dump("Game", entry.second);
			}
		}
		logs::info("settings: {} written to {}", count, path.string());
	}

	void TraceIniSettings()
	{
		auto* ini = RE::INISettingCollection::GetSingleton();
		auto* prefs = RE::INIPrefSettingCollection::GetSingleton();

		for (const std::string_view name : kIniCandidates) {
			RE::Setting* setting = ini ? ini->GetSetting(name) : nullptr;
			std::string_view collection = "INISettingCollection";
			if (!setting && prefs) {
				setting = prefs->GetSetting(name);
				collection = "INIPrefSettingCollection";
			}

			Trace::Args args;
			args.Str("name", name).Bool("found", setting != nullptr);
			if (setting) {
				const auto value = SettingValue(*setting);
				args.Str("collection", collection).Str("value", value);
				logs::info("ini {} = {} ({})", name, value, collection);
			} else {
				logs::info("ini {} not found", name);
			}
			Trace::Write("info.ini", args);
		}
		Trace::Flush();
	}

	std::string_view MessageName(std::uint32_t a_type)
	{
		using Msg = SKSE::MessagingInterface;
		switch (a_type) {
		case Msg::kPostLoad:
			return "kPostLoad";
		case Msg::kPostPostLoad:
			return "kPostPostLoad";
		case Msg::kPreLoadGame:
			return "kPreLoadGame";
		case Msg::kPostLoadGame:
			return "kPostLoadGame";
		case Msg::kSaveGame:
			return "kSaveGame";
		case Msg::kDeleteGame:
			return "kDeleteGame";
		case Msg::kInputLoaded:
			return "kInputLoaded";
		case Msg::kNewGame:
			return "kNewGame";
		case Msg::kDataLoaded:
			return "kDataLoaded";
		default:
			return "unknown";
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		Trace::Write("skse.message", Trace::Args{}.Int("type", a_msg->type).Str("name", MessageName(a_msg->type)), nullptr, true);

		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostPostLoad:
			Trace::BuildCallerTable();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Events::Register();
			TraceIniSettings();
			DumpAllSettings();
			Physics::InitWarmStart();
			Overrides::Init();
			break;
		case SKSE::MessagingInterface::kSaveGame:
			// SKSE sends it before the game writes the save: an attached interior is saved as the game
			// knows it, in its buffer. The portal attaches it again a second later.
			InteriorSim::Detach("save");
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			// The world is torn down next; the Loading Menu's close ends the transition.
			InteriorSim::SetTransition(true);
			InteriorSim::Detach("kPreLoadGame");
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	// Built-in logger is off: it always writes to <MyDocuments>\My Games\...\SKSE.
	SKSE::Init(a_skse, { .log = false, .trampoline = true, .trampolineSize = 64 });

	auto& settings = Settings::Get();
	settings.Load();

	bool       usedFallback = false;
	const auto directory = settings.ResolveLogDirectory(usedFallback);
	if (!directory.empty()) {
		g_logDirectory = directory;
		InitLog(directory, usedFallback);
		if (settings.traceEnabled) {
			Trace::Init(directory);
		}
		Watchdog::Start(directory);
	}

	TracePluginInfo();
	Hooks::Install();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

	logs::info("loaded");
	return true;
}

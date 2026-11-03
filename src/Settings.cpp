#include "Settings.h"

namespace
{
	std::filesystem::path GetIniPath()
	{
		// Not REX::W32::MAX_PATH: <windows.h> (pulled in by spdlog's msvc_sink) defines it as a macro.
		std::wstring buf(32768, L'\0');
		const auto   len = REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buf.data(), static_cast<std::uint32_t>(buf.size()));
		buf.resize(len);

		std::filesystem::path path{ buf };
		path.replace_extension(L".ini");
		return path;
	}

	std::wstring Trim(std::wstring_view a_str)
	{
		const auto first = a_str.find_first_not_of(L" \t");
		if (first == std::wstring_view::npos) {
			return {};
		}
		const auto last = a_str.find_last_not_of(L" \t");
		return std::wstring{ a_str.substr(first, last - first + 1) };
	}

	class IniReader
	{
	public:
		explicit IniReader(const std::filesystem::path& a_file) :
			_file(a_file.wstring())
		{}

		// Returns std::nullopt when the key is missing.
		std::optional<std::wstring> Get(const wchar_t* a_section, const wchar_t* a_key) const
		{
			constexpr auto missing = L"\x01";

			std::wstring buf(4096, L'\0');
			const auto   len = REX::W32::GetPrivateProfileStringW(a_section, a_key, missing, buf.data(), static_cast<std::uint32_t>(buf.size()), _file.c_str());
			buf.resize(len);
			if (buf == missing) {
				return std::nullopt;
			}

			// GetPrivateProfileString keeps inline comments: "value    ; comment"
			for (std::size_t i = 0; i < buf.size(); ++i) {
				if (buf[i] == L';' && (i == 0 || buf[i - 1] == L' ' || buf[i - 1] == L'\t')) {
					buf.resize(i);
					break;
				}
			}
			return Trim(buf);
		}

		void Read(const wchar_t* a_section, const wchar_t* a_key, bool& a_value) const
		{
			if (const auto str = Get(a_section, a_key); str && !str->empty()) {
				a_value = std::wcstol(str->c_str(), nullptr, 10) != 0;
			}
		}

		void Read(const wchar_t* a_section, const wchar_t* a_key, std::uint32_t& a_value) const
		{
			if (const auto str = Get(a_section, a_key); str && !str->empty()) {
				a_value = static_cast<std::uint32_t>(std::wcstoul(str->c_str(), nullptr, 10));
			}
		}

		void Read(const wchar_t* a_section, const wchar_t* a_key, float& a_value) const
		{
			if (const auto str = Get(a_section, a_key); str && !str->empty()) {
				a_value = std::wcstof(str->c_str(), nullptr);
			}
		}

		void Read(const wchar_t* a_section, const wchar_t* a_key, std::string& a_value) const
		{
			if (const auto str = Get(a_section, a_key)) {
				a_value = SKSE::stl::utf16_to_utf8(*str).value_or(std::string{});
			}
		}

		void Read(const wchar_t* a_section, const wchar_t* a_key, std::filesystem::path& a_value) const
		{
			if (const auto str = Get(a_section, a_key)) {
				a_value = *str;
			}
		}

	private:
		std::wstring _file;
	};
}

Settings& Settings::Get()
{
	static Settings singleton;
	return singleton;
}

void Settings::Load()
{
	_iniPath = GetIniPath();
	const IniReader ini{ _iniPath };

	ini.Read(L"General", L"bTraceEnabled", traceEnabled);
	ini.Read(L"General", L"bSnapshotGrid", snapshotGrid);
	ini.Read(L"General", L"bReadCellBuffers", readCellBuffers);
	ini.Read(L"General", L"bLogCellAttachPerRef", logCellAttachPerRef);
	ini.Read(L"General", L"bBlockDoorsInCombat", blockDoorsInCombat);
	ini.Read(L"General", L"bHotkeyMessages", hotkeyMessages);
	ini.Read(L"General", L"bProfileLoadingScreen", profileLoadingScreen);
	ini.Read(L"General", L"sLogDirectory", logDirectory);

	ini.Read(L"Hotkeys", L"iSnapshot", snapshotKey);
	ini.Read(L"Hotkeys", L"iCycleOverrides", overridesKey);
	ini.Read(L"Hotkeys", L"iDetectorsReport", detectorsKey);
	ini.Read(L"Hotkeys", L"iInteriorSimStage", interiorSimKey);
	ini.Read(L"Hotkeys", L"iToggleTemporalAA", toggleTemporalAAKey);
	ini.Read(L"Hotkeys", L"iInteriorImageSpace", interiorImageSpaceKey);
	ini.Read(L"Hotkeys", L"iAccessWatch", accessWatchKey);

	overrideSets.clear();
	for (int i = 1; i <= 9; ++i) {
		std::string set;
		ini.Read(L"Overrides", std::format(L"sSet{}", i).c_str(), set);
		if (!set.empty()) {
			overrideSets.push_back(std::move(set));
		}
	}

	ini.Read(L"Preload", L"bEnabled", preloadEnabled);
	ini.Read(L"Preload", L"sInteriorCell", preloadInterior);
	ini.Read(L"Preload", L"fRefreshSeconds", preloadRefreshSeconds);
	ini.Read(L"Preload", L"bPreloadLinkedInteriors", preloadLinkedInteriors);
	ini.Read(L"Preload", L"bKeepInterior", keepInterior);
	ini.Read(L"Preload", L"bKeepInteriorNearDoorOnly", keepInteriorNearDoorOnly);

	ini.Read(L"Detectors", L"bEnabled", detectorsEnabled);
	ini.Read(L"Detectors", L"fDoorsIntervalSeconds", doorsIntervalSeconds);
	ini.Read(L"Detectors", L"iNearestCount", nearestCount);

	ini.Read(L"InteriorSim", L"iStartStage", interiorSimStage);
	ini.Read(L"InteriorSim", L"fStatusSeconds", interiorSimStatusSeconds);
	ini.Read(L"InteriorSim", L"bForceWorldActive", interiorSimForceWorldActive);
	ini.Read(L"InteriorSim", L"bAttachInterior", interiorSimAttachInterior);

	ini.Read(L"Portal", L"bEnabled", portalEnabled);
	ini.Read(L"Portal", L"iWidth", portalWidth);
	ini.Read(L"Portal", L"iHeight", portalHeight);
	ini.Read(L"Portal", L"iColorSlot", portalColorSlot);
	ini.Read(L"Portal", L"iDepthSlot", portalDepthSlot);
	ini.Read(L"Portal", L"bInteriorLights", portalInteriorLights);
	ini.Read(L"Portal", L"bMirrorPlayerLights", portalMirrorPlayerLights);
	ini.Read(L"Portal", L"bInteriorAmbient", portalInteriorAmbient);
	ini.Read(L"Portal", L"bLightEffects", portalLightEffects);
	ini.Read(L"Portal", L"bAnimateActors", portalAnimateActors);
	ini.Read(L"Portal", L"bSyncActorPositions", portalSyncActorPositions);
	ini.Read(L"Portal", L"iControllerUpdate", portalControllerMode);
	ini.Read(L"Portal", L"bFullActorProcessing", portalFullActorProcessing);
	ini.Read(L"Portal", L"bGameAnimatesActors", portalGameAnimatesActors);
	ini.Read(L"Portal", L"bIgnoreParticleBudget", portalIgnoreParticleBudget);
	ini.Read(L"Portal", L"bMasterParticles", portalMasterParticles);
	ini.Read(L"Portal", L"iParticleFacing", portalParticleFacing);
	ini.Read(L"Portal", L"bActorsSeen", portalActorsSeen);
	ini.Read(L"Portal", L"bVirtualCamera", portalVirtualCamera);
	ini.Read(L"Portal", L"bSoundThroughDoor", portalSoundThroughDoor);
	ini.Read(L"Portal", L"bDistanceThroughDoor", portalDistanceThroughDoor);
	ini.Read(L"Portal", L"bSubtitlesThroughDoor", portalSubtitlesThroughDoor);

	ini.Read(L"Watchdog", L"bEnabled", watchdogEnabled);
	ini.Read(L"Watchdog", L"fHangSeconds", watchdogHangSeconds);
	ini.Read(L"Watchdog", L"bTerminate", watchdogTerminate);

	ini.Read(L"AccessWatch", L"bEnabled", accessWatchEnabled);
	ini.Read(L"AccessWatch", L"fSeconds", accessWatchSeconds);
	ini.Read(L"AccessWatch", L"iMaxHits", accessWatchMaxHits);

	ini.Read(L"Probe", L"bEnabled", probeEnabled);
	ini.Read(L"Probe", L"fIntervalSeconds", probeIntervalSeconds);
	ini.Read(L"Probe", L"iMaxActors", probeMaxActors);
	ini.Read(L"Probe", L"bActorJumps", probeActorJumps);
}

std::filesystem::path Settings::ResolveLogDirectory(bool& a_usedFallback) const
{
	a_usedFallback = false;
	if (!logDirectory.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(logDirectory, ec);
		if (std::filesystem::is_directory(logDirectory, ec)) {
			return std::filesystem::absolute(logDirectory, ec);
		}
		a_usedFallback = true;
	}
	return SKSE::log::log_directory().value_or(std::filesystem::path{});
}

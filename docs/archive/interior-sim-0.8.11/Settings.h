#pragma once

// Values from Data\SKSE\Plugins\CellLoadResearch.ini (same folder and stem as the DLL).
struct Settings
{
	// [General]
	bool                  traceEnabled{ true };
	bool                  snapshotGrid{ true };
	bool                  readCellBuffers{ false };
	bool                  logCellAttachPerRef{ false };
	bool                  blockDoorsInCombat{ false };
	bool                  showCellNotifications{ true };  // HUD message on every cell change
	bool                  profileLoadingScreen{ true };   // sample the main thread while the Loading Menu is open
	std::filesystem::path logDirectory;                   // empty -> <MyDocuments>\My Games\Skyrim Special Edition\SKSE

	// [Hotkeys] (DirectInput scan codes)
	std::uint32_t startAutoTestKey{ 68 };  // F10
	std::uint32_t snapshotKey{ 87 };       // F11
	std::uint32_t overridesKey{ 65 };      // F7: cycle [Overrides] sets (not F9: that is the game's quickload)
	std::uint32_t detectorsKey{ 64 };      // F6: doors and windows summary on the HUD
	std::uint32_t interiorSimKey{ 66 };    // F8: cycle InteriorSim stages
	std::uint32_t portalDepthKey{ 62 };    // F4: cycle the portal's depth modes

	// [Overrides] sSet1..sSet9: "name:Section=value | ..." applied in memory by the hotkey
	std::vector<std::string> overrideSets;

	// [AutoTest]
	bool        autoTestEnabled{ false };
	std::string saveName;  // empty -> most recent save
	float       settleSeconds{ 3.0f };
	float       stepTimeoutSeconds{ 60.0f };
	bool        quitWhenDone{ false };
	std::string steps{ "coc:RiverwoodSleepingGiantInn | door:nearest | door:nearest | door:nearest | walk:+x,4 | coc:WhiterunBanneredMare | coc:Riverwood | coc:WhiterunBanneredMare | coc:Riverwood | purge | coc:WhiterunBanneredMare | coc:Riverwood | coc:Whiterun" };

	// [Preload] keep one interior and one exterior area loaded (experiment)
	bool          preloadEnabled{ false };
	std::string   preloadInterior;  // interior cell editor ID
	std::string   preloadExterior;  // exterior cell editor ID: center of the kept area
	std::uint32_t preloadRadius{ 2 };  // 2 -> 5x5 cells, as uGridsToLoad=5
	float         preloadRefreshSeconds{ 2.0f };
	bool          preloadLinkedInteriors{ false };  // sets the game's bPreloadLinkedInteriors in memory
	bool          keepInterior{ true };             // queue / move to the buffer front the interior (AE#18540)
	bool          keepInteriorNearDoorOnly{ true };  // ... only while a door to it is in the loaded cells

	// [Detectors] load doors and windows near the player; off since 0.8.4 (doors are still scanned silently)
	bool          detectorsEnabled{ false };
	bool          detectorsNotify{ true };
	float         doorsIntervalSeconds{ 0.5f };
	std::uint32_t nearestCount{ 5 };
	bool          windowsEnabled{ true };
	float         windowsIntervalSeconds{ 2.0f };
	float         windowRadius{ 3000.0f };  // game units (70 ~ 1 m)
	std::string   windowKeywords{ "window,lattice" };  // comma-separated, case-insensitive: texture or model path
	std::string   windowExclude{ "lod" };

	// [InteriorSim] simulate the kept interior while the player is outside
	std::uint32_t interiorSimStage{ 0 };  // stage at start, 0..6
	float         interiorSimLift{ 1500.0f };
	float         interiorSimStatusSeconds{ 10.0f };
	bool          interiorSimForceWorldActive{ true };
	bool          interiorSimHideDoor{ true };  // hide the doors and the doorway while the interior is rendered (stages 3..5)

	// [Portal] InteriorSim stage 6: the interior drawn by a second camera into the door's quad
	bool          portalEnabled{ true };
	std::uint32_t portalWidth{ 0 };   // render target, pixels; 0 = the game's main target (the screen)
	std::uint32_t portalHeight{ 0 };
	std::uint32_t portalColorSlot{ 17 };  // engine render target slot lent to the portal while it draws (SE kLOCAL_MAP_SWAP)
	std::uint32_t portalDepthSlot{ 9 };   // engine depth target slot (kBOOK_TEXT)
	bool          portalInteriorLights{ true };      // copies of the interior's lights, lighting only the interior
	bool          portalMirrorPlayerLights{ true };  // the player's dynamic lights near the door carried into the interior
	bool          portalInteriorAmbient{ true };     // the interior's ambient and directional light for the pass
	std::uint32_t portalDepthMode{ 2 };              // how the depth buffer is lent to the engine (Portal.cpp DepthMode)

	// [Probe] simulation probe (H11, H12)
	bool          probeEnabled{ true };
	float         probeIntervalSeconds{ 5.0f };
	std::uint32_t probeMaxActors{ 400 };

	[[nodiscard]] static Settings& Get();

	// Must run before logging is initialized, so it does not log anything itself.
	void Load();

	[[nodiscard]] const std::filesystem::path& IniPath() const { return _iniPath; }

	// Creates logDirectory if needed. Falls back to the SKSE default folder when it is
	// empty or cannot be created; a_usedFallback reports that case for a later warning.
	[[nodiscard]] std::filesystem::path ResolveLogDirectory(bool& a_usedFallback) const;

private:
	std::filesystem::path _iniPath;
};

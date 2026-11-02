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
	std::uint32_t startAutoTestKey{ 68 };     // F10
	std::uint32_t snapshotKey{ 87 };          // F11
	std::uint32_t overridesKey{ 65 };         // F7: cycle [Overrides] sets (not F9: that is the game's quickload)
	std::uint32_t detectorsKey{ 64 };         // F6: doors and windows summary on the HUD
	std::uint32_t interiorSimKey{ 66 };       // F8: InteriorSim off / portal
	std::uint32_t toggleTemporalAAKey{ 62 };  // F4: the game's TAA on or off in memory (0.8.20; until 0.8.19 soft particles in the portal)
	std::uint32_t toggleSoftEffectsKey{ 60 }; // F2: soft particles in the portal's pass on or off
	std::uint32_t accessWatchKey{ 61 };       // F3: capture who reads the player's and the camera's position

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
	std::uint32_t interiorSimStage{ 0 };  // stage at start: 0 off, 1 portal (stages 1-5 of 0.8.11: docs/INTERIOR_SIM_STAGES.md)
	float         interiorSimStatusSeconds{ 10.0f };
	bool          interiorSimForceWorldActive{ true };
	bool          interiorSimAttachInterior{ true };  // the portal's interior attached like a grid cell next to the player's (TESObjectCELL::kAttached)

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
	bool          portalLightEffects{ false };       // effects (fire, candles) get the interior's lights
	bool          portalAnimateActors{ true };       // the interior's actors' animation advanced before each pass
	bool          portalSyncActorPositions{ true };  // ... their 3D moved to where the actors are first
	std::uint32_t portalControllerMode{ 1 };         // the interior's controllers: 0 off, 1 selective update, 2 whole tree
	bool          portalFullActorProcessing{ true }; // the interior's actors get the game's full high-process update (AI, movement)
	bool          portalGameAnimatesActors{ true };  // ... and the game's own animation jobs (AE#41466)
	bool          portalSoftEffects{ true };         // soft particles in the portal's pass
	bool          portalIgnoreParticleBudget{ true };  // the interior's emitters ignore the game's particle budget
	bool          portalMasterParticles{ true };       // the pass also culls the master particle systems (candle flames); only at the fence: froze 0.8.21 before RenderWorld
	bool          portalPassAtSceneListFence{ true };  // the pass runs at the scene list fence, not right before RenderWorld
	std::uint32_t portalParticleFacing{ 2 };           // what particle quads face: 0 the game's choice, 1 the portal camera, 2 the player's view through the door
	bool          portalActorsSeen{ true };            // the interior's actors count as seen: no kNotVisible (their height follows the controller)
	bool          portalVirtualCamera{ true };         // the world camera, for the interior's actors, is the portal's camera
	bool          portalSoundThroughDoor{ true };      // the interior's sounds are heard in the street
	bool          portalDistanceThroughDoor{ true };   // GetDistance between the player and the interior's references goes through the door

	// [Watchdog] sees a frozen game from outside while the portal runs
	bool  watchdogEnabled{ true };
	float watchdogHangSeconds{ 6.0f };
	bool  watchdogTerminate{ true };  // end the process after writing the stacks

	// [AccessWatch] hardware breakpoints on the positions the game measures from
	bool          accessWatchEnabled{ true };
	float         accessWatchSeconds{ 3.0f };
	std::uint32_t accessWatchMaxHits{ 20000000 };  // the capture ends early after that many

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

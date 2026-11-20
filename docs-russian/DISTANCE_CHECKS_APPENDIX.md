# Приложение: обращения к позиции игрока и камеры

Файл создан `tools/re/distance_report.py` из результата `tools/re/scan_player_access.py` для 1.6.1170, руками не правится. Разбор и статус — в `docs/DISTANCE_CHECKS.md`.

Прямых читателей: 59. Читателей через вспомогательные функции: 89.

`дист.` — после чтения идут вычитание, квадрат или корень и сравнение, то есть это почти наверняка проверка расстояния. `Что это`: слоты таблиц виртуальных функций и строки из самой функции.

## Прямые чтения

| Функция | Что читает | Что это | Кто вызывает |
|---|---|---|---|
| AE#47314 | player.location ×6, дист. |  | AE#33262 AE#49171 |
| AE#51728 | player.location ×6, дист. |  | AE#51612 AE#51668 AE#51670 AE#51744 |
| AE#15203 | player.location ×3, дист. |  | AE#15217 AE#15230 AE#19575 AE#36215 |
| AE#18889 | player.location ×3, дист. |  | AE#13291 AE#18871 |
| AE#34351 | player.location ×3, дист. | GuideEffect [0x14] |  |
| AE#34355 | player.location ×3, дист. |  | AE#34351 |
| AE#36216 | player.location ×3, дист. |  | AE#22683 |
| AE#36661 | player.location ×3, дист. |  | AE#36561 AE#36590 |
| AE#37286+0x2a | worldCamera.translate ×3, дист. |  |  |
| AE#37361+0x190 | worldCamera.translate ×3, дист. |  |  |
| AE#37690 | worldCamera.translate ×3, дист. |  | AE#36991 AE#38586 |
| AE#38308 | worldCamera.translate ×3, дист. |  | AE#37507 |
| AE#39322 | player.location ×6, дист. |  | AE#37800 AE#37801 AE#37802 AE#37803 |
| AE#40818 | player.location ×3, дист. |  | AE#40447 |
| AE#41470 | player.location ×3, дист. |  | AE#41425 |
| AE#41471 | player.location ×3, дист. |  | AE#41423 AE#41428 |
| AE#41472 | player.location ×3, дист. |  | AE#41424 |
| AE#41473 | player.location ×3, дист. |  | AE#41427 |
| AE#41474 | player.location ×3, дист. |  | AE#41426 |
| AE#43851 | worldCamera.translate ×3, дист. |  | AE#43836 |
| AE#43869 | player.location ×3, дист. | "SpecialIdle_AreaEffect", "CWaterExplosionSplash" | AE#43836 |
| AE#44213 | worldCamera.translate ×3, дист. |  | AE#43806 AE#44123 AE#44205 |
| AE#44217 | player.location ×3, дист. |  | AE#44216 |
| AE#50724 | player.location ×3, дист. | FirstPersonState [0x3] |  |
| AE#51671 | player.location ×3, дист. | EnemyHealth [0x1], "SetPercent", "SetTargetPercent" |  |
| AE#51676 | player.location ×3, дист. | FloatingQuestMarker [0x1] |  |
| AE#56860 | player.location ×3, дист. |  |  |
| AE#32217 | player.location ×4, дист. |  | AE#32081 AE#36991 AE#44203 |
| AE#40807 | player.location ×2, дист. |  | AE#40438 AE#40808 AE#40818 |
| AE#40817 | player.location ×2, дист. |  | AE#40818 |
| AE#53111 | player.location ×3, дист. | "WorldMap.MarkerData", "RefreshMarkers", "SetSelectedMarker" | AE#53101 AE#53118 |
| AE#17927 | player.location ×3 |  |  |
| AE#19384 | worldCamera.translate ×3 |  | AE#18894 AE#18900 AE#19204 |
| AE#21550 | player.location ×1 | "VATS Target Height >> %0.2f" |  |
| AE#22643 | player.location ×3 |  |  |
| AE#32089 | player.location ×3 |  | AE#13291 |
| AE#32164 | player.location ×2 |  |  |
| AE#32179 | player.location ×2, worldCamera.translate ×3 | "Data\Textures\Cubemaps\QuickSky_e.dd" | AE#32054 |
| AE#32218 | player.location ×2 |  | AE#36991 |
| AE#33120 | player.location ×3 | DragonCameraState [0xe] |  |
| AE#35639 | player.location ×3 |  | AE#35638 AE#35941 |
| AE#35651 | player.location ×3 |  |  |
| AE#36581 | player.location ×3 |  | AE#36564 |
| AE#37618 | player.location ×3 |  | AE#36520 AE#37621 AE#37648 AE#39637 |
| AE#39391 | player.location ×1 |  | AE#28316 |
| AE#39648 | player.location ×1 |  | AE#39637 AE#39649 AE#39655 AE#39671 |
| AE#39673 | player.location ×3 |  | AE#39637 |
| AE#40445 | player.location ×3 | "FastTravel: Could not compute path l" | AE#40438 |
| AE#40607+0x2a | player.location ×3 |  |  |
| AE#40725 | player.location ×3 |  | AE#40359 |
| AE#40804 | player.location ×1 |  | AE#40722 AE#40807 AE#40811 AE#40817 |
| AE#40821 | player.location ×3 |  | AE#40543 AE#40726 AE#40825 |
| AE#40826 | player.location ×3 |  |  |
| AE#40829 | player.location ×3 |  | AE#40713 |
| AE#47272 | player.location ×1 |  | AE#34406 AE#38608 AE#42350 |
| AE#48834 | player.location ×1 |  |  |
| AE#50722 | player.location ×3 | FirstPersonState [0x1] |  |
| AE#51744 | player.location ×3 |  | AE#51612 |
| AE#55494 | player.location ×3 |  |  |

## Чтения через вспомогательные функции

| Функция | Через что | Что это | Кто вызывает |
|---|---|---|---|
| AE#13328+0x84 | AE#40437 |  |  |
| AE#15768 | AE#40566 | "Sound: ", "Enum: StopSounds ", "Enum: HitShader" | AE#20119 AE#20121 |
| AE#17927 | AE#37313 |  |  |
| AE#22088 | AE#30635 | "Worldspace %s (%08x)", "Interior cell %s (%08x)", "Could not find navmesh path from Doo" |  |
| AE#22873 | AE#40437 |  |  |
| AE#22875+0x8e | AE#40437 |  |  |
| AE#22876+0x80 | AE#40437 |  |  |
| AE#22974 | AE#22279 | "Target index out of bounds.  Must be", "No current targets" |  |
| AE#24716 | AE#38706 | TESNPC [0x4c], "%s's %s", "%s - %s" |  |
| AE#25063 | AE#19824 |  | AE#25048 |
| AE#32160 | AE#37771 |  | AE#32174 |
| AE#34143 | AE#41233 | ActorMagicCaster [0x1d] |  |
| AE#34147+0x2b | AE#40565 |  |  |
| AE#34299 | AE#40565 | DisguiseEffect::DetectionChecker [0x5] |  |
| AE#34406 | AE#34458 AE#47272 | ActorMagicCaster [0x2], MagicCaster [0x2] |  |
| AE#35046 | AE#37743 | TelekinesisEffect [0x14] |  |
| AE#35052+0x223 | AE#37743 |  |  |
| AE#35294 | AE#37765 | " %s  walkaway topic is running" | AE#19660 AE#35273 AE#35282 AE#37474 |
| AE#35639 | AE#40562 |  | AE#35638 AE#35941 |
| AE#35651 | AE#40562 |  |  |
| AE#35652 | AE#19032 |  | AE#35642 |
| AE#36216 | AE#37710 |  | AE#22683 |
| AE#36549+0xf | AE#19411 AE#40438 |  |  |
| AE#36564 | AE#40438 AE#40724 | "Hitched from shader compile" | AE#36550 |
| AE#36580 | AE#41246 |  |  |
| AE#36581 | AE#19411 AE#40450 |  | AE#36564 |
| AE#36582 | AE#40724 |  |  |
| AE#36601 | AE#19032 |  | AE#36550 AE#36564 AE#416418 |
| AE#36642 | AE#19411 AE#40438 |  |  |
| AE#37284 | AE#19824 |  | AE#441575 AE#19232 AE#19326 AE#19742 |
| AE#37289 | AE#38561 | Actor [0x96], Character [0x96] |  |
| AE#37422 | AE#37765 AE#39361 |  | AE#14864 AE#17887 AE#22681 AE#37521 |
| AE#37427 | AE#39361 |  | AE#17702 AE#17922 AE#22434 AE#38626 |
| AE#37627+0x64 | AE#19824 AE#37773 |  |  |
| AE#37896 | AE#19824 | Actor [0x10e], Character [0x10e], "'%s' (%08X) was killed by '%s' (%08X", "'%s' (%08X) has died with no attacke" |  |
| AE#38009 | AE#19824 | Actor [0x122], Character [0x122] |  |
| AE#38012 | AE#38008 |  | AE#38009 |
| AE#39089 | AE#40522 |  |  |
| AE#39219 | AE#19824 |  | AE#39108 AE#39209 |
| AE#39268 | AE#38697 |  | AE#39637 AE#39638 |
| AE#39632 | AE#19824 |  | AE#39637 |
| AE#39637 | AE#19824 |  | AE#39108 |
| AE#39647 | AE#30635 |  | AE#39637 |
| AE#39648 | AE#19824 |  | AE#39637 AE#39649 AE#39655 AE#39671 |
| AE#39652 | AE#19824 AE#38561 |  | AE#39637 AE#39638 |
| AE#39673 | AE#37250 |  | AE#39637 |
| AE#39695 | AE#38561 |  | AE#39361 |
| AE#39762 | AE#38697 AE#39158 | RunActionAnimationLoadedCallback [0x1] |  |
| AE#39923 | AE#38697 AE#39158 |  | AE#28316 AE#29150 AE#37330 AE#39268 |
| AE#40435 | AE#19824 |  | AE#42537 |
| AE#40438 | AE#38697 AE#40445 |  | AE#36564 AE#36642 AE#40437 |
| AE#40445 | AE#30635 | "FastTravel: Could not compute path l" | AE#40438 |
| AE#40446 | AE#19032 |  | AE#35652 |
| AE#40517 | AE#30635 |  | AE#23940 AE#23941 AE#25129 AE#25130 |
| AE#40654 | AE#37765 |  | AE#51141 |
| AE#40745 | AE#19032 |  | AE#35652 AE#40438 AE#40445 |
| AE#40847 | AE#19824 |  | AE#40447 |
| AE#41233 | AE#38608 AE#38639 AE#44284 |  | AE#34143 AE#40574 AE#42436 |
| AE#41334 | AE#19824 |  | AE#22206 AE#37427 AE#37679 AE#38626 |
| AE#41348 | AE#19824 AE#37433 |  |  |
| AE#41402 | AE#19824 |  | AE#21743 AE#40443 AE#40444 |
| AE#41403 | AE#19824 |  |  |
| AE#41407 | AE#38561 |  | AE#24524 |
| AE#41442 | AE#19824 |  | AE#41296 |
| AE#42350 | AE#40574 AE#47272 |  | AE#37257 AE#37507 AE#37946 AE#38543 |
| AE#42436 | AE#41233 |  |  |
| AE#42742 | AE#19784 |  |  |
| AE#44200 | AE#41248 |  | AE#38639 AE#44222 |
| AE#44673 | AE#37547 |  | AE#38586 AE#38626 AE#40574 AE#40590 |
| AE#44803 | AE#19784 |  | AE#44703 |
| AE#46888 | AE#44745 |  | AE#46887 |
| AE#47272 | AE#37768 |  | AE#34406 AE#38608 AE#42350 |
| AE#47297 | AE#41248 |  | AE#29383 AE#34458 AE#38585 AE#38602 |
| AE#47314 | AE#47309 |  | AE#33262 AE#49171 |
| AE#48839 | AE#19784 |  |  |
| AE#51126 | AE#39395 | ContainerMenu [0x4], "ToggleMenuFade", "AttemptEquip", "XButtonPress" |  |
| AE#51543 | AE#39395 | "UIFavorite" | AE#52247 AE#52248 AE#56858 |
| AE#51548 | AE#39395 | "_root.GetSelectedIndex", "UIFavorite" | AE#51534 |
| AE#51848 | AE#39395 | InventoryMenu [0x4], "_root.GetInventoryItemList", "entryList", "ToggleMenuFade" |  |
| AE#52346 | AE#19165 | RaceSexMenu [0x5], "HideLoadingIcon" |  |
| AE#52353 | AE#39395 |  |  |
| AE#52385 | AE#37605 | RaceSexMenu[+0x30] [0x3] |  |
| AE#52387+0x23 | AE#37605 |  |  |
| AE#52392 | AE#39395 |  |  |
| AE#52465+0x67 | AE#37605 |  |  |
| AE#52638 | AE#19784 |  | AE#52629 |
| AE#53021 | AE#30635 |  | AE#53101 |
| AE#55367 | AE#40437 | "Cannot center on a cell with no name" |  |
| AE#55427 | AE#40437 | "LatentPlayerMover could not find des" | AE#55430 |

## Вспомогательные функции

| Функция | Аргумент с позицией | Мест, где туда передан игрок |
|---|---:|---:|
| AE#19824 | 2 | 17 |
| AE#19824 | 1 | 8 |
| AE#30635 | 3 | 7 |
| AE#39395 | 2 | 6 |
| AE#40437 | 1 | 6 |
| AE#19032 | 2 | 4 |
| AE#19784 | 1 | 4 |
| AE#38561 | 2 | 4 |
| AE#38697 | 1 | 4 |
| AE#19411 | 2 | 3 |
| AE#37605 | 1 | 3 |
| AE#37765 | 2 | 3 |
| AE#40438 | 1 | 3 |
| AE#37743 | 1 | 2 |
| AE#38639 | 1 | 2 |
| AE#39158 | 2 | 2 |
| AE#39361 | 2 | 2 |
| AE#40562 | 1 | 2 |
| AE#40565 | 1 | 2 |
| AE#40724 | 1 | 2 |
| AE#41233 | 1 | 2 |
| AE#41248 | 1 | 2 |
| AE#44284 | 2 | 2 |
| AE#47272 | 1 | 2 |
| AE#19165 | 2 | 1 |
| AE#22279 | 1 | 1 |
| AE#34458 | 2 | 1 |
| AE#37250 | 2 | 1 |
| AE#37313 | 1 | 1 |
| AE#37433 | 2 | 1 |
| AE#37547 | 1 | 1 |
| AE#37710 | 1 | 1 |
| AE#37768 | 1 | 1 |
| AE#37771 | 1 | 1 |
| AE#37773 | 1 | 1 |
| AE#38008 | 2 | 1 |
| AE#38608 | 1 | 1 |
| AE#38706 | 1 | 1 |
| AE#40445 | 1 | 1 |
| AE#40450 | 1 | 1 |
| AE#40522 | 1 | 1 |
| AE#40566 | 1 | 1 |
| AE#40574 | 1 | 1 |
| AE#41246 | 1 | 1 |
| AE#44745 | 2 | 1 |
| AE#47309 | 1 | 1 |

## Настройки расстояния и кто их читает

Создано `tools/re/scan_distance_settings.py`. `нет объекта`: имя настройки пишется в объект кодом при старте, адрес этим способом не найден.

| Настройка | Значение | Кто читает |
|---|---:|---|
| bAutoAimBasedOnDistance | 0 | AE#41246 |
| bAutoViewDistance:Display | 0 | AE#76751 |
| bDebugCombatAttackRange:Combat | 0 | не читается напрямую |
| bVATSForceRanged:VATS | 0 | AE#41233 |
| bVATSRangedSelective:VATS | 1 | AE#41233 |
| f1stPersonFarDepthRange:Display | 0.01 | AE#107129 AE#107139 |
| f1stPersonFarDepthRangeControlDriven:Display | 0.1 | AE#107129 AE#107139 |
| fActionPointsAttackRanged | 1 | не читается напрямую |
| fActivatePickRadius:Interface | 16 | AE#26125 |
| fAIAcquireObjectDistance | 4000 | AE#39206 AE#40051 AE#40059 AE#39201 AE#39357 AE#39655 |
| fAIAimBlockedHalfCircleRadius | 250 | AE#29383 |
| fAIBestHeadTrackDistance | 500 | не читается напрямую |
| fAICombatNoAreaEffectAllyDistance | 350 | не читается напрямую |
| fAIDialogueDistance | 170 | AE#28385 AE#39658 |
| fAIDistanceRadiusMinLocation | 100 | AE#27316 AE#27317 AE#27318 AE#27335 AE#27336 AE#27350 и ещё 35 |
| fAIDistanceTeammateDrawWeapon | 2000 | AE#39637 |
| fAIDodgeVerticalRangedAttackMult | 0.1 | не читается напрямую |
| fAIEscortFastTravelMaxDistFromPath | 5000 | AE#37939 |
| fAIEscortWaitDistanceExterior | 2 | AE#39209 AE#39539 AE#39654 AE#40072 AE#40087 AE#40088 |
| fAIEscortWaitDistanceInterior | 512 | AE#39209 AE#39539 AE#39654 AE#40072 AE#40087 AE#40088 |
| fAIExplosiveWeaponRangeMult | 1 | AE#0x4174c8 |
| fAIExteriorSpectatorDistance | 450 | не читается напрямую |
| fAIFindBedChairsDistance | 8000 | AE#40062 |
| fAIFurnitureDestinationRadius | 150 | AE#27316 AE#27317 AE#27318 AE#27335 AE#27336 AE#0x4424d3 и ещё 1 |
| fAIHorseSearchDistance | 3000 | AE#38045 AE#38706 |
| fAIIdleAnimationDistance | 2000 | AE#39281 AE#39649 |
| fAIIdleAnimationLargeCreatureDistanceMult | 3 | AE#39281 |
| fAIInDialogueModeWithPlayerDistance | 500 | не читается напрямую |
| fAIInteriorSpectatorDistance | 200 | не читается напрямую |
| fAIMarkerDestinationRadius | 50 | AE#28316 AE#39644 AE#27318 AE#27335 AE#27336 AE#29149 и ещё 8 |
| fAIMaxAngleRangeMovingToStartSceneDialogue | 80 | AE#0x6623f1 |
| fAIMaxHeadTrackDistance | 400 | AE#37617 AE#37919 AE#38014 |
| fAIMaxHeadTrackDistanceFromPC | 2000 | AE#38009 AE#37261 AE#0x66ab20 AE#37363 AE#37919 AE#38015 |
| fAIMaxLargeCreatureHeadTrackDistance | 12000 | AE#37919 AE#38009 AE#37261 AE#0x66ab20 AE#37363 AE#37617 и ещё 2 |
| fAIMaxSmileDistance | 200 | не читается напрямую |
| fAIMinAngleRangeToStartSceneDialogue | 20 | AE#0x6623f1 |
| fAIMinGreetingDistance | 150 | AE#39632 |
| fAIMoveDistanceToRecalcFollowPath | 300 | AE#28316 AE#28644 AE#39639 AE#39644 AE#39648 AE#40074 и ещё 2 |
| fAIMoveDistanceToRecalcTravelPath | 300 | AE#29198 AE#29199 |
| fAIMoveDistanceToRecalcTravelToActor | 100 | AE#29198 AE#29199 |
| fAIPursueDistanceLineOfSight | 2500 | AE#28644 AE#39639 |
| fAIRandomizeInitialLocationMinRadius | 200 | AE#0x480cb4 |
| fAIRangedWeaponMult | 5 | AE#0x4174a3 |
| fAISocialRadiusToTriggerConversation | 500 | AE#39736 |
| fAISocialRadiusToTriggerConversationInterior | 500 | AE#39736 |
| fAISpectatorShutdownDistance | 1000 | AE#39342 |
| fAISpectatorThreatDistExplosion | 512 | AE#42647 AE#42659 |
| fAISpectatorThreatDistMelee | 512 | AE#42647 |
| fAISpectatorThreatDistMine | 256 | AE#42647 |
| fAISpectatorThreatDistRanged | 256 | AE#42647 |
| fAIUpdateMovementRestrictionsDistance | 48 | не читается напрямую |
| fAIUseWeaponDistance | 15000 | не читается напрямую |
| fAIWanderDefaultMinDist | 200 | AE#28991 AE#29438 |
| fAmbushOverRideRadiusforPlayerDetection | 1000 | AE#39652 |
| fAnimInterpFarDist:Animation | 800 | AE#63587 |
| fAnimInterpNearDist:Animation | 400 | AE#63587 |
| fArrowMaxDistance | 2000 | не читается напрямую |
| fArrowMinDistanceForTrails | 300 | AE#43704 |
| fArrowOptimalDistance | 1000 | не читается напрямую |
| fAutoAimMaxDistance | 1800 | AE#41246 |
| fAutoDoorActivateDistance | 350 | AE#17934 AE#0x27f969 |
| fAutoDoorDisplayDistance | 700 | AE#17934 |
| fAvoidanceDistanceRadiusMult:Pathfinding | 1 | AE#90758 |
| fAvoidNodeRadiusAdd:Pathfinding | 11 | AE#39322 AE#48363 AE#92193 |
| fAvoidPlayerDistance | 100 | AE#39663 |
| fBlockLevel0Distance:TerrainManager | 60000 | AE#22838 AE#31859 |
| fBlockLevel1Distance:TerrainManager | 90000 | AE#22838 AE#0x511db8 |
| fBlockMaximumDistance:TerrainManager | 250000 | AE#22838 AE#0x511dee |
| fBoneLODDistanceScale:Animation | 1 | AE#63575 |
| fBookDistance:Interface | 110 | AE#51049 |
| fBookLight2Radius:Interface | 400 | AE#52746 |
| fBookLightRadius:Interface | 400 | AE#52746 |
| fBumpReactionIdealMoveDist | 125 | AE#39664 |
| fBumpReactionMinMoveDist | 75 | AE#39664 |
| fCameraCasterFadeSittingRadius:HAVOK | 45 | AE#33008 |
| fCameraDist:RagdollAnim | 1000 | не читается напрямую |
| fCameraShakeDistFadeDelta | 2500 | AE#33012 |
| fCameraShakeDistFadeStart | 2000 | AE#33012 |
| fCameraShakeDistMin | 300 | AE#33012 |
| fCameraShakeExplosionDistMult | 0.001 | AE#43869 |
| fCharControllerWarpDistSqr:HAVOK | 6e+06 | не читается напрямую |
| fCheckPositionFallDistance | 72 | AE#37657 |
| fClosetoPlayerDistance | 800 | не читается напрямую |
| fCloudLevel0Distance:Clouds | 16384 | AE#33072 AE#33074 AE#0x553d50 |
| fCloudLevel1Distance:Clouds | 32768 | AE#33072 AE#33074 AE#33083 |
| fCloudLevel2Distance:Clouds | 262144 | AE#33072 AE#33074 AE#0x553d30 |
| fCloudNearFadeDistance:Clouds | 9000 | AE#33072 |
| fCollisionRecoveryMinDist:Camera | 4000 | AE#50911 AE#50920 |
| fCombatAbsoluteMaxRangeMult | 4 | не читается напрямую |
| fCombatAcquireWeaponAvoidTargetRadius | 256 | AE#47587 |
| fCombatAcquireWeaponCloseDistanceMax | 768 | AE#0x8dd63f |
| fCombatAcquireWeaponCloseDistanceMin | 256 | AE#0x8dd63f |
| fCombatAcquireWeaponDisarmedDistanceMax | 2048 | AE#0x8dd5c7 |
| fCombatAcquireWeaponDisarmedDistanceMin | 512 | AE#0x8dd5c7 |
| fCombatAcquireWeaponFindAmmoDistance | 384 | AE#47587 |
| fCombatAcquireWeaponMinimumTargetDistance | 384 | AE#47583 |
| fCombatAcquireWeaponRangedDistanceMax | 2048 | AE#0x8dd63f |
| fCombatAcquireWeaponRangedDistanceMin | 512 | AE#0x8dd63f |
| fCombatAcquireWeaponReachDistance | 48 | AE#47587 AE#47591 |
| fCombatAcquireWeaponSearchRadiusBuffer | 384 | AE#47587 |
| fCombatAcquireWeaponTargetDistanceCheck | 256 | AE#47587 |
| fCombatAcquireWeaponUnarmedDistanceMax | 2048 | AE#0x8dd63f |
| fCombatAcquireWeaponUnarmedDistanceMin | 256 | AE#0x8dd63f |
| fCombatActiveCombatantAttackRangeDistance | 512 | AE#41261 |
| fCombatAdvanceInnerRadiusMax | 256 | AE#50643 |
| fCombatAdvanceInnerRadiusMid | 0 | AE#50643 |
| fCombatAdvanceInnerRadiusMin | 0 | AE#50643 |
| fCombatAdvanceOuterRadiusMax | 512 | AE#50643 |
| fCombatAdvanceOuterRadiusMid | 256 | AE#50643 |
| fCombatAdvanceOuterRadiusMin | 128 | AE#50643 |
| fCombatAdvanceRadiusStaggerMult | 0 | AE#50643 |
| fCombatAimProjectileGroundMinRadius | 128 | AE#44396 |
| fCombatAnticipatedLocationCheckDistance | 32 | AE#0x854046 |
| fCombatApproachTargetSlowdownDistance | 96 | AE#50532 |
| fCombatApproachTargetSprintStopMovingRange | 0 | AE#50532 |
| fCombatApproachTargetSprintStopRange | 768 | AE#50532 |
| fCombatAreaHoldPositionMinimumRadius | 256 | AE#33259 |
| fCombatAreaStandardAttackedRadius | 8192 | AE#44469 |
| fCombatAreaStandardCheckViewConeDistanceMax | 4096 | AE#44469 |
| fCombatAreaStandardCheckViewConeDistanceMin | 1024 | AE#44469 |
| fCombatAreaStandardFlyingRadiusMult | 2 | AE#44469 |
| fCombatAreaStandardRadius | 4096 | AE#44468 AE#44469 |
| fCombatAttackAllowedOverrunDistance | 128 | AE#49171 |
| fCombatAttackAnticipatedDistanceMin | -64 | не читается напрямую |
| fCombatAttackCheckTargetRangeDistance | 512 | AE#49170 |
| fCombatAttackMovingAttackDistance | 32 | AE#49171 |
| fCombatAttackStationaryAttackDistance | 5 | AE#49171 |
| fCombatBackoffMinDistanceMult | 0.75 | AE#47920 |
| fCombatBlockStartDistanceMax | 192 | AE#50679 |
| fCombatBlockStartDistanceMin | 32 | AE#50679 |
| fCombatBlockStopDistanceMax | 384 | AE#50680 |
| fCombatBlockStopDistanceMin | 128 | AE#50680 |
| fCombatCastImmediateOffensiveMagicHoldTimeMinDistance | 256 | AE#50668 |
| fCombatChangeProcessFaceTargetDistance | 256 | AE#33219 |
| fCombatCircleDistanceMax | 1024 | AE#47917 |
| fCombatCircleDistantChanceMax | 0.75 | AE#50650 |
| fCombatCircleDistantChanceMin | 0.25 | AE#50650 |
| fCombatCircleMinDistanceMult | 0.5 | AE#47917 |
| fCombatCircleMinDistanceRadiusMult | 3 | AE#50648 |
| fCombatCircleMinMovementDistance | 64 | AE#47916 AE#47924 |
| fCombatCloseRangeTrackTargetDistance | 768 | AE#47898 AE#48367 AE#48371 |
| fCombatCoverAttackOffsetDistance | 16 | AE#44620 |
| fCombatCoverAvoidTargetRadius | 256 | AE#49804 |
| fCombatCoverEdgeOffsetDistance | 16 | AE#44620 |
| fCombatCoverLedgeOffsetDistance | 128 | AE#44620 |
| fCombatCoverMaxRangeMult | 1.5 | не читается напрямую |
| fCombatCoverMidPointMaxRangeBuffer | 256 | AE#44618 |
| fCombatCoverMinimumActiveRange | 512 | AE#0x8dc6b8 |
| fCombatCoverMinimumRange | 768 | AE#0x8dc650 |
| fCombatCoverRangeMaxActiveMult | 1.25 | AE#0x8dc6c4 |
| fCombatCoverRangeMaxBufferDistance | 768 | AE#0x8dc630 AE#0x8dc690 |
| fCombatCoverRangeMinActiveMult | 0.5 | AE#0x8dc6b0 |
| fCombatCoverRangeMinBufferDistance | 512 | AE#0x8dc640 AE#0x8dc6a0 |
| fCombatCoverSearchDistanceMax | 3072 | AE#50655 |
| fCombatCoverSearchDistanceMin | 1536 | AE#50655 |
| fCombatCoverSecondaryThreatMinDistance | 512 | AE#44619 |
| fCombatCoverWaitLookOffsetDistance | 32 | AE#49812 |
| fCombatCurrentWeaponAbsoluteMaxRangeMult | 1.25 | не читается напрямую |
| fCombatDetectionLostCheckNoticedDistance | 256 | AE#44725 |
| fCombatDetectionLostRemoveDistance | 4096 | AE#44799 |
| fCombatDetectionLostRemoveDistanceTime | 60 | AE#44799 |
| fCombatDetectionLowDetectionDistance | 4096 | AE#44786 AE#0x80c260 AE#44804 |
| fCombatDetectionLowPriorityDistance | 2048 | AE#44725 |
| fCombatDetectionNoticedDistanceLimit | 768 | AE#44725 |
| fCombatDetectionVeryLowPriorityDistance | 4096 | AE#44725 |
| fCombatDialogueAllyKilledDistanceMult | 0.75 | не читается напрямую |
| fCombatDialogueAttackDistanceMult | 0.33 | не читается напрямую |
| fCombatDialogueAvoidThreatDistanceMult | 1 | не читается напрямую |
| fCombatDialogueBashDistanceMult | 0.33 | не читается напрямую |
| fCombatDialogueBleedoutDistanceMult | 0.75 | не читается напрямую |
| fCombatDialogueBlockDistanceMult | 0.33 | не читается напрямую |
| fCombatDialogueDeathDistanceMult | 1 | не читается напрямую |
| fCombatDialogueFleeDistanceMult | 1 | не читается напрямую |
| fCombatDialogueGroupStrategyDistanceMult | 1 | не читается напрямую |
| fCombatDialogueHitDistanceMult | 0.5 | не читается напрямую |
| fCombatDialoguePowerAttackDistanceMult | 0.5 | не читается напрямую |
| fCombatDialogueTauntDistanceMult | 0.75 | не читается напрямую |
| fCombatDistance | 141 | AE#0x418243 AE#34458 |
| fCombatDistanceMin | 400 | AE#26439 |
| fCombatDiveBombSlowDownDistance | 4096 | AE#48859 |
| fCombatDodgeBufferDistance | 64 | AE#47802 |
| fCombatEffectiveDistanceAnticipateTime | 1 | AE#50693 AE#0x8dd3c8 |
| fCombatFallbackDistanceMax | 256 | AE#47909 AE#50645 |
| fCombatFallbackDistanceMin | 96 | AE#50645 |
| fCombatFallbackMinMovementDistance | 64 | AE#47908 |
| fCombatFindAllyAttackLocationAllyRadius | 64 | AE#48139 |
| fCombatFindAllyAttackLocationDistanceMax | 4096 | AE#48139 |
| fCombatFindAllyAttackLocationDistanceMin | 256 | AE#48139 |
| fCombatFindAttackLocationAvoidTargetRadius | 384 | AE#48135 |
| fCombatFindAttackLocationDistance | 1024 | AE#48135 |
| fCombatFindLateralAttackLocationDistance | 512 | AE#48131 |
| fCombatFlankingDirectionDistanceMult | 0.2 | AE#48392 |
| fCombatFlankingDistanceMax | 1024 | AE#50651 |
| fCombatFlankingDistanceMin | 0 | AE#50651 |
| fCombatFlankingGoalAngleFarMaxDistance | 1024 | AE#48381 AE#48394 |
| fCombatFlankingGoalAngleFarMinDistance | 384 | AE#48381 AE#48394 |
| fCombatFlankingGoalCheckDistanceMax | 512 | AE#48392 |
| fCombatFlankingGoalCheckDistanceMin | 256 | AE#48392 |
| fCombatFlankingGoalCheckDistanceMult | 0.75 | AE#48392 |
| fCombatFlankingNearDistance | 384 | AE#48381 AE#48394 |
| fCombatFlankingStalkRange | 256 | AE#48357 AE#48363 AE#48367 AE#48372 |
| fCombatFlankingStepDistanceMax | 384 | AE#48392 |
| fCombatFlankingStepDistanceMin | 96 | AE#48392 |
| fCombatFlankingStepDistanceMult | 0.33 | AE#48392 |
| fCombatFleeAllyDistanceMax | 4096 | AE#48543 |
| fCombatFleeAllyDistanceMin | 1536 | AE#48543 |
| fCombatFleeAllyRadius | 64 | AE#48543 |
| fCombatFleeCoverMinDistance | 1024 | AE#48551 |
| fCombatFleeCoverSearchRadius | 3072 | AE#48551 |
| fCombatFleeDistanceExterior | 4096 | AE#48555 |
| fCombatFleeDistanceInterior | 2048 | AE#48555 |
| fCombatFleeDoorDistanceMax | 1024 | AE#48547 |
| fCombatFleeDoorTargetCheckDistance | 128 | AE#48547 |
| fCombatFleeTargetAvoidRadius | 256 | AE#48542 |
| fCombatFleeTargetGatherRadius | 3072 | AE#48542 |
| fCombatFlightEffectiveDistance | 512 | AE#50693 |
| fCombatFlightMinimumRange | 1536 | AE#0x8dd32c |
| fCombatFlyingAttackTargetDistanceThreshold | 256 | AE#48847 |
| fCombatFollowRadiusBase | 512 | не читается напрямую |
| fCombatFollowRadiusMin | 512 | не читается напрямую |
| fCombatFollowRadiusMult | 3 | не читается напрямую |
| fCombatFollowSneakFollowRadius | 512 | не читается напрямую |
| fCombatGuardFollowBufferDistance | 384 | не читается напрямую |
| fCombatGuardRadiusMin | 384 | не читается напрямую |
| fCombatGuardRadiusMult | 1.5 | не читается напрямую |
| fCombatHideCheckViewConeDistanceMax | 4096 | AE#46958 |
| fCombatHideCheckViewConeDistanceMin | 1024 | AE#46958 |
| fCombatHideFailedTargetDistance | 128 | AE#48560 |
| fCombatHideFailedTargetLOSDistance | 384 | AE#48560 |
| fCombatInventoryDesiredRangeScoreMultMax | 10 | AE#50694 |
| fCombatInventoryDesiredRangeScoreMultMid | 2 | AE#50694 |
| fCombatInventoryDesiredRangeScoreMultMin | 1.5 | AE#50694 |
| fCombatInventoryMaxRangeEquippedBonus | 384 | AE#0x80fa38 AE#44920 |
| fCombatInventoryMaxRangeScoreMult | 0.1 | AE#0x80fa38 AE#44920 |
| fCombatInventoryMeleeEquipRange | 256 | AE#45011 |
| fCombatInventoryMinRangeScoreMult | 0.1 | AE#0x80fa38 AE#44920 |
| fCombatInventoryMinRangeUnequippedBonus | 256 | AE#0x80fa38 AE#44920 |
| fCombatInventoryOptimalRangePercent | 0.75 | AE#45010 AE#0x81883a AE#45041 AE#45060 AE#0x819a6a |
| fCombatInventoryRangedScoreMult | 2 | AE#45033 AE#45084 AE#0x819d72 |
| fCombatInventoryShieldEquipRange | 256 | AE#45042 |
| fCombatInventoryTorchEquipRange | 256 | AE#45061 |
| fCombatIronSightsDistance | 512 | не читается напрямую |
| fCombatIronSightsRangeMult | 5 | не читается напрямую |
| fCombatLandingAvoidActorRadius | 64 | AE#48872 |
| fCombatLandingSearchDistance | 6000 | AE#48854 |
| fCombatLandingZoneDistance | 6000 | AE#48854 |
| fCombatLocationTargetRadiusMin | 10 | не читается напрямую |
| fCombatLowMaxAttackDistance | 2048 | AE#49049 |
| fCombatMagicArmorDistanceMax | 512 | AE#45418 |
| fCombatMagicArmorDistanceMin | 256 | AE#45416 |
| fCombatMagicBoundItemDistance | 1024 | AE#45412 |
| fCombatMagicCloakDistanceMax | 384 | AE#45400 |
| fCombatMagicCloakDistanceMin | 128 | AE#45398 AE#45399 |
| fCombatMagicDisarmDistance | 256 | AE#45393 AE#45394 |
| fCombatMagicInvisibilityDistance | 1024 | AE#45408 |
| fCombatMagicParalyzeDistance | 384 | AE#45443 |
| fCombatMagicReanimateDistance | 1024 | AE#45439 |
| fCombatMagicStaggerDistance | 256 | AE#45389 |
| fCombatMagicWardAttackRangeDistance | 384 | AE#45361 |
| fCombatMaintainOptimalDistanceMaxAngle | 20 | AE#49465 |
| fCombatMaintainRangeDistanceMin | 64 | AE#49464 |
| fCombatMaximumOptimalRangeMax | 1536 | AE#50704 |
| fCombatMaximumOptimalRangeMid | 1280 | AE#50704 |
| fCombatMaximumOptimalRangeMin | 1024 | AE#50704 |
| fCombatMaximumProjectileRange | 4096 | AE#45367 AE#47332 |
| fCombatMaximumRange | 4096 | AE#44899 AE#44835 AE#44846 AE#47332 AE#47338 |
| fCombatMeleeTrackTargetDistanceMax | 512 | AE#49167 |
| fCombatMeleeTrackTargetDistanceMin | 128 | AE#49167 |
| fCombatMinEngageDistance | 64 | не читается напрямую |
| fCombatMoveToActorBufferDistance | 64 | не читается напрямую |
| fCombatMusicNearCombatInnerRadius | 2500 | AE#46888 |
| fCombatMusicNearCombatOuterRadius | 3500 | AE#46888 |
| fCombatOptimalRangeMaxBufferDistance | 512 | AE#0x8ddbdb |
| fCombatOptimalRangeMinBufferDistance | 256 | AE#0x8ddbcb |
| fCombatOrbitDistance | 4000 | AE#48826 AE#48850 |
| fCombatPathingGoalRayCastPathDistance | 512 | AE#50076 |
| fCombatPathingIncompletePathMinDistance | 64 | AE#50078 AE#50085 |
| fCombatPathingRefLocationUpdateDistance | 32 | AE#50015 |
| fCombatPathingRefLocationUpdateTimeDistanceMax | 4096 | AE#50696 |
| fCombatPathingRefLocationUpdateTimeDistanceMin | 1024 | AE#50696 |
| fCombatPathingStartRayCastPathDistance | 512 | AE#50076 AE#50083 AE#50084 |
| fCombatPathingStraightPathCheckDistance | 384 | AE#47314 |
| fCombatPathingStraightRayCastPathDistance | 1024 | AE#50075 |
| fCombatProjectileMaxRangeMult | 0.9 | AE#47332 |
| fCombatProjectileMaxRangeOptimalMult | 0.85 | не читается напрямую |
| fCombatRadiusMinMult | 0.9 | не читается напрямую |
| fCombatRangedAimVariance | 0.9 | AE#33135 |
| fCombatRangedAttackChanceLastAttackBonus | 0.5 | AE#50700 |
| fCombatRangedAttackChanceLastAttackBonusTime | 10 | AE#50700 |
| fCombatRangedAttackChanceMax | 1 | AE#50700 |
| fCombatRangedAttackChanceMin | 0.05 | AE#50700 |
| fCombatRangedAttackHoldTimeAbsoluteMin | 0.1 | AE#50701 |
| fCombatRangedAttackHoldTimeMax | 1.5 | AE#50701 |
| fCombatRangedAttackHoldTimeMin | 0.25 | AE#50701 |
| fCombatRangedAttackHoldTimeMinDistance | 256 | AE#50701 |
| fCombatRangedAttackMaximumHoldTime | 10 | AE#33145 |
| fCombatRangedDistance | 1024 | AE#44383 AE#44473 AE#45033 AE#45084 AE#0x819d69 AE#46958 и ещё 6 |
| fCombatRangedMinimumRange | 256 | AE#47332 |
| fCombatRangedProjectileFiringArc | 30 | AE#33141 AE#33145 AE#45367 |
| fCombatRangedStandoffTimer | 6 | не читается напрямую |
| fCombatSearchCenterRadius | 128 | AE#49520 |
| fCombatSearchCheckDestinationDistanceMax | 512 | AE#49508 |
| fCombatSearchCheckDestinationDistanceMid | 384 | AE#49508 |
| fCombatSearchCheckDestinationDistanceMin | 256 | AE#49508 |
| fCombatSearchDoorDistance | 384 | AE#49524 |
| fCombatSearchDoorDistanceLow | 4096 | AE#49524 |
| fCombatSearchDoorSearchRadius | 512 | AE#49524 |
| fCombatSearchExteriorRadiusMax | 2048 | AE#44763 |
| fCombatSearchExteriorRadiusMin | 1280 | AE#44763 |
| fCombatSearchIgnoreLocationRadius | 384 | AE#49554 |
| fCombatSearchInteriorRadiusMax | 1280 | AE#44763 |
| fCombatSearchInteriorRadiusMin | 768 | AE#44763 |
| fCombatSearchLocationCheckDistance | 512 | AE#49519 |
| fCombatSearchLocationInvestigateDistance | 256 | AE#44769 |
| fCombatSearchLocationRadius | 128 | AE#44770 |
| fCombatSearchRadiusBufferDistance | 256 | AE#44763 |
| fCombatSearchRadiusMemberDistance | 4096 | AE#44763 |
| fCombatSearchSightRadius | 1536 | AE#44760 |
| fCombatSearchWanderDistance | 512 | AE#49535 AE#49537 AE#49546 |
| fCombatSplashDamageMinRadius | 128 | не читается напрямую |
| fCombatStepAdvanceDistance | 128 | не читается напрямую |
| fCombatStrafeDistanceMax | 256 | AE#50702 |
| fCombatStrafeDistanceMin | 96 | AE#49460 AE#50702 |
| fCombatStrafeMinDistanceRadiusMult | 3 | AE#50702 |
| fCombatSurroundDistanceMax | 384 | AE#47904 |
| fCombatSurroundDistanceMin | 128 | AE#47904 |
| fCombatTargetLocationAvoidNodeRadiusOffset | -16 | AE#47051 |
| fCombatTargetLocationCurrentReservationDistanceMult | 0.75 | AE#47016 |
| fCombatTargetLocationMaxDistance | 2048 | AE#47051 |
| fCombatTargetLocationMinDistanceMult | 0.67 | AE#47016 |
| fCombatTargetLocationPathingRadius | 10 | AE#47051 |
| fCombatTargetLocationRadiusSizeMult | 1.25 | AE#47011 AE#47020 AE#47050 |
| fCombatTeammateFollowRadiusBase | 512 | не читается напрямую |
| fCombatTeammateFollowRadiusMin | 512 | не читается напрямую |
| fCombatTeammateFollowRadiusMult | 3 | не читается напрямую |
| fCombatThreatBufferRadius | 16 | AE#47246 |
| fCombatThreatProximityExplosionAvoidTime | 0 | не читается напрямую |
| fConeProjectileEnvironmentDistance | 192 | AE#43806 |
| fCoveredAdvanceMinAdvanceDistanceMax | 512 | не читается напрямую |
| fCoveredAdvanceMinAdvanceDistanceMin | 256 | не читается напрямую |
| fDangerousObjectExplosionRadius | 100 | AE#14158 AE#14175 |
| fDangerousProjectileExplosionRadius | 30 | AE#46886 AE#47308 |
| fDeadReactionDistance | 400 | AE#39665 AE#0x70aef6 |
| fDeathForceRangedDamageMax | 50 | AE#0x4188ef |
| fDeathForceRangedDamageMin | 1 | AE#0x4188e7 |
| fDeathForceRangedForceMax | 30 | AE#0x4188ff |
| fDeathForceRangedForceMin | 10 | AE#0x4188f7 |
| fDeathSoundMaxDistance | 1000 | AE#37896 |
| fDefaultAvoidBoxAvoidNodeRadius:Pathfinding | 32 | AE#92753 |
| fDefaultStaticAvoidNodeRadius:Pathfinding | 32 | AE#92150 |
| fDetectEventDistanceNPC | 512 | AE#37774 |
| fDetectEventDistancePlayer | 1536 | AE#37774 |
| fDetectEventDistanceVeryLoudMult | 2 | AE#37774 |
| fDetectEventSneakDistanceVeryLoud | 3072 | AE#26366 |
| fDetectionCombatNonTargetDistanceMult | 0.75 | AE#37774 |
| fDetectionLOSDistanceAngle | 60 | AE#37774 |
| fDetectionLOSDistanceMultExterior | 0.5 | AE#37774 |
| fDetectionLOSDistanceMultInterior | 0.75 | AE#37774 |
| fDetectProjectileDistanceNPC | 512 | AE#37774 |
| fDetectProjectileDistancePlayer | 1024 | AE#37774 |
| fDialogFocalDepthRange | 300 | не читается напрямую |
| fDisarmedPickupWeaponDistanceMult | 2 | не читается напрямую |
| fDistanceAutomaticallyActivateDoor | 250 | AE#41412 |
| fDistanceExteriorReactCombat | 600 | не читается напрямую |
| fDistanceFadeActorAutoLoadDoor | 150 | AE#30824 |
| fDistanceInteriorReactCombat | 300 | не читается напрямую |
| fDistanceMultiplier:LOD | 1 | AE#13279 |
| fDistanceProjectileExplosionDetection | 400 | не читается напрямую |
| fDistancetoPlayerforConversations | 3000 | AE#0x6e0b2f AE#39637 |
| fDistFromPathForFollowingRadiusMult:Pathfinding | 2 | AE#0x1228fb6 |
| fDOFDistanceMult | 5 | не читается напрямую |
| fDragonLandingForPlayerSearchDist:GENERAL | 3000 | AE#39647 |
| fDragonLandingZoneClearRadius | 256 | AE#20587 |
| fDragonRiderLandingZoneClearRadius | 200 | AE#31059 |
| fDynamicDOFFarDist:Display | 1000 | AE#107383 |
| fDynamicDOFFarRange:Display | 10000 | AE#107383 |
| fDynamicDOFNearDist:Display | 100 | AE#107383 |
| fDynamicDOFNearRange:Display | 100 | AE#107383 |
| fEdgeDistFromVert: NavMesh | 10 | не читается напрямую |
| fExplosionLOSBufferDistance | 24 | AE#43852 |
| fExplosionSplashRadius | 250 | AE#43869 |
| fExplosionWaterRadiusRatio | 0.1 | AE#43866 |
| fEyeIKDistanceMax:LookIK | 2.5 | не читается напрямую |
| fFavorEventStopDistance | 650 | не читается напрямую |
| fFavorEventTriggerDistance | 220 | не читается напрямую |
| fFavorRequestPickDistance | 2100 | AE#26127 |
| fFavorRequestRadius | 4000 | AE#37347 AE#0x675798 |
| fFindClosestEdgesRadius:BSPathing | 512 | AE#90402 |
| fFirstSliceDistance:Display | 2000 | AE#108496 |
| fFleeDistanceExterior | 5000 | AE#28543 AE#42561 AE#28536 AE#39641 AE#40077 |
| fFleeDistanceInterior | 3000 | AE#28542 AE#42560 AE#28536 AE#39641 AE#40077 |
| fFleeDoneDistanceExterior | 2500 | AE#28556 AE#39641 |
| fFleeDoneDistanceInterior | 600 | AE#28556 AE#39641 |
| fFlickeringLightDistance:General | 1024 | AE#18889 |
| fFloatQuestMarkerMaxDistance | 2000 | AE#51676 AE#51728 |
| fFloatQuestMarkerMinDistance | 1000 | AE#51676 |
| fFollowStartSprintDistance | 500 | AE#28644 |
| fFootIKDistance:GamePlay | 1024 | не читается напрямую |
| fGrassFadeRange:Grass | 1000 | AE#15370 AE#0x20351b |
| fGrassMaxStartFadeDistance:Grass | 7000 | AE#53308 AE#36622 AE#53310 |
| fGrassMinStartFadeDistance:Grass | 400 | AE#53308 AE#36622 AE#53310 AE#15370 AE#0x2034dd |
| fGrassStartFadeDistance:Grass | 7000 | AE#15370 AE#0x2034ed AE#15379 AE#0x20354b AE#36622 AE#53308 и ещё 1 |
| fGuardPackageAttackRadiusMult | 0.5 | не читается напрямую |
| fGunDecalCameraDistance | 2048 | не читается напрямую |
| fGunParticleCameraDistance | 2048 | AE#37690 AE#43851 AE#44213 |
| fGunShellCameraDistance | 512 | не читается напрямую |
| fHazardDropMaxDistance | 1024 | AE#0x7da0c4 |
| fHostileActorExteriorDistance | 3000 | AE#41402 |
| fHostileActorInteriorDistance | 2000 | AE#41402 |
| fHostileFlyingActorExteriorDistance | 15000 | AE#41402 |
| fHUDCompassLocationMaxDist | 20000 | AE#51641 |
| fIBLFChannelsDistortionBlue:Display | -2 | AE#0x14d211d |
| fIBLFChannelsDistortionGreen:Display | 0 | AE#0x14d20c5 |
| fIBLFChannelsDistortionRed:Display | 2 | AE#0x14d20f1 |
| fIBLFLightsRangeDownshift:Display | 1 | AE#0x14d20a5 |
| fImpactShaderMaxDistance | 5000 | AE#26481 |
| fIndRadius:Display | 100 | AE#107255 |
| fInteriorShadowDistance:Display | 3000 | AE#0x147feda AE#0x14f00f8 |
| fInventory3DBoundRadiusScale:Interface | 12.5 | AE#51773 |
| fInventoryLight2Radius:Interface | 0 | AE#52746 |
| fInventoryLightRadius:Interface | 400 | AE#52746 |
| fIronSightsDOFDistance | 275 | AE#41246 AE#41249 |
| fIronSightsDOFRange | 650 | AE#41246 AE#41249 |
| fKinectMaxAllyTradeDistance:Kinect | 60000 | AE#56860 |
| fLeafAnimDampenDistEnd:Display | 4600 | AE#105624 |
| fLeafAnimDampenDistStart:Display | 3600 | AE#105624 |
| fLensFlareFalloffRange:ImageSpace | 256 | AE#106989 AE#0x14c3559 |
| fLightLODRange:Display | 500 | AE#0x40eedb AE#0x2903b6 AE#18882 AE#26232 AE#0x40c3e7 AE#0x40f056 |
| fLinearRadiusMult:bLightAttenuation | 1 | AE#0x17364 AE#0x173b5 AE#17610 AE#17626 |
| fLockpickLightRadius:Interface | 400 | AE#52746 |
| fLODBoundRadiusMult:LOD | 10 | AE#15906 |
| fLodDistance:LOD | 500 | AE#0x43258c |
| fLowPerfCombatantVoiceDistance | 1000 | не читается напрямую |
| fMagicAbsorbDistanceReachMult | 3 | не читается напрямую |
| fMagicBallMaximumDistance | 2000 | AE#0x14e078 |
| fMagicBallOptimalDistance | 1000 | AE#0x14e06c |
| fMagicBoltMaximumDistance | 2000 | AE#0x14e0c6 |
| fMagicBoltOptimalDistance | 1000 | AE#11416 |
| fMagicDefaultTouchDistance | 256 | не читается напрямую |
| fMagicFogMaximumDistance | 500 | AE#0x14e092 |
| fMagicFogOptimalDistance | 300 | AE#11414 |
| fMagicGrabActorMinDistance | 120 | AE#34334 |
| fMagicGrabActorRange | 750 | не читается напрямую |
| fMagicLightRadiusBase | 0 | не читается напрямую |
| fMagicProjectileMaxDistance | 10000 | не читается напрямую |
| fMagicRangeTargetCostMult | 1 | AE#26504 |
| fMagicSprayMaximumDistance | 1000 | AE#0x14e0ac |
| fMagicSprayOptimalDistance | 500 | AE#11415 |
| fMagicTargetLocationExtraLargeActorRadius:Gameplay | 256 | AE#34410 |
| fMagicTargetLocationNormalActorRadius:Gameplay | 32 | AE#34410 |
| fMagicTelekinesiDistanceMult | 1 | AE#35047 |
| fMagicTelekinesisBaseDistance | 2100 | AE#40541 AE#40551 |
| fMagicTelekinesisDistanceMin | 64 | AE#35047 |
| fMagnetismStrafeMaxDistance:Combat | 750 | AE#41234 |
| fMapLocalMarkerSelectionDist:MapMenu | 0.03 | AE#52980 |
| fMapWorldYawRange:MapMenu | 80 | AE#53043 |
| fMaxAvoidanceRadius:Pathfinding | 512 | AE#92200 |
| fMaxDistanceFromNavmeshMult:Pathfinding | 0.25 | AE#93109 AE#93112 AE#93121 AE#93125 |
| fMaxDistanceMoved:Pathfinding | 5 | не читается напрямую |
| fMaxDistFromPathRadiusMult:Pathfinding | 5 | AE#90714 |
| fMaxFocusShadowMapDistance:Display | 450 | AE#36561 AE#36589 AE#0x14fa203 |
| fMaxHighlightRadius:Audio | 250 | AE#35124 AE#35127 |
| fMaxMarkerSelectionDist:MapMenu | 0.003 | AE#53111 |
| fMaxPathLookAtPointDist:HeadTracking | 512 | AE#38009 |
| fMaxSubtitleDistance:Interface | 1250 | AE#52637 |
| fMaxTrackingDist:LookIK | 5000 | не читается напрямую |
| fMeshLODLevel1FadeDist:Display | 999999 | AE#0x147aeb8 AE#0x147afc3 |
| fMeshLODLevel1FadeTreeDistance:Display | 2844 | AE#105497 |
| fMeshLODLevel2FadeDist:Display | 999999 | AE#0x147aec0 AE#0x147aff3 |
| fMeshLODLevel2FadeTreeDistance:Display | 2048 | AE#0x147aed8 |
| fMinAvoidanceRadius:Pathfinding | 256 | AE#92200 |
| fMinDist:Pathfinding | 50 | не читается напрямую |
| fMinDistanceUseHorse | 10000 | не читается напрямую |
| fMineExteriorRadiusMult | 1.6 | AE#44217 |
| fMinFailureDistance:Pathfinding | 50 | AE#42212 |
| fMinPathLookAtPointDist:HeadTracking | 128 | AE#38009 |
| fMinTrackingDist:LookIK | 12 | не читается напрямую |
| fmodifiedTargetAttackRange | 512 | AE#44899 |
| fMostCommonProjectileCollisionRadius1:Combat | 0 | AE#44191 AE#44196 |
| fMostCommonProjectileCollisionRadius2:Combat | 0.5 | AE#44191 AE#44196 |
| fMostCommonProjectileCollisionRadius3:Combat | 10 | AE#44191 AE#44196 |
| fMountedAttackRange:Combat | 135 | AE#47272 |
| fMovementNearTargetAvoidRadius | 64 | не читается напрямую |
| fMovementTargetAvoidRadius | 512 | AE#44741 |
| fMovementTargetAvoidRadiusMult | 1.25 | AE#44741 |
| fNavmeshBoundsActorRadiusMultiplier:Pathfinding | 1 | AE#0x12110c3 |
| fNear1stPersonDistance:Display | 5 | AE#107123 AE#107139 |
| fNearDistance:Display | 15 | AE#0x40cb1f AE#76746 AE#76750 |
| fNodeDistanceThreshold:Pathfinding | 25 | AE#42481 |
| fNoLODFarDistanceMax:Display | 10240 | AE#0x652d87 AE#76746 AE#0xe2f4b2 |
| fNoLODFarDistanceMin:Display | 100 | AE#0x652e20 AE#76746 AE#0xe2f4ba AE#0xe2f4ca |
| fNoLODFarDistancePct:Display | 1 | AE#0x652e20 AE#76746 AE#0xe2f4c2 |
| fNoteDistance:Interface | 90 | AE#51049 |
| fNotVisibleNavmeshMoveDist:Actor | 2048 | AE#38308 |
| fOverShoulderRangedDragonAddY | -150 | AE#0x554d64 |
| fOverShoulderRangedDragonPosX | 0 | AE#0x554d54 |
| fOverShoulderRangedDragonPosZ | 90 | AE#0x554d5c |
| fOverShoulderRangedMountedAddY | -150 | AE#50767 |
| fOverShoulderRangedMountedPosX | 0 | AE#50767 |
| fOverShoulderRangedMountedPosZ | 90 | AE#50767 |
| fPathingLargeActorRadius:Pathfinding | 80 | AE#31012 AE#31111 AE#31129 AE#34994 AE#0x8cec2a AE#0x8cec73 и ещё 5 |
| fPathPointZDistanceAllowance | 10 | не читается напрямую |
| fPickupItemDistanceFudge | 32 | не читается напрямую |
| fPickupWeaponDistanceMinMaxDPSMult | 5 | не читается напрямую |
| fPickupWeaponMeleeDistanceMax | 1024 | не читается напрямую |
| fPickupWeaponMeleeDistanceMin | 256 | не читается напрямую |
| fPickupWeaponRangedDistanceMax | 2048 | не читается напрямую |
| fPickupWeaponRangedDistanceMin | 512 | не читается напрямую |
| fPickupWeaponRangedMeleeDPSRatioThreshold | 0.5 | не читается напрямую |
| fPickupWeaponTargetUnreachableDistanceMult | 4 | не читается напрямую |
| fPickupWeaponUnarmedDistanceMax | 2048 | не читается напрямую |
| fPickupWeaponUnarmedDistanceMin | 256 | не читается напрямую |
| fPitchZoomOutMaxDist:Camera | 100 | AE#33119 AE#50770 AE#50914 |
| fPlayerBodyEditDistance:Interface | 175 | AE#52413 |
| fPlayerDropDistance | 100 | AE#40554 |
| fPlayerFaceEditDistance:Interface | 100 | AE#52413 |
| fPlayerTargetCombatDistance | 3000 | не читается напрямую |
| fPoissonRadiusScale:Display | 4 | AE#0x14fa203 |
| fProjectileDefaultTracerRange | 10000 | не читается напрямую |
| fProjectileMaxDistance | 32768 | не читается напрямую |
| fQuadraticRadiusMult:bLightAttenuation | 1 | AE#0x173bf AE#0x173cc AE#17610 AE#17626 |
| fRandomDoorDistance | 150 | AE#0x27f969 |
| fReflectionMarchingRadius:Display | 0.4 | AE#23172 AE#107239 |
| fRumbleShakeRadiusMult | 1 | не читается напрямую |
| fSandboxDurationRangeMult | 0.25 | AE#0x416768 |
| fSandBoxExtraDialogueRange | 384 | AE#37548 |
| fSandBoxInterMarkerMinDist | 100 | AE#29011 |
| fSandBoxRadiusHysteresis | 50 | AE#29001 |
| fSandBoxSearchRadius | 6000 | AE#29011 AE#29028 AE#42620 AE#42639 |
| fSAO_CS_Radius:Display | 250 | AE#0x14d6feb |
| fSAORadius:Display | 250 | AE#0x14d5990 |
| fSeenDataUpdateRadius | 1024 | AE#19193 AE#0x2c2a0e AE#19142 AE#36865 |
| fShadowDistance:Display | 10000 | AE#105624 AE#0x147fee4 AE#0x14f00f0 AE#108496 |
| fShockBoltsRadius | 48 | не читается напрямую |
| fShockBoltsRadiusStrength | 3 | не читается напрямую |
| fShockBranchBoltsRadius | 24 | не читается напрямую |
| fShockBranchBoltsRadiusStrength | 3 | не читается напрямую |
| fShortPathRadiusMult:Pathfinding | 2 | AE#90711 |
| fSkyCellRefFadeDistance | 150000 | AE#15906 |
| fSkyCellRefProcessDistanceMult | 10 | AE#19413 AE#0x2d50ce AE#37284 |
| fSnapMaxAngularDistance:RagdollAnim | 1 | AE#78484 |
| fSnapMaxLinearDistance:RagdollAnim | 0.3 | AE#78484 |
| fSneakAttackSkillUsageRanged | 2.5 | AE#38586 |
| fSneakDistanceAttenuationExponent | 2 | AE#26365 AE#26366 |
| fSneakExteriorDistanceMult | 2.1 | AE#0x4159b5 AE#26365 AE#26366 |
| fSneakFlyingDistanceMult | 3 | AE#0x4159f7 AE#26365 |
| fSneakMaxDistance | 2500 | AE#26365 AE#0x4159bd AE#0x4159c7 AE#26366 |
| fSortActorDistanceListTimer | 0 | AE#40411 AE#40747 |
| fSparklesMaxDistance:Display | 1000 | AE#107245 |
| fSplitDistanceMult:TerrainManager | 1.5 | AE#0x511e28 |
| fSprayDecalsDistance | 64 | AE#15226 |
| fSprintDistToPathThresholdRadiusMult:Pathfinding | 0.5 | AE#42050 |
| fStatsCameraNearDistance | 1 | AE#52530 |
| fSubmergedLODDistance | 384 | AE#32089 |
| fSubmergedMaxWaterDistance | 48 | AE#32117 |
| fSummonDistanceCheckThreshold | 750 | AE#34990 |
| fSummonedCreatureMaxFollowDist | 400 | AE#37404 |
| fSummonedCreatureMinFollowDist | 250 | AE#37404 |
| fSummonedCreatureSearchRadius | 512 | AE#34994 |
| fTalkingDistance:LOD | 2000 | AE#0x43258c |
| fTargetLockXYRange:Gameplay | 7500 | AE#40829 |
| fTargetSearchRadius | 20000 | AE#29583 |
| fTeammateAggroOnDistancefromPlayer | 4000 | AE#0x70aa50 |
| fTotalDisplacementThresholdRadiusMult:Pathfinding | 0.66 | AE#92149 |
| fTreeLoadDistance:TerrainManager | 75000 | AE#0x511e0e |
| fTreesMidLODSwitchDist:Display | 3600 | AE#27083 AE#0x43718d |
| fTriggerAvoidPlayerDistance | 400 | AE#41442 |
| fUICameraFarDistance:Interface | 20480 | AE#52726 AE#52742 |
| fUICameraNearDistance:Interface | 15 | AE#52726 AE#52742 |
| fUIPlayerSceneLight2Radius:Interface | 1024 | AE#52746 |
| fUIPlayerSceneLight3Radius:Interface | 1024 | AE#52746 |
| fUIPlayerSceneLightRadius:Interface | 1500 | AE#52746 |
| fUIRaceSexLight2Radius:Interface | 1400 | AE#52746 |
| fUIRaceSexLightRadius:Interface | 1400 | AE#52746 |
| fVanityModeMaxDist:Camera | 600 | AE#50911 |
| fVanityModeMaxDistFlyMult:Camera | 4 | AE#50911 |
| fVanityModeMinDist:Camera | 155 | AE#50911 |
| fVanityModeMinDistFly:Camera | 1200 | AE#50911 |
| fVATSCameraCutAwayDistance | 1 | AE#20710 |
| fVATSDistanceFactor | 2 | AE#0x419063 |
| fVATSDOFRange | 500 | AE#41246 AE#41250 |
| fVATSGrenadeDistAimZMult | 0.4 | не читается напрямую |
| fVATSGrenadeRangeMin | 128 | AE#0x419063 |
| fVATSGrenadeRangeMult | 2 | AE#0x419063 |
| fVATSGrenadeSuccessMaxDistance | 75 | AE#43917 |
| fVATSH2HWarpDistanceMult | 0.27 | не читается напрямую |
| fVATSLightDistance:VATS | 100 | AE#0x7f4027 |
| fVATSMaxEngageDistance | 5000 | не читается напрямую |
| fVATSMeleeMaxDistance | 300 | AE#0x419063 |
| fVATSMeleeWarpDistanceMult | 0.32 | не читается напрямую |
| fVATSRangedLongDistance:VATS | 2500 | AE#44284 |
| fVATSRangedLowHealthPercent:VATS | 5 | AE#44284 |
| fVATSRangedPercentMin:VATS | 50 | AE#44284 |
| fVATSRangedPercentSneakKill:VATS | 20 | AE#44284 |
| fVATSRangedPercentTargetNoThreat:VATS | 10 | AE#44284 |
| fVATSRangedTargetLowLevelMult:VATS | 0.3 | AE#44284 |
| fVATSRangeSpreadMax | 3 | AE#0x419063 |
| fVATSRangeSpreadUncertainty | 0.7 | AE#0x419063 |
| fVATSSafetyMaxTimeRanged | 6 | AE#44295 |
| fVATSSmartCameraCheckStepDistance | 64 | AE#37727 |
| fVATSStrangerDistance | 164 | не читается напрямую |
| fVATSStrangerOdds | 0.1 | не читается напрямую |
| fVATSTargetActorZMultFarDist | 35 | не читается напрямую |
| fVATSTargetFOVMinDist | 150 | не читается напрямую |
| fVATSTargetFOVMultFarDist | 2750 | не читается напрямую |
| fVATSThrownWeaponRangeMult | 2 | AE#0x418f95 AE#0x419063 |
| fVisibleNavmeshMoveDist:Actor | 4096 | AE#38308 |
| fVolumetricLightingRangeFactor:Display | 40 | AE#0xff768 AE#0xff808 AE#0x14c44ef AE#0x14c479c AE#107014 |
| fWarpRequestActorRadius:Pathfinding | 5 | AE#42218 |
| fWaterGroupHeightRange:Water | 10 | не читается напрямую |
| fWaveOffsetRange:Grass | 1.75 | AE#18758 |
| fZKeyComplexHelperMinDistance | 6 | AE#40556 |
| fZKeyMaxContactDistance | 10 | AE#40556 |
| iAIMaxSocialDistanceToTriggerEvent | 500 | AE#37548 |
| iAISocialDistanceToTriggerEvent | 200 | AE#37548 |
| iAutoViewMinDistance:Display | 2000 | AE#76751 |
| iBoneLODDistMult | 1000 | не читается напрямую |
| iCrimeAlarmLowRecDistance | 2000 | AE#39695 |
| iCrimeAlarmRecDistance | 4000 | AE#39361 AE#39695 AE#40084 |
| iDistancetoAttackedTarget | 384 | не читается напрямую |
| iDragonFastTravelMinDistance | 10000 | не читается напрямую |
| iFadeNodeMinNearDistance:LOD | 500 | AE#105491 |
| iGrassCellRadius:Grass | 2 | AE#15370 |
| iMapMarkerFadeStartDistance | 10000 | AE#51728 |
| iMapMarkerRevealDistance | 1000 | AE#0x2f979e AE#20174 AE#0x2f9b33 |
| iMapMarkerVisibleDistance | 12500 | не читается напрямую |
| iSneakSkillUseDistance | 1000 | не читается напрямую |
| iVATSCameraHitDist | 100 | AE#44310 |
| iVATSStrangerMaxHP | 150 | AE#44287 |
| SMagicCastRangedUnderwater | Вы не можете использовать дистанционные заклинания под водой | нет объекта |
| SRangeText | Дальность | нет объекта |
| SSelfRange | на себя | нет объекта |
| STargetRange | на цель | нет объекта |
| STouchRange | при ударе | нет объекта |
| SViewDistance | Обзор | нет объекта |
| uDistantTreeBlockCacheSizePerCell:TerrainManager | 1 | не читается напрямую |
| uIBLFBloomRadius:Display | 2 | AE#0x14d212b |
| uIBLFBlurRadiusX:Display | 7 | AE#0x14d20d3 |
| uIBLFBlurRadiusY:Display | 2 | AE#0x14d20ff |

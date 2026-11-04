#pragma once

// T10 diagnostics (0.8.27, [Probe] bTraceScenes): why the bard's song ends when the player leaves the
// tavern. T10 found in the game's code that a scene's dialogue line ends at once while its speaker has no
// AIProcess::high (BGSSceneActionDialogue vfunc 0x13, AE#24236 +0x245 / +0x257), so the scene runs out of
// phases in frames (AE#24126 +0xc9 -> AE#24140). Records:
//   scene.start, scene.stop - BGSScene::SetActive (AE#24140) for every scene; a stop with its stack;
//   scene.stop.papyrus      - Scene.Stop from a script (native AE#55321);
//   scene.line              - a dialogue line ends (vfunc 0x11, AE#24235), for speakers in cells ActorHold
//                             holds and in the Preload interior: level, AIProcess::high, the "said" bit.
namespace Scenes
{
	// AE only.
	void Install();
}

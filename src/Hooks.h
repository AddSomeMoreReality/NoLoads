#pragma once

namespace Hooks
{
	// Puts in the hooks of HooksCells.cpp, HooksTes.cpp, HooksInteriorSim.cpp and HooksPortal.cpp (vfuncs,
	// the detours in one transaction, call hooks; helpers in HooksCommon.h) and PlayerCharacter::Update here,
	// then calls the modules that install their own hooks (ActorHold, Scenes).
	//
	// A new module (StoryFilter, ...) keeps its hooks in its own .cpp, in an anonymous namespace, and puts
	// them in from its Module::Install() with HooksCommon.h (DetourTransaction, WriteCall, WriteVfunc), in a
	// transaction of its own. Install gets one line: the call of Module::Install().
	void Install();

	// Every hook the portal relies on is in. Without AE#19130's detour an attached interior crashes the
	// game when it leaves (0.8.17), so nothing attaches unless this is true.
	[[nodiscard]] bool Ready();

	// Main thread: puts a_cell at the front of TES::interiorBuffer (AE#13434). Works once detours are in.
	void BufferInterior(RE::TESObjectCELL* a_cell);
}

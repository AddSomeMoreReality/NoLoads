#include "Hooks.h"

#include "AccessWatch.h"
#include "ActorHold.h"
#include "ActorJumps.h"
#include "Detectors.h"
#include "Events.h"
#include "HooksCommon.h"
#include "Hud.h"
#include "InteriorSim.h"
#include "MainThread.h"
#include "Preload.h"
#include "Probe.h"
#include "Scenes.h"
#include "Trace.h"

namespace
{
	// ------------------------------------------------------------------
	// PlayerCharacter::Update (Actor::Update, 0xAD): frame counter for the trace.
	// ------------------------------------------------------------------
	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			Trace::NextFrame();
			Events::OnPlayerTick();
			if (_first) {
				_first = false;
				Trace::Write("info.PlayerUpdate.first", Trace::Args{}.Num("delta", a_delta), _ReturnAddress());
			}

			func(a_this, a_delta);
			MainThread::Drain();
			Hud::Tick();
			AccessWatch::Tick();
			ActorJumps::Tick();
			Preload::Tick();
			Detectors::Tick();
			InteriorSim::Tick();
			Probe::Tick();
			Trace::FlushIfStale();
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static inline bool                             _first{ true };
	};

	std::atomic<bool> g_detoursReady{ false };
	std::atomic<bool> g_callsReady{ false };

	// One transaction for the detours of every Hooks*.cpp file: all of them go in, or none.
	void InstallDetours()
	{
		Hooks::DetourTransaction transaction;
		Hooks::Tes::AttachDetours(transaction);
		Hooks::Interior::AttachDetours(transaction);
		Hooks::Portals::AttachDetours(transaction);
		if (!transaction.Commit()) {
			return;
		}
		g_detoursReady = true;
		logs::info("detours installed");
	}
}

namespace Hooks
{
	void Install()
	{
		Cells::InstallVfuncs();

		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		WriteVfunc<PlayerUpdate>(player, 0xAD);  // Actor::Update

		Interior::InstallVfuncs();
		Portals::InstallVfuncs();

		logs::info("vfunc hooks installed");

		InstallDetours();
		g_callsReady = Portals::InstallCalls();  // controller LOD and fade
		ActorHold::Install();  // T11: the portal's actors stay high through transitions; level traces
		Scenes::Install();     // T10: scene traces
	}

	bool Ready()
	{
		return g_detoursReady.load() && g_callsReady.load();
	}

	void BufferInterior(RE::TESObjectCELL* a_cell)
	{
		if (auto* tes = RE::TES::GetSingleton(); tes && g_detoursReady.load()) {
			Tes::BufferInteriorOriginal(tes, a_cell);
		}
	}
}

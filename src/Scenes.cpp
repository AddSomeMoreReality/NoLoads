#include "Scenes.h"

#include "ActorHold.h"
#include "Settings.h"
#include "Trace.h"

#include <detours.h>

namespace
{
	constexpr std::uint32_t kLineSaid = 0x4000;  // BGSSceneAction::flags: the line was started (AE#24236 +0x1ca), cleared by vfunc 0x11

	// Editor IDs are read on the main thread only (Trace::Args::Form); scripts stop scenes from VM threads.
	Trace::Args& AddScene(Trace::Args& a_args, const RE::BGSScene* a_scene)
	{
		if (Trace::IsMainThread()) {
			a_args.Form("scene", a_scene).Form("quest", a_scene->parentQuest);
		} else {
			a_args.FormID("scene", a_scene->GetFormID()).FormID("quest", a_scene->parentQuest ? a_scene->parentQuest->GetFormID() : 0);
		}
		return a_args.Bool("isPlaying", a_scene->isPlaying)
			.Int("shuttingDown", a_scene->isShuttingDown & 0xFF)
			.Int("phase", static_cast<std::int32_t>(a_scene->currentPhaseIndex));
	}

	Trace::Args& AddTiming(Trace::Args& a_args)
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* cell = player ? player->GetParentCell() : nullptr;
		return a_args.FormID("playerCell", cell ? cell->GetFormID() : 0)
			.Int("msSinceOpen", ActorHold::MsSinceOpen())
			.Int("msSinceClose", ActorHold::MsSinceClose());
	}

	// AE#24140: BGSScene::SetActive(bool). The first false (+0xB0 set, +0xB4 clear) starts the stop and
	// sends the end event; a second false from AE#24125+0x42 clears +0xB0.
	struct SetActive
	{
		static void thunk(RE::BGSScene* a_scene, bool a_active)
		{
			if (a_scene) {
				const bool stop = !a_active && a_scene->isPlaying && (a_scene->isShuttingDown & 0xFF) == 0;
				const bool start = a_active && !a_scene->isPlaying;
				if (stop || start) {
					Trace::Args args;
					AddScene(args, a_scene);
					AddTiming(args);
					if (stop) {
						args.Raw("stack", Trace::StackJson(0, 10));
					}
					Trace::Write(stop ? "scene.stop" : "scene.start", args, _ReturnAddress());
				}
			}
			func(a_scene, a_active);
		}

		static inline decltype(&thunk) func;
	};

	// AE#55321: the native Scene.Stop (xor edx,edx; mov rcx,r8; jmp AE#24140).
	struct PapyrusStop
	{
		static void thunk(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID, RE::BGSScene* a_scene)
		{
			if (a_scene) {
				Trace::Args args;
				AddScene(args, a_scene);
				AddTiming(args);
				Trace::Write("scene.stop.papyrus", args.Int("stackID", a_stackID), _ReturnAddress());
			}
			func(a_vm, a_stackID, a_scene);
		}

		static inline decltype(&thunk) func;
	};

	// BGSSceneActionDialogue vfunc 0x11 (AE#24235): the line ends. AE#24236 calls it when the line is over,
	// and at once when the speaker has no AIProcess::high (T10).
	struct LineEnd
	{
		static void thunk(RE::BGSSceneActionDialogue* a_this, RE::BGSScene* a_scene)
		{
			if (a_this && a_scene && a_scene->parentQuest) {
				const auto ref = a_scene->parentQuest->GetAliasedRef(a_this->actorID).get();
				auto*      actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (actor && ActorHold::InScope(actor)) {
					const auto* process = actor->GetActorRuntimeData().currentProcess;
					const auto* base = actor->GetActorBase();  // its name: GetName would lock the extra data list
					Trace::Args args;
					AddScene(args, a_scene);
					args.Int("index", a_this->index)
						.FormID("actor", actor->GetFormID())
						.Str("name", base && base->GetFullName() ? base->GetFullName() : "")
						.Int("processLevel", process ? static_cast<std::int32_t>(std::to_underlying(process->processLevel.get())) : -1)
						.Bool("high", process && process->high)
						.Bool("said", (a_this->flags.underlying() & kLineSaid) != 0)
						.Bool("held", ActorHold::HoldsActor(actor));
					AddTiming(args);
					Trace::Write("scene.line", args, _ReturnAddress());
				}
			}
			func(a_this, a_scene);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace Scenes
{
	void Install()
	{
		if (!Settings::Get().probeTraceScenes) {
			return;
		}
		if (!REL::Module::IsAE()) {
			logs::info("scene trace: not installed, AE only (AE#24140, AE#55321, AE#24235)");
			return;
		}

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		SetActive::func = reinterpret_cast<decltype(SetActive::func)>(REL::Relocation<std::uintptr_t>{ REL::ID(24140) }.address());
		PapyrusStop::func = reinterpret_cast<decltype(PapyrusStop::func)>(REL::Relocation<std::uintptr_t>{ REL::ID(55321) }.address());
		const auto setActive = DetourAttach(reinterpret_cast<PVOID*>(&SetActive::func), reinterpret_cast<PVOID>(&SetActive::thunk));
		const auto papyrusStop = DetourAttach(reinterpret_cast<PVOID*>(&PapyrusStop::func), reinterpret_cast<PVOID>(&PapyrusStop::thunk));
		bool       detours = false;
		if (setActive != NO_ERROR || papyrusStop != NO_ERROR) {
			DetourTransactionAbort();
			logs::error("scene trace: detours failed: AE#24140 {}, AE#55321 {}", setActive, papyrusStop);
		} else if (const auto commit = DetourTransactionCommit(); commit != NO_ERROR) {
			logs::error("scene trace: detour commit failed: {}", commit);
		} else {
			detours = true;
		}

		REL::Relocation<std::uintptr_t> dialogue{ RE::VTABLE_BGSSceneActionDialogue[0] };
		LineEnd::func = dialogue.write_vfunc(0x11, LineEnd::thunk);
		logs::info("scene trace: SetActive and Scene.Stop detours {}, dialogue line end hook installed", detours);
	}
}

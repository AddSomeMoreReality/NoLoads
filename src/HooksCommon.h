#pragma once

// Helpers shared by the Hooks*.cpp files and by the modules that install their own hooks (ActorHold, Scenes).

#include "Trace.h"

#include <detours.h>

namespace Hooks
{
	inline Trace::Args& AddTesState(Trace::Args& a_args)
	{
		if (auto* tes = RE::TES::GetSingleton()) {
			a_args.Cell("tesInterior", tes->interiorCell)
				.Form("tesWorld", tes->GetRuntimeData2().worldSpace)
				.XY("grid", tes->currentGridX, tes->currentGridY)
				.XY("queued", tes->currentQueuedX, tes->currentQueuedY);
		}
		return a_args;
	}

	inline Trace::Args& AddPlayerPos(Trace::Args& a_args)
	{
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			a_args.Pos("playerPos", player->GetPosition()).Cell("playerCell", player->GetParentCell());
		}
		return a_args;
	}

	// Writes hook.<name>.enter on construction and hook.<name>.leave with dur_us on Leave().
	class HookScope
	{
	public:
		HookScope(std::string_view a_name, const Trace::Args& a_args, const void* a_caller) :
			_name(a_name),
			_caller(a_caller)
		{
			Trace::Write(std::format("hook.{}.enter", _name), a_args, _caller);
			_start = Trace::NowUs();
		}

		void Leave(Trace::Args a_args = {})
		{
			a_args.Int("dur_us", Trace::NowUs() - _start);
			Trace::Write(std::format("hook.{}.leave", _name), a_args, _caller);
		}

	private:
		std::string_view _name;
		const void*      _caller;
		std::int64_t     _start{ 0 };
	};

	template <class Hook>
	void WriteVfunc(REL::Relocation<std::uintptr_t>& a_vtbl, std::size_t a_index)
	{
		Hook::func = a_vtbl.write_vfunc(a_index, Hook::thunk);
	}

	// A call instruction in the game's code redirected to a_thunk, after checking that it is one and
	// calls a_target. AE only (offsets from 1.6.1170's machine code).
	template <class Hook>
	bool WriteCall(std::string_view a_name, REL::ID a_function, std::ptrdiff_t a_offset, REL::ID a_target)
	{
		const auto  site = REL::Relocation<std::uintptr_t>{ a_function }.address() + a_offset;
		const auto  target = REL::Relocation<std::uintptr_t>{ a_target }.address();
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(site);
		const auto  called = site + 5 + *reinterpret_cast<const std::int32_t*>(site + 1);
		if (bytes[0] != 0xE8 || called != target) {
			logs::error("call hook {}: no call to AE#{} at AE#{}+0x{:X}", a_name, a_target.id(), a_function.id(), a_offset);
			return false;
		}
		Hook::func = SKSE::GetTrampoline().write_call<5>(site, Hook::thunk);
		return true;
	}

	// Detours on regular functions (Microsoft Detours relocates the prologue). Only inside a transaction.
	template <class Hook>
	LONG AttachDetour(std::uintptr_t a_address)
	{
		Hook::func = reinterpret_cast<decltype(Hook::func)>(a_address);
		return DetourAttach(reinterpret_cast<PVOID*>(&Hook::func), reinterpret_cast<PVOID>(&Hook::thunk));
	}

	template <class Hook>
	LONG AttachDetour(REL::RelocationID a_id)
	{
		return AttachDetour<Hook>(REL::Relocation<std::uintptr_t>{ a_id }.address());
	}

	// One Detours transaction: Attach adds a detour by name, Commit puts all of them in, or none if one
	// failed, and logs why. a_prefix starts the log lines ("actor hold: "); Hooks::Install's own has none.
	// Not committed, it is aborted on destruction.
	class DetourTransaction
	{
	public:
		explicit DetourTransaction(std::string_view a_prefix = {}) :
			_prefix(a_prefix)
		{
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());
		}

		DetourTransaction(const DetourTransaction&) = delete;
		DetourTransaction& operator=(const DetourTransaction&) = delete;

		~DetourTransaction()
		{
			if (!_done) {
				DetourTransactionAbort();
			}
		}

		template <class Hook>
		void Attach(std::string_view a_name, std::uintptr_t a_address)
		{
			_results.emplace_back(a_name, AttachDetour<Hook>(a_address));
		}

		template <class Hook>
		void Attach(std::string_view a_name, REL::RelocationID a_id)
		{
			_results.emplace_back(a_name, AttachDetour<Hook>(a_id));
		}

		// True when every detour is in.
		bool Commit()
		{
			_done = true;
			bool ok = true;
			for (const auto& [name, error] : _results) {
				if (error != NO_ERROR) {
					logs::error("{}detour {} failed: {}", _prefix, name, error);
					ok = false;
				}
			}
			if (!ok) {
				DetourTransactionAbort();
				logs::error("{}detours not installed", _prefix);
				return false;
			}

			if (const auto error = DetourTransactionCommit(); error != NO_ERROR) {
				logs::error("{}detour commit failed: {}", _prefix, error);
				return false;
			}
			return true;
		}

	private:
		std::string_view                               _prefix;
		std::vector<std::pair<std::string_view, LONG>> _results;
		bool                                           _done{ false };
	};

	// The Hooks*.cpp files, for Hooks::Install only. Each puts in its own hooks; the detours go into
	// Install's one transaction.
	namespace Cells
	{
		void InstallVfuncs();  // TESObjectDOOR::Activate, GridCellArray, CellLoaderTask
	}

	namespace Tes
	{
		void AttachDetours(DetourTransaction& a_transaction);  // TES, ExteriorCellLoader, references, Havok, cell unload

		// The game's AE#13434 (TES::BufferInterior), past the detour. Only once the detours are in.
		void BufferInteriorOriginal(RE::TES* a_tes, RE::TESObjectCELL* a_cell);
	}

	namespace Interior
	{
		void InstallVfuncs();                                  // NiAVObject::OnVisible
		void AttachDetours(DetourTransaction& a_transaction);  // the attached interior: cells update, physics, detach
	}

	namespace Portals
	{
		void InstallVfuncs();                                  // effects, NPCs, sound, image space
		void AttachDetours(DetourTransaction& a_transaction);  // NPCs, distances, particles
		[[nodiscard]] bool InstallCalls();                     // true when the controller LOD and fade calls are in
	}
}

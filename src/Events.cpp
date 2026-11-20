#include "Events.h"

#include "AccessWatch.h"
#include "Detectors.h"
#include "Hud.h"
#include "InteriorSim.h"
#include "Portal.h"
#include "MainThread.h"
#include "Overrides.h"
#include "Physics.h"
#include "Preload.h"
#include "Probe.h"
#include "Profiler.h"
#include "Settings.h"
#include "Snapshot.h"
#include "Trace.h"

namespace
{
	constexpr std::array kPositionPlayerTypes{ "kPre"sv, "kPreUpdatePackages"sv, "kPostUpdatePackages"sv, "kPost"sv, "kFinish"sv };

	bool IsPlayer(const RE::TESObjectREFR* a_ref)
	{
		return a_ref && a_ref->IsPlayerRef();
	}

	std::atomic<std::int64_t> g_loadingClosedUs{ -1 };  // last Loading Menu close

	// What the player sees: door -> (wait) -> Fader Menu open = fade-out -> Loading Menu open ->
	// Loading Menu close -> Fader Menu close = end of the fade-in. Without a door (coc, fast travel)
	// the timeline starts at the loading screen. Fader Menu events come on several threads.
	class Journey
	{
	public:
		static Journey& Get()
		{
			static Journey singleton;
			return singleton;
		}

		void Door(std::int64_t a_now)
		{
			const std::scoped_lock lock{ _lock };
			_stamps = { .door = a_now };
		}

		void Menu(std::string_view a_menu, bool a_opening, std::int64_t a_now)
		{
			// An override may remove the fade-in: then no Fader Menu is left to close after the loading screen.
			const bool closing = a_menu == RE::LoadingMenu::MENU_NAME && !a_opening;
			auto*       ui = closing ? RE::UI::GetSingleton() : nullptr;
			const bool  noFadeIn = ui && !ui->IsMenuOpen(RE::FaderMenu::MENU_NAME);

			std::optional<Stamps> done;
			{
				const std::scoped_lock lock{ _lock };
				auto&                  s = _stamps;
				if (a_menu == RE::LoadingMenu::MENU_NAME) {
					if (a_opening) {
						if (!s.door || s.loadingOpen || a_now - *s.door > kStaleUs) {
							s = {};
						}
						s.loadingOpen = a_now;
					} else if (s.loadingOpen) {
						s.loadingClose = a_now;
						if (noFadeIn) {
							done = s;
							s = {};
						}
					}
				} else if (a_menu == RE::FaderMenu::MENU_NAME) {
					if (a_opening && s.door && !s.fadeOut && !s.loadingOpen) {
						s.fadeOut = a_now;
					} else if (!a_opening && s.loadingClose) {
						done = s;
						s = {};
					}
				}
			}
			if (done) {
				Report(*done, a_now);
			}
		}

	private:
		static constexpr std::int64_t kStaleUs = 10'000'000;

		struct Stamps
		{
			std::optional<std::int64_t> door;
			std::optional<std::int64_t> fadeOut;  // Fader Menu open after the door
			std::optional<std::int64_t> loadingOpen;
			std::optional<std::int64_t> loadingClose;
		};

		static void Report(const Stamps& a_stamps, std::int64_t a_fadeInEnd)
		{
			const auto ms = [](const std::optional<std::int64_t>& a_from, const std::optional<std::int64_t>& a_to) -> std::int64_t {
				return a_from && a_to ? (*a_to - *a_from) / 1000 : -1;
			};
			const std::optional<std::int64_t> end{ a_fadeInEnd };
			const auto&                       start = a_stamps.door ? a_stamps.door : a_stamps.loadingOpen;
			const auto                        total = ms(start, end);
			const auto                        wait = ms(a_stamps.door, a_stamps.fadeOut);
			const auto                        fadeOut = ms(a_stamps.fadeOut, a_stamps.loadingOpen);
			const auto                        loading = ms(a_stamps.loadingOpen, a_stamps.loadingClose);
			const auto                        fadeIn = ms(a_stamps.loadingClose, end);

			Trace::Write("transition.visible", Trace::Args{}
												   .Bool("door", a_stamps.door.has_value())
												   .Int("total_ms", total)
												   .Int("doorToFadeOut_ms", wait)
												   .Int("fadeOut_ms", fadeOut)
												   .Int("loading_ms", loading)
												   .Int("fadeIn_ms", fadeIn)
												   .Int("overrideSet", Overrides::Active()),
				nullptr, true);
			logs::info("[Transition] {} -> picture {} ms: door->fade-out {} | fade-out {} | loading {} | fade-in {} (ms), override set {}",
				a_stamps.door ? "door" : "loading screen", total, wait, fadeOut, loading, fadeIn, Overrides::Active());
		}

		std::mutex _lock;
		Stamps     _stamps;
	};

	class EventSink final :
		public RE::BSTEventSink<RE::PositionPlayerEvent>,
		public RE::BSTEventSink<RE::BGSActorCellEvent>,
		public RE::BSTEventSink<RE::TESCellAttachDetachEvent>,
		public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
		public RE::BSTEventSink<RE::TESMoveAttachDetachEvent>,
		public RE::BSTEventSink<RE::TESActorLocationChangeEvent>,
		public RE::BSTEventSink<RE::TESFastTravelEndEvent>,
		public RE::BSTEventSink<RE::TESLoadGameEvent>,
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		using Result = RE::BSEventNotifyControl;

		static EventSink* GetSingleton()
		{
			static EventSink singleton;
			return &singleton;
		}

		// kPre -> (kPreUpdatePackages -> kPostUpdatePackages) -> kPost -> kFinish
		Result ProcessEvent(const RE::PositionPlayerEvent* a_event, RE::BSTEventSource<RE::PositionPlayerEvent>*) override
		{
			if (!a_event) {
				return Result::kContinue;
			}

			using Type = RE::PositionPlayerEvent::EVENT_TYPE;
			const auto type = a_event->type.get();
			const auto index = static_cast<std::size_t>(std::to_underlying(type));

			Trace::Args args;
			args.Int("type", static_cast<std::int64_t>(index))
				.Str("name", index < kPositionPlayerTypes.size() ? kPositionPlayerTypes[index] : "?"sv);

			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				const auto& runtime = player->GetPlayerRuntimeData();
				args.Cell("parentCell", player->GetParentCell())
					.Bool("isLoading", runtime.playerFlags.isLoading)
					.Bool("travelUseDoor", runtime.playerFlags.travelUseDoor)
					.Bool("fastTraveling", runtime.playerFlags.fastTraveling);

				if (type == Type::kPre) {
					const auto& target = runtime.queuedTargetLoc;
					args.Bool("target_valid", target.isValid)
						.Form("target_world", target.world)
						.Cell("target_interior", target.interior)
						.Pos("target_pos", target.location)
						.Hex("target_arrivalFunc", reinterpret_cast<std::uintptr_t>(target.arrivalFunc))
						.Bool("target_resetWeather", target.resetWeather)
						.Bool("target_allowAutoSave", target.allowAutoSave);
				} else if (type == Type::kFinish) {
					args.Form("location", player->GetCurrentLocation())
						.Pos("pos", player->GetPosition());
				}
				if (type == Type::kPost || type == Type::kFinish) {
					// Does kPost -> kFinish (Havok code in the 0.5.0 profile) grow with the number of actors?
					if (const auto* lists = RE::ProcessLists::GetSingleton()) {
						args.Int("highActors", lists->highActorHandles.size())
							.Int("middleHighActors", lists->middleHighActorHandles.size())
							.Int("lowActors", lists->lowActorHandles.size());
					}
					// Is kPost -> kFinish waiting for the cell's references to load? (0.5.0: no, all loaded at kPost)
					auto* cell = player->GetParentCell();
					if (const auto* loaded = cell ? cell->GetRuntimeData().loadedData : nullptr) {
						args.Bool("refsFullyLoaded", loaded->refsFullyLoaded)
							.Int("queuedRefs", loaded->queuedRefCount)
							.Int("criticalQueuedRefs", loaded->criticalQueuedRefCount)
							.Int("queuedDistantRefs", loaded->queuedDistantRefCount);
					}
				}
			}

			Trace::Write("event.PositionPlayer", args, nullptr, type == Type::kFinish);
			Preload::CheckStates("PositionPlayer");

			const auto now = Trace::NowUs();
			switch (type) {
			case Type::kPre:
				_timing.pre = now;
				_timing.enter.reset();
				_timing.post.reset();
				_timing.finish.reset();
				_timing.physicsAtPost.reset();
				_timing.physicsAtFinish.reset();
				Profiler::SetPhase(Profiler::Phase::kPreToEnter);
				break;
			case Type::kPost:
				_timing.post = now;
				_timing.physicsAtPost = Physics::Total();
				Profiler::SetPhase(Profiler::Phase::kPostToFinish);
				break;
			case Type::kFinish:
				_timing.finish = now;
				_timing.physicsAtFinish = Physics::Total();
				Profiler::SetPhase(Profiler::Phase::kFinishToClose);
				Snapshot::Take("kFinish");
				Probe::Request("afterTransition");  // runs on the first Update tick after the loading screen
				break;
			default:
				break;
			}
			return Result::kContinue;
		}

		// Player entered/left a cell, including walking across exterior cell borders
		Result ProcessEvent(const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>*) override
		{
			if (a_event) {
				const bool entered = a_event->flags.get() == RE::BGSActorCellEvent::CellFlag::kEnter;
				auto*      cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(a_event->cellID);
				Trace::Write("event.ActorCell", Trace::Args{}
													.Str("flag", entered ? "enter" : "leave")
													.FormID("cellID", a_event->cellID)
													.Cell("cell", cell));
				Preload::CheckStates(entered ? "ActorCell.enter" : "ActorCell.leave");

				if (entered && _timing.pre && !_timing.enter) {
					// kPre -> ActorCell enter: the cell load itself, without the loading screen around it
					_timing.enter = Trace::NowUs();
					Profiler::SetPhase(Profiler::Phase::kEnterToPost);
				}
			}
			return Result::kContinue;
		}

		Result ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
		{
			if (a_event && (Settings::Get().logCellAttachPerRef || IsPlayer(a_event->reference.get()))) {
				Trace::Write("event.CellAttachDetach", Trace::Args{}
														   .Form("ref", a_event->reference.get())
														   .Bool("attached", a_event->attached));
			}
			return Result::kContinue;
		}

		// All refs of the cell and their 3D are loaded. Fires on worker threads for exterior cells.
		Result ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
		{
			if (a_event && a_event->cell) {
				Trace::Write("event.CellFullyLoaded", Trace::Args{}
														  .Cell("cell", a_event->cell)
														  .Bool("interior", a_event->cell->IsInteriorCell()));
			}
			return Result::kContinue;
		}

		Result ProcessEvent(const RE::TESMoveAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESMoveAttachDetachEvent>*) override
		{
			if (a_event && (Settings::Get().logCellAttachPerRef || IsPlayer(a_event->movedRef.get()))) {
				Trace::Write("event.MoveAttachDetach", Trace::Args{}
														   .Form("ref", a_event->movedRef.get())
														   .Bool("isCellAttached", a_event->isCellAttached));
			}
			return Result::kContinue;
		}

		// BGSLocation change, not a cell change
		Result ProcessEvent(const RE::TESActorLocationChangeEvent* a_event, RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override
		{
			if (a_event && IsPlayer(a_event->actor.get())) {
				Trace::Write("event.ActorLocationChange", Trace::Args{}
															  .Form("oldLoc", a_event->oldLoc)
															  .Form("newLoc", a_event->newLoc));
			}
			return Result::kContinue;
		}

		Result ProcessEvent(const RE::TESFastTravelEndEvent* a_event, RE::BSTEventSource<RE::TESFastTravelEndEvent>*) override
		{
			if (a_event) {
				Trace::Write("event.FastTravelEnd", Trace::Args{}.Num("hours", a_event->fastTravelEndHours), nullptr, true);
			}
			return Result::kContinue;
		}

		Result ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
		{
			Trace::Write("event.LoadGame", {}, nullptr, true);
			return Result::kContinue;
		}

		Result ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (!a_event) {
				return Result::kContinue;
			}

			const bool loading = a_event->menuName == RE::LoadingMenu::MENU_NAME;
			Trace::Write("event.Menu", Trace::Args{}
										   .Str("menu", a_event->menuName.c_str())
										   .Bool("opening", a_event->opening),
				nullptr, loading);
			Journey::Get().Menu(a_event->menuName.c_str(), a_event->opening, Trace::NowUs());

			if (loading) {
				Preload::CheckStates(a_event->opening ? "LoadingMenu.open" : "LoadingMenu.close");
				if (a_event->opening) {
					// Before the game touches cells: the kept interior's 3D must not stay in the exterior scene,
					// and its physics is not stepped until the loading screen closes.
					InteriorSim::SetTransition(true);
					InteriorSim::Detach("LoadingMenu.open");
					_timing = { .open = Trace::NowUs(), .physicsAtOpen = Physics::Total() };
					if (Settings::Get().profileLoadingScreen) {
						Profiler::Start();
					}
					logs::info("[LoadingMenu] open");
				} else {
					Profiler::Stop();
					InteriorSim::SetTransition(false);
					g_loadingClosedUs = Trace::NowUs();
					ReportLoadingScreen(Trace::NowUs());
					_timing = {};
					Snapshot::Take("LoadingMenu.close");
				}
			}
			return Result::kContinue;
		}

	private:
		// Timestamps (us) of the transition in progress.
		struct Timing
		{
			std::optional<std::int64_t> open;    // Loading Menu open
			std::optional<std::int64_t> pre;     // kPre
			std::optional<std::int64_t> enter;   // ActorCell enter after kPre
			std::optional<std::int64_t> post;    // kPost
			std::optional<std::int64_t> finish;  // kFinish

			// Havok steps counted by hkpWorld::stepDeltaTime at the same moments
			std::optional<Physics::Totals> physicsAtOpen;
			std::optional<Physics::Totals> physicsAtPost;
			std::optional<Physics::Totals> physicsAtFinish;
		};

		// Where the loading screen time went: the trace, the log and a HUD line.
		void ReportLoadingScreen(std::int64_t a_closeUs)
		{
			if (!_timing.open) {
				logs::info("[LoadingMenu] close without a seen open");
				return;
			}
			const auto span = [](const std::optional<std::int64_t>& a_from, const std::optional<std::int64_t>& a_to) -> std::int64_t {
				return a_from && a_to ? *a_to - *a_from : -1;
			};
			const std::optional<std::int64_t> close{ a_closeUs };
			const auto total = a_closeUs - *_timing.open;
			const auto openToPre = span(_timing.open, _timing.pre);
			const auto cell = span(_timing.pre, _timing.enter);
			const auto enterToPost = span(_timing.enter, _timing.post);
			const auto postToFinish = span(_timing.post, _timing.finish);
			const auto finishToClose = span(_timing.finish, close);

			Trace::Args summary;
			summary.Int("total_us", total)
				.Int("openToPre_us", openToPre)
				.Int("cellLoad_us", cell)
				.Int("enterToPost_us", enterToPost)
				.Int("postToFinish_us", postToFinish)
				.Int("finishToClose_us", finishToClose)
				.Num("warmStart", Physics::WarmStartValue())
				.Int("overrideSet", Overrides::Active());

			// Havok steps under the loading screen, and in kPost -> kFinish (the warm start)
			const auto physicsNow = Physics::Total();
			std::string physicsText;
			if (_timing.physicsAtOpen) {
				summary.Int("physicsSteps", static_cast<std::int64_t>(physicsNow.steps - _timing.physicsAtOpen->steps));
			}
			if (_timing.physicsAtPost && _timing.physicsAtFinish) {
				const auto& from = *_timing.physicsAtPost;
				const auto& to = *_timing.physicsAtFinish;
				summary.Int("postToFinish_physicsSteps", static_cast<std::int64_t>(to.steps - from.steps))
					.Num("postToFinish_physicsSimSeconds", to.simSeconds - from.simSeconds)
					.Int("postToFinish_physicsStep_us", to.stepUs - from.stepUs);
				physicsText = std::format(" ({} physics steps, {:.2f} s simulated)", to.steps - from.steps, to.simSeconds - from.simSeconds);
			}
			Trace::Write("loading.summary", summary, nullptr, true);
			logs::info("[LoadingMenu] close after {} ms: open->kPre {} | kPre->enter (cell) {} | enter->kPost {} | kPost->kFinish {}{} | kFinish->close {} (us), fWarmStartMaxTime {}",
				total / 1000, openToPre, cell, enterToPost, postToFinish, physicsText, finishToClose, Physics::WarmStartValue());

			// No HUD line since 0.8.1: the "door -> picture" line after the fade-in includes the loading screen.
		}

		Timing      _timing;
	};

	class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputSink* GetSingleton()
		{
			static InputSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			for (auto* event = a_event ? *a_event : nullptr; event; event = event->next) {
				if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || event->GetDevice() != RE::INPUT_DEVICES::kKeyboard) {
					continue;
				}
				const auto* button = event->AsButtonEvent();
				if (button && button->IsDown() && button->GetIDCode() == Settings::Get().overridesKey) {
					Trace::Write("input.hotkey", Trace::Args{}.Str("action", "cycleOverrides"));
					MainThread::Post(Overrides::Cycle);
				}
				if (button && button->IsDown() && button->GetIDCode() == Settings::Get().detectorsKey) {
					Trace::Write("input.hotkey", Trace::Args{}.Str("action", "detectorsReport"));
					MainThread::Post(Detectors::Report);
				}
				if (button && button->IsDown() && button->GetIDCode() == Settings::Get().interiorSimKey) {
					Trace::Write("input.hotkey", Trace::Args{}.Str("action", "interiorSimCycle"));
					MainThread::Post(InteriorSim::Cycle);
				}
				if (button && button->IsDown() && button->GetIDCode() == Settings::Get().toggleTemporalAAKey) {
					Trace::Write("input.hotkey", Trace::Args{}.Str("action", "toggleTemporalAA"));
					MainThread::Post(Portal::ToggleTemporalAA);
				}
				if (button && button->IsDown() && button->GetIDCode() == Settings::Get().portalGradingKey) {
					Trace::Write("input.hotkey", Trace::Args{}.Str("action", "portalGrading"));
					MainThread::Post(Portal::TogglePortalGrading);
				}
				if (button && button->IsDown() && button->GetIDCode() == Settings::Get().accessWatchKey) {
					Trace::Write("input.hotkey", Trace::Args{}.Str("action", "accessWatch"));
					MainThread::Post(AccessWatch::Start);
				}
				if (button && button->IsDown() && button->GetIDCode() == Settings::Get().snapshotKey) {
					Trace::Write("input.hotkey", Trace::Args{}.Str("action", "snapshot"));
					Snapshot::Take("hotkey");
					Probe::Request("hotkey");
					MainThread::Post(Portal::RequestDump);
					Hud::Show("snapshot", std::format("CLR: snapshot #{} written to the trace", ++_snapshots));
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		std::atomic<std::uint32_t> _snapshots{ 0 };
	};
}

namespace Events
{
	void Register()
	{
		auto* sink = EventSink::GetSingleton();

		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			player->AsPositionPlayerEventSource()->AddEventSink(static_cast<RE::BSTEventSink<RE::PositionPlayerEvent>*>(sink));
			player->AsBGSActorCellEventSource()->AddEventSink(static_cast<RE::BSTEventSink<RE::BGSActorCellEvent>*>(sink));
		} else {
			logs::error("PlayerCharacter is not available, player events are not registered");
		}

		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESCellAttachDetachEvent>(sink);
			holder->AddEventSink<RE::TESCellFullyLoadedEvent>(sink);
			holder->AddEventSink<RE::TESMoveAttachDetachEvent>(sink);
			holder->AddEventSink<RE::TESActorLocationChangeEvent>(sink);
			holder->AddEventSink<RE::TESFastTravelEndEvent>(sink);
			holder->AddEventSink<RE::TESLoadGameEvent>(sink);
		}

		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(sink);
		}

		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink(InputSink::GetSingleton());
		}

		logs::info("events registered");
	}

	void OnPlayerDoorActivated()
	{
		Journey::Get().Door(Trace::NowUs());
	}

	void OnPlayerTick()
	{
		constexpr std::int64_t kHitchUs = 50'000;
		constexpr std::int64_t kWindowUs = 5'000'000;

		static std::int64_t last = -1;
		const auto          now = Trace::NowUs();
		const auto          closed = g_loadingClosedUs.load();
		// Only gaps that start after the loading screen: the frame counter stands still under it.
		if (last >= 0 && closed >= 0 && last >= closed && now - closed < kWindowUs && now - last > kHitchUs) {
			Trace::Write("frame.hitch", Trace::Args{}
											.Int("gap_ms", (now - last) / 1000)
											.Int("afterLoading_ms", (last - closed) / 1000)
											.Int("overrideSet", Overrides::Active()));
			logs::info("[Hitch] {} ms frame, {} ms after the loading screen", (now - last) / 1000, (last - closed) / 1000);
		}
		last = now;
	}
}

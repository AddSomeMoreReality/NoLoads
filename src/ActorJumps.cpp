#include "ActorJumps.h"

#include "Settings.h"
#include "Trace.h"

namespace
{
	constexpr float         kJumpUnits = 150.0f;        // a sprint covers about 10 units a frame at 60 fps
	constexpr std::int64_t  kStackMatchUs = 500'000;    // a SetPosition this recent explains a jump
	constexpr std::size_t   kMaxStacks = 64;
	constexpr std::uint64_t kForgetFrames = 36'000;     // ten minutes at 60 fps

	struct Last
	{
		RE::NiPoint3  position;
		RE::FormID    cell{ 0 };
		std::uint64_t frame{ 0 };
	};
	std::unordered_map<RE::FormID, Last> g_last;  // main thread

	struct Moved
	{
		RE::FormID   actor{ 0 };
		std::int64_t us{ 0 };
		RE::NiPoint3 from;
		RE::NiPoint3 to;
		bool         mainThread{ false };
		std::string  stack;
	};
	std::mutex        g_movesLock;
	std::deque<Moved> g_moves;
	std::uint64_t     g_lastPrune{ 0 };

	std::string Stack()
	{
		std::array<void*, 14> frames{};
		const auto            count = ::RtlCaptureStackBackTrace(2, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
		std::string           stack;
		for (std::size_t i = 0; i < count; ++i) {
			stack += (stack.empty() ? "" : ",") + Trace::JsonString(Trace::DescribeAddress(reinterpret_cast<std::uintptr_t>(frames[i]), true));
		}
		return "[" + stack + "]";
	}

	// The SetPosition behind a jump of a_actor, if one was recorded lately.
	std::optional<Moved> TakeMove(RE::FormID a_actor, std::int64_t a_now)
	{
		const std::scoped_lock lock{ g_movesLock };
		std::optional<Moved>   found;
		std::erase_if(g_moves, [&](const Moved& a_move) {
			if (a_now - a_move.us > kStackMatchUs) {
				return true;
			}
			if (a_move.actor == a_actor) {
				found = a_move;
				return true;
			}
			return false;
		});
		return found;
	}

	void Check(const RE::BSTArray<RE::ActorHandle>& a_handles, std::string_view a_level, std::uint64_t a_frame, std::int64_t a_now)
	{
		for (const auto& handle : a_handles) {
			const auto actorPtr = handle.get();
			auto*      actor = actorPtr.get();
			if (!actor || actor->IsPlayerRef()) {
				continue;
			}
			auto*      cell = actor->GetParentCell();
			const auto cellID = cell ? cell->GetFormID() : 0;
			const auto position = actor->GetPosition();
			auto [it, added] = g_last.try_emplace(actor->GetFormID(), Last{ position, cellID, a_frame });
			if (added) {
				continue;
			}
			const auto last = it->second;
			it->second = { position, cellID, a_frame };
			const auto distance = position.GetDistance(last.position);
			const auto gap = a_frame - last.frame;  // 1 = seen last frame; more = it came back to these lists
			const bool newCell = cellID != last.cell;
			if (!newCell && distance <= kJumpUnits) {
				continue;
			}
			const auto* package = actor->GetCurrentPackage();
			const auto* process = actor->GetActorRuntimeData().currentProcess;
			Trace::Args args;
			args.Form("actor", actor)
				.Str("name", actor->GetName())
				.Str("level", a_level)
				.Int("processLevel", process ? static_cast<std::int64_t>(std::to_underlying(process->processLevel.get())) : -1)
				.Str("kind", newCell ? "cell" : gap > 1 ? "returned" : "jump")
				.Pos("from", last.position)
				.Pos("to", position)
				.Num("distance", distance)
				.Int("frames", static_cast<std::int64_t>(gap))
				.FormID("fromCell", last.cell)
				.Cell("toCell", cell)
				.FormID("package", package ? package->GetFormID() : 0);
			if (const auto move = TakeMove(actor->GetFormID(), a_now)) {
				args.Pos("setFrom", move->from).Pos("setTo", move->to).Bool("setOnMain", move->mainThread).Raw("stack", move->stack);
			}
			Trace::Write("actor.jump", args);
		}
	}
}

namespace ActorJumps
{
	void Tick()
	{
		if (!Settings::Get().probeActorJumps) {
			return;
		}
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return;
		}
		const auto frame = Trace::Frame();
		const auto now = Trace::NowUs();
		Check(lists->highActorHandles, "high", frame, now);
		Check(lists->middleHighActorHandles, "middleHigh", frame, now);
		if (frame - g_lastPrune > 600) {
			g_lastPrune = frame;
			std::erase_if(g_last, [&](const auto& a_entry) { return frame - a_entry.second.frame > kForgetFrames; });
		}
	}

	void BeforeSetPosition(const RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_position)
	{
		if (!a_ref || a_ref->GetFormType() != RE::FormType::ActorCharacter || a_ref->IsPlayerRef()) {
			return;
		}
		const auto from = a_ref->GetPosition();
		if (from.GetDistance(a_position) <= kJumpUnits || !Settings::Get().probeActorJumps) {
			return;
		}
		auto*      main = RE::Main::GetSingleton();
		Moved      move{ a_ref->GetFormID(), Trace::NowUs(), from, a_position, main && ::GetCurrentThreadId() == main->threadID, Stack() };
		const std::scoped_lock lock{ g_movesLock };
		g_moves.push_back(std::move(move));
		if (g_moves.size() > kMaxStacks) {
			g_moves.pop_front();
		}
	}
}

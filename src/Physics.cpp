#include "Physics.h"

#include "Trace.h"

namespace
{
	std::mutex                                                g_lock;
	Physics::Totals                                           g_totals;
	std::unordered_map<std::uintptr_t, Physics::WorldSteps>   g_worlds;  // since the last TakeWorldSteps

	RE::Setting*      g_warmStart{ nullptr };
	std::atomic<bool> g_inWarmStart{ false };
}

namespace Physics
{
	void OnStep(const void* a_world, float a_deltaTime, std::int64_t a_durationUs)
	{
		const std::scoped_lock lock{ g_lock };
		++g_totals.steps;
		g_totals.simSeconds += a_deltaTime;
		g_totals.stepUs += a_durationUs;

		const auto key = reinterpret_cast<std::uintptr_t>(a_world);
		auto&      world = g_worlds.try_emplace(key, WorldSteps{ key, 0, 0.0, 0 }).first->second;
		++world.steps;
		world.simSeconds += a_deltaTime;
		world.mainThreadSteps += Trace::IsMainThread() ? 1 : 0;
	}

	Totals Total()
	{
		const std::scoped_lock lock{ g_lock };
		return g_totals;
	}

	std::vector<WorldSteps> TakeWorldSteps()
	{
		std::vector<WorldSteps> result;
		const std::scoped_lock  lock{ g_lock };
		result.reserve(g_worlds.size());
		for (const auto& [key, world] : g_worlds) {
			result.push_back(world);
		}
		g_worlds.clear();
		return result;
	}

	void InitWarmStart()
	{
		auto* ini = RE::INISettingCollection::GetSingleton();
		g_warmStart = ini ? ini->GetSetting("fWarmStartMaxTime:HAVOK") : nullptr;
		if (!g_warmStart) {
			logs::warn("fWarmStartMaxTime:HAVOK not found");
			return;
		}
		Trace::Write("havok.warmStart", Trace::Args{}.Str("action", "init").Num("game", g_warmStart->data.f), nullptr, true);
		logs::info("fWarmStartMaxTime:HAVOK = {}", g_warmStart->data.f);
	}

	void SetInWarmStart(bool a_value)
	{
		g_inWarmStart = a_value;
	}

	bool InWarmStart()
	{
		return g_inWarmStart;
	}

	float WarmStartValue()
	{
		return g_warmStart ? g_warmStart->data.f : -1.0f;
	}
}

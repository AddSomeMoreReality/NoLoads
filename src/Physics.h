#pragma once

// Havok step accounting and the warm start experiment.
// - hkpWorld::stepDeltaTime (AE#61410) is counted per world: the probe shows which cells' worlds
//   are really simulated (H11, H12), the loading screen report shows how many steps run under it.
// - kPost -> kFinish is TES (as a PositionPlayerEvent sink) stepping the world: a Havok "warm start"
//   of fWarmStartMaxTime:HAVOK (4 s of simulated time by default). Overrides.* switches it in memory.
namespace Physics
{
	struct Totals
	{
		std::uint64_t steps{ 0 };
		double        simSeconds{ 0.0 };  // sum of the stepped delta times
		std::int64_t  stepUs{ 0 };        // wall time spent in stepDeltaTime
	};

	struct WorldSteps
	{
		std::uintptr_t world;
		std::uint64_t  steps;
		double         simSeconds;
		std::uint64_t  mainThreadSteps;  // which thread steps the worlds decides where a second world can be stepped
	};

	// Any thread: called by the stepDeltaTime hook.
	void OnStep(const void* a_world, float a_deltaTime, std::int64_t a_durationUs);

	// Running totals over all worlds.
	[[nodiscard]] Totals Total();

	// Per-world steps since the previous call (for the probe).
	[[nodiscard]] std::vector<WorldSteps> TakeWorldSteps();

	// After the data is loaded: finds fWarmStartMaxTime for WarmStartValue.
	void InitWarmStart();

	// Set by the TES PositionPlayerEvent hook while TES handles the event (the warm start runs there).
	void                SetInWarmStart(bool a_value);
	[[nodiscard]] bool  InWarmStart();

	// Current fWarmStartMaxTime, or a negative value when the setting is not found.
	[[nodiscard]] float WarmStartValue();
}

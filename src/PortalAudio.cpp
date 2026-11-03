#include "PortalState.h"

namespace Portal
{
	bool SpeakerThroughDoor(const RE::TESObjectREFR* a_speaker, RE::NiPoint3& a_position)
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		if (!space || !a_speaker || !g_subtitlesThroughDoor.load(std::memory_order_relaxed) || a_speaker->GetParentCell() != space->cell) {
			return false;
		}
		a_position = space->toExterior * a_position;
		++g_subtitlesCarried;
		return true;
	}

	bool SoundThroughDoor(RE::NiPoint3& a_position)
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		if (!space || !g_soundThroughDoor.load(std::memory_order_relaxed) || a_position.GetDistance(space->center) > space->radius) {
			return false;
		}
		if (a_position.x == 0.0f && a_position.y == 0.0f && a_position.z == 0.0f) {
			return false;  // a sound with no place of its own
		}
		a_position = space->toExterior * a_position;
		g_lastSound = a_position;
		g_soundsCarried.fetch_add(1, std::memory_order_relaxed);
		return true;
	}
}

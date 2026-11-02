#pragma once

// Where NPCs jump (0.8.25, [Probe] bActorJumps). Run 0.8.24: entering the tavern, Idolaf stood at once
// at the bard's audience mark though he had been in the street, and Saadia jumped from place to place
// inside. The probe, every 5 s, cannot tell a walk from a jump. Every frame the high and middle-high
// actors are compared with the last frame: a step longer than a sprint, a change of cell, or a return to
// those lists far from where the actor left them goes to the trace (actor.jump), with the stack of the
// SetPosition call behind it when there was one.
namespace ActorJumps
{
	// Main thread, every PlayerCharacter::Update.
	void Tick();

	// Detour of TESObjectREFR::SetPosition (AE#19790), any thread, before the original.
	void BeforeSetPosition(const RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_position);
}

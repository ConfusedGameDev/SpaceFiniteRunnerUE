#pragma once

#include "CoreMinimal.h"

/**
 * Unity.Mathematics.Random, bit for bit: a 32-bit xorshift whose state is what a saved track stores
 * (FTrackSectionRecord::RngState), so a section replayed from it rolls exactly what Unity rolled.
 * The generator and every spawner draw from one of these, never from FRandomStream.
 */
struct FRunnerRandom
{
	uint32 State = 0x6E624EB7u;

	FRunnerRandom() = default;

	/** Seeds like `new Random(seed)`: a zero seed is invalid in Unity, so it is bumped to 1, and the first state is skipped. */
	explicit FRunnerRandom(uint32 Seed)
	{
		State = Seed == 0u ? 1u : Seed;
		NextState();
	}

	/** Wraps an existing state like `new Random { state = s }` (no skip). */
	static FRunnerRandom FromState(uint32 InState)
	{
		FRunnerRandom R;
		R.State = InState == 0u ? 1u : InState;
		return R;
	}

	uint32 NextState()
	{
		const uint32 T = State;
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return T;
	}

	/** [0, 1). */
	float NextFloat()
	{
		const uint32 Bits = 0x3f800000u | (NextState() >> 9);
		float F;
		FMemory::Memcpy(&F, &Bits, sizeof(F));
		return F - 1.f;
	}

	/** [Min, Max). */
	float NextFloat(float Min, float Max) { return NextFloat() * (Max - Min) + Min; }

	/** [Min, Max). */
	int32 NextInt(int32 Min, int32 Max)
	{
		const uint32 Range = static_cast<uint32>(Max - Min);
		return static_cast<int32>((static_cast<uint64>(NextState()) * static_cast<uint64>(Range)) >> 32) + Min;
	}

	uint32 NextUInt() { return NextState() - 1u; }

	bool NextBool() { return (NextState() & 1u) == 1u; }
};

#pragma once

#include "CoreMinimal.h"

/**
 * The draw every weighted table shares (the feature table, the speed-orb tiers): normalised by the
 * table's sum, so it stays right while the shares are being edited. The port of Unity's
 * WeightedTable.Pick. Entries are read through a projection, so any array of anything with a
 * probability share works.
 */
struct FWeightedTable
{
	/**
	 * One draw off a 0..1 roll. An excluded entry weighs nothing and is skipped. Returns the winning
	 * index, or INDEX_NONE when nothing else is left. A table whose shares sum to zero gives its first
	 * entry (its last remaining one when something is excluded), as Unity does.
	 */
	template <typename EntryType, typename ProjectionType>
	static int32 Pick(const TArray<EntryType>& Table, ProjectionType&& Probability, float Roll01, int32 Exclude = INDEX_NONE)
	{
		float Total = 0.f;
		int32 Last = INDEX_NONE;
		for (int32 i = 0; i < Table.Num(); ++i)
		{
			if (i == Exclude) continue;
			Total += Probability(Table[i]);
			Last = i;
		}
		if (Last == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		if (Total <= 0.f)
		{
			return Exclude == INDEX_NONE ? 0 : Last;
		}

		float Roll = Roll01 * Total;
		for (int32 i = 0; i < Table.Num(); ++i)
		{
			if (i == Exclude) continue;
			Roll -= Probability(Table[i]);
			if (Roll <= 0.f)
			{
				return i;
			}
		}
		return Last;
	}
};

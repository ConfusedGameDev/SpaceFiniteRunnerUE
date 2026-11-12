#pragma once

#include "CoreMinimal.h"

/** Colours as the data authors them (sRGB, Unity's inspector) and the material parameters that take them. */
namespace RunnerColor
{
	inline const FName BaseColorParam(TEXT("BaseColor"));
	inline const FName EmissionColorParam(TEXT("EmissionColor"));

	/** One sRGB channel to linear: the exact curve in [0, 1], a 2.2 power past it (HDR values). */
	inline float ToLinear(float C)
	{
		C = FMath::Max(C, 0.f);
		if (C > 1.f) return FMath::Pow(C, 2.2f);
		return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
	}

	/** An authored sRGB colour to the linear value a material parameter wants; alpha is kept as is. */
	inline FLinearColor FromAuthored(const FLinearColor& Srgb)
	{
		return FLinearColor(ToLinear(Srgb.R), ToLinear(Srgb.G), ToLinear(Srgb.B), Srgb.A);
	}
}

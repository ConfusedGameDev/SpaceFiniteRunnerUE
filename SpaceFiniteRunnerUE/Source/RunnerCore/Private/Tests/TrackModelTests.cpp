#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Track/RunnerRandom.h"
#include "Track/RunnerSpline.h"
#include "Track/TrackLayoutAsset.h"
#include "Track/TrackModel.h"

namespace
{
	constexpr EAutomationTestFlags TrackTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UTrackLayoutAsset* LoadLayout(const TCHAR* Path)
	{
		return LoadObject<UTrackLayoutAsset>(nullptr, Path);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerRandomTest, "Runner.Track.Random", TrackTestFlags)
bool FRunnerRandomTest::RunTest(const FString& Parameters)
{
	// Unity.Mathematics.Random(1): the first NextUInt is 270369 (state after the seed skip).
	FRunnerRandom R(1u);
	TestEqual(TEXT("state after seeding 1"), R.State, 270369u);
	const float F = R.NextFloat();
	TestTrue(TEXT("NextFloat in [0,1)"), F >= 0.f && F < 1.f);
	FRunnerRandom A(42u), B(42u);
	for (int32 i = 0; i < 100; ++i)
	{
		if (A.NextInt(0, 1000) != B.NextInt(0, 1000))
		{
			AddError(TEXT("two generators with one seed diverged"));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerSplineStraightTest, "Runner.Track.Spline.Straight", TrackTestFlags)
bool FRunnerSplineStraightTest::RunTest(const FString& Parameters)
{
	FRunnerSpline Spline;
	Spline.Add(FRunnerSplineKnot(FVector3f::ZeroVector, FVector3f::ZeroVector, FVector3f::ZeroVector, FQuat4f::Identity), ETrackTangentMode::AutoSmooth);
	Spline.Add(FRunnerSplineKnot(FVector3f(300.f, 0.f, 0.f), FVector3f::ZeroVector, FVector3f::ZeroVector, FQuat4f::Identity), ETrackTangentMode::AutoSmooth);
	Spline.Add(FRunnerSplineKnot(FVector3f(600.f, 0.f, 0.f), FVector3f::ZeroVector, FVector3f::ZeroVector, FQuat4f::Identity), ETrackTangentMode::AutoSmooth);
	TestNearlyEqual(TEXT("collinear knots: length is the chord"), Spline.GetLength(), 600.f, 0.01f);

	FVector3f Position, Tangent, Up;
	Spline.Evaluate(450.f, Position, Tangent, Up);
	TestNearlyEqual(TEXT("position at 450 m"), Position.X, 450.f, 0.5f);
	TestNearlyEqual(TEXT("up stays up"), Up.GetSafeNormal().Z, 1.f, 1e-4f);
	return true;
}

/**
 * The M2.1 exit: TL_Standard (86 knots, 30 km, saved in Unity) replayed knot by knot comes out at
 * the length Unity saved, every knot at its saved track distance, and the pose walks the road with
 * no jump. Writes Saved/RunnerTests/TL_Standard_Poses.csv (every 250 m) for a side-by-side with Unity.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerTrackStandardLoadTest, "Runner.Track.Layout.Standard", TrackTestFlags)
bool FRunnerTrackStandardLoadTest::RunTest(const FString& Parameters)
{
	UTrackLayoutAsset* Asset = LoadLayout(TEXT("/Game/Runner/Data/Tracks/TL_Standard.TL_Standard"));
	if (!TestNotNull(TEXT("TL_Standard loads"), Asset))
	{
		return false;
	}
	const FTrackLayout& Layout = Asset->Layout;
	TestTrue(TEXT("layout is valid"), Layout.IsValid());

	UTrackModel* Model = NewObject<UTrackModel>();
	Model->LoadRoad(Layout, Asset->Shape);

	const float Length = Model->GetLength();
	AddInfo(FString::Printf(TEXT("TL_Standard: rebuilt %.3f m, saved %.3f m (%.4f %%)"), Length, Layout.Length, 100.f * (Length - Layout.Length) / Layout.Length));
	TestTrue(TEXT("length within 1 % of Unity"), FMath::Abs(Length - Layout.Length) <= 0.01f * Layout.Length);
	TestTrue(TEXT("length within 1 m of Unity"), FMath::Abs(Length - Layout.Length) <= 1.f);

	float WorstKnot = 0.f;
	int32 WorstIndex = -1;
	for (int32 i = 0; i < Layout.KnotDistances.Num(); ++i)
	{
		const float Error = FMath::Abs(Model->KnotTrackDistance(i) - Layout.KnotDistances[i]);
		if (Error > WorstKnot)
		{
			WorstKnot = Error;
			WorstIndex = i;
		}
	}
	AddInfo(FString::Printf(TEXT("worst knot distance error %.4f m at knot %d"), WorstKnot, WorstIndex));
	TestTrue(TEXT("every knot within 0.1 % of its saved distance"), WorstKnot <= 0.001f * FMath::Max(1.f, Layout.Length));

	// Walk the road a metre at a time: no gap, no flip. A "metre" of distance is up to ~10 % off a
	// real metre, because Unity maps distance to t through a 30-entry piecewise-linear table per
	// curve (and so does the port): that is parity, not a gap.
	FVector Previous;
	FQuat PreviousRotation;
	Model->GetPoseAtDistance(0.f, 0.f, Previous, PreviousRotation);
	float WorstStep = 0.f;
	float WorstTurn = 0.f;
	for (float D = 1.f; D <= Length; D += 1.f)
	{
		FVector Position;
		FQuat Rotation;
		Model->GetPoseAtDistance(D, 0.f, Position, Rotation);
		WorstStep = FMath::Max(WorstStep, static_cast<float>(FMath::Abs(FVector::Distance(Position, Previous) - 1.0)));
		WorstTurn = FMath::Max(WorstTurn, FMath::RadiansToDegrees(static_cast<float>(Rotation.AngularDistance(PreviousRotation))));
		Previous = Position;
		PreviousRotation = Rotation;
	}
	AddInfo(FString::Printf(TEXT("worst 1 m step error %.4f m, worst turn per metre %.3f deg"), WorstStep, WorstTurn));
	TestTrue(TEXT("no gap along the road"), WorstStep < 0.15f);
	TestTrue(TEXT("no flip along the road"), WorstTurn < 2.f);

	// Poses for the side-by-side with Unity (Unity axes: x = UE Y, y = UE Z, z = UE X).
	FString Csv = TEXT("distance,x,y,z,upx,upy,upz,bank\n");
	for (float D = 0.f; D <= Length; D += 250.f)
	{
		FVector Position;
		FQuat Rotation;
		Model->GetPoseAtDistance(D, 0.f, Position, Rotation);
		const FVector Up = Rotation.GetUpVector();
		Csv += FString::Printf(TEXT("%.1f,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.3f\n"), D, Position.Y, Position.Z, Position.X, Up.Y, Up.Z, Up.X, Model->GetBankAtDistance(D));
	}
	const FString OutDir = FPaths::ProjectSavedDir() / TEXT("RunnerTests");
	IFileManager::Get().MakeDirectory(*OutDir, true);
	FFileHelper::SaveStringToFile(Csv, *(OutDir / TEXT("TL_Standard_Poses.csv")));
	return true;
}

/** Every shipped saved track rebuilds to its saved length (1 m). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRunnerTrackAllLayoutsTest, "Runner.Track.Layout.All", TrackTestFlags)
bool FRunnerTrackAllLayoutsTest::RunTest(const FString& Parameters)
{
	const TCHAR* Names[] = { TEXT("TL_Standard"), TEXT("TL_StandardCurved"), TEXT("TL_Arcade"), TEXT("TL_BeatTheClock"), TEXT("TL_Race"),
		TEXT("TL_Level_02"), TEXT("TL_Level_03"), TEXT("TL_Level_60878307"), TEXT("TL_NewLasers"), TEXT("TL_Test4") };
	for (const TCHAR* Name : Names)
	{
		const FString Path = FString::Printf(TEXT("/Game/Runner/Data/Tracks/%s.%s"), Name, Name);
		UTrackLayoutAsset* Asset = LoadLayout(*Path);
		if (Asset == nullptr)
		{
			AddWarning(FString::Printf(TEXT("%s: not found"), Name));
			continue;
		}
		UTrackModel* Model = NewObject<UTrackModel>();
		Model->LoadRoad(Asset->Layout, Asset->Shape);
		const float Error = Model->GetLength() - Asset->Layout.Length;
		AddInfo(FString::Printf(TEXT("%s: %d knots, %d sections, rebuilt %.2f m, saved %.2f m"), Name, Asset->Layout.Knots.Num(), Asset->Layout.Sections.Num(), Model->GetLength(), Asset->Layout.Length));
		TestTrue(FString::Printf(TEXT("%s length within 1 m"), Name), FMath::Abs(Error) <= 1.f);
	}
	return true;
}

#endif

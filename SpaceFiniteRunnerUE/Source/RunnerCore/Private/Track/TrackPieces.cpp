#include "Track/TrackPieces.h"

#include "Engine/StaticMesh.h"
#include "Track/TrackModel.h"

namespace
{
	/**
	 * The roll USplineMeshComponent must carry at an end for the mesh's up to be Up there. It builds
	 * its frame as BaseX = SplineUpDir × Dir, BaseY = Dir × BaseX and rolls it; with ForwardAxis X the
	 * mesh's Z follows the rolled BaseY, with Y it follows the rolled BaseX (a quarter turn further).
	 */
	float SolveRoll(const FVector& SplineUpDir, const FVector& Dir, const FVector& Up, ESplineMeshAxis::Type ForwardAxis)
	{
		const FVector BaseX = (SplineUpDir ^ Dir).GetSafeNormal();
		const FVector BaseY = (Dir ^ BaseX).GetSafeNormal();
		const float Roll = FMath::Atan2(FVector::DotProduct(Up, BaseX), FVector::DotProduct(Up, BaseY));
		return ForwardAxis == ESplineMeshAxis::Y ? Roll - HALF_PI : Roll;
	}
}

void TrackPieces::Pose(USplineMeshComponent& Piece, const UTrackModel& Track, float From, float To, float Lateral, const FTrackPieceFit& Fit)
{
	FVector P0, P1;
	FQuat R0, R1;
	Track.GetPoseAtDistance(From, Lateral, P0, R0);
	Track.GetPoseAtDistance(To, Lateral, P1, R1);
	const FVector Up0 = R0.GetUpVector();
	const FVector Up1 = R1.GetUpVector();

	// Track space is metres; the generator's local space is centimetres.
	const FVector Start = (P0 + Up0 * Fit.UpOffset) * 100.0;
	const FVector End = (P1 + Up1 * Fit.UpOffset) * 100.0;
	const double Chord = FMath::Max(FVector::Distance(Start, End), 1.0);
	const FVector Dir0 = R0.GetForwardVector();
	const FVector Dir1 = R1.GetForwardVector();

	Piece.SetForwardAxis(Fit.ForwardAxis, false);
	Piece.SetSplineUpDir(Up0, false);
	Piece.SetStartAndEnd(Start, Dir0 * Chord, End, Dir1 * Chord, false);

	float Roll0 = SolveRoll(Up0, Dir0, Up0, Fit.ForwardAxis);
	float Roll1 = SolveRoll(Up0, Dir1, Up1, Fit.ForwardAxis);
	// The roll is interpolated linearly: never the long way round.
	while (Roll1 - Roll0 > PI) Roll1 -= TWO_PI;
	while (Roll1 - Roll0 < -PI) Roll1 += TWO_PI;
	Piece.SetStartRoll(Roll0, false);
	Piece.SetEndRoll(Roll1, false);

	// SplineMeshComponent's 2D scale is (mesh Y, Z) along X, (Z, mesh X) along Y.
	const FVector2D Scale = Fit.ForwardAxis == ESplineMeshAxis::Y
		? FVector2D(Fit.UpScale, Fit.AcrossScale)
		: FVector2D(Fit.AcrossScale, Fit.UpScale);
	Piece.SetStartScale(Scale, false);
	Piece.SetEndScale(Scale, false);
	Piece.SetStartOffset(FVector2D::ZeroVector, false);
	Piece.SetEndOffset(FVector2D::ZeroVector, false);
	Piece.UpdateMesh();
}

float TrackPieces::AcrossExtent(const UStaticMesh& Mesh, ESplineMeshAxis::Type ForwardAxis)
{
	const FBox Box = Mesh.GetBoundingBox();
	return static_cast<float>(ForwardAxis == ESplineMeshAxis::Y ? Box.GetSize().X : Box.GetSize().Y);
}

float TrackPieces::UpExtent(const UStaticMesh& Mesh)
{
	return static_cast<float>(Mesh.GetBoundingBox().GetSize().Z);
}

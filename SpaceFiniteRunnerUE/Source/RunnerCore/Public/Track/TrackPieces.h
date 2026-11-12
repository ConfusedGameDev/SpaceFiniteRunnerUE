#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"

class UTrackModel;

/**
 * How one kit mesh is fitted onto the track: which of its axes runs along the road, how it is
 * scaled across and up, and where its origin sits above the flight line. Unity bent every vertex
 * of a piece to its own track distance; here a USplineMeshComponent runs between the poses at the
 * piece's two ends, so neighbours that share an end pose meet exactly (the plan's choice: bent on
 * the GPU, no mesh copies).
 */
struct RUNNERCORE_API FTrackPieceFit
{
	/** The mesh axis that runs along the track (X for the road slab, Y for the Kenney tiles). */
	ESplineMeshAxis::Type ForwardAxis = ESplineMeshAxis::X;
	/** Scale on the mesh axis that spans the road. */
	float AcrossScale = 1.f;
	/** Scale on the mesh's up axis (Z). */
	float UpScale = 1.f;
	/** Where the mesh origin sits along the track's up, metres above the flight line. */
	float UpOffset = 0.f;
};

namespace TrackPieces
{
	/**
	 * Poses Piece between track distances From and To at Lateral, in the generator's local space
	 * (centimetres). Each end's roll is solved so the mesh's up is the track's up there exactly.
	 */
	RUNNERCORE_API void Pose(USplineMeshComponent& Piece, const UTrackModel& Track, float From, float To, float Lateral, const FTrackPieceFit& Fit);

	/** The mesh's extent across (the axis that is neither forward nor Z), in its own units. */
	RUNNERCORE_API float AcrossExtent(const UStaticMesh& Mesh, ESplineMeshAxis::Type ForwardAxis);
	/** The mesh's extent up (Z), in its own units. */
	RUNNERCORE_API float UpExtent(const UStaticMesh& Mesh);
}

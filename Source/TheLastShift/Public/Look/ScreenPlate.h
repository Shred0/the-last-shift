// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * Brief 27: a screen that shows nothing is black. A screen takes it from a flat plate laid over its face: the engine's
 * plane (/Engine/BasicShapes/Plane) scaled to the face, in UTheLastShiftSettings::ScreenMaterial (MI_Surface_Screen:
 * black, opaque, not emissive), a component of the actor that owns the screen. A plate collides with nothing, affects no
 * navigation, casts no shadow and is never captured by a scan (bHiddenInSceneCapture): a scan still sees the screen's
 * own face under it, so it shows nothing a scan did not show before.
 */
namespace ScreenPlate
{
	/** Distance of a plate in front of its face, cm. */
	constexpr float FaceGapCm = 0.25f;

	/** Path of the engine's plane: 100 x 100 cm in its X-Y plane, facing +Z, pivot at its centre. */
	inline const TCHAR* PlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");

	/** Gives Plate its mesh (Plane) and the flags above (safe in a constructor). */
	THELASTSHIFT_API void Setup(UStaticMeshComponent* Plate, UStaticMesh* Plane);

	/** Gives Plate the screens' material (at runtime; nothing on a class default). */
	THELASTSHIFT_API void ApplyMaterial(UStaticMeshComponent* Plate);

	/** Lays Plate (attached to the component whose frame the face is in) over a face: centre, normal, up, size (cm). */
	THELASTSHIFT_API void Place(UStaticMeshComponent* Plate, const FVector& Centre, const FVector& Normal, const FVector& Up, const FVector2D& Size);
}

// SPDX-License-Identifier: MIT

#include "Look/ScreenPlate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Run/TheLastShiftSettings.h"

void ScreenPlate::Setup(UStaticMeshComponent* Plate, UStaticMesh* Plane)
{
	if (!Plate)
	{
		return;
	}
	if (Plane)
	{
		Plate->SetStaticMesh(Plane);
	}
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->SetCollisionResponseToAllChannels(ECR_Ignore);
	Plate->SetGenerateOverlapEvents(false);
	Plate->SetCanEverAffectNavigation(false);
	Plate->SetCastShadow(false);
	Plate->bAffectDistanceFieldLighting = false;
	// The scan's capture sees the screen's own face under the plate: nothing a scan shows changes.
	Plate->bHiddenInSceneCapture = true;
	Plate->SetRenderCustomDepth(false);
}

void ScreenPlate::ApplyMaterial(UStaticMeshComponent* Plate)
{
	// Loaded at runtime only (never while a class default is built).
	if (!Plate || Plate->IsTemplate())
	{
		return;
	}
	if (UMaterialInterface* Material = UTheLastShiftSettings::GetScreenMaterial(); Material && Plate->GetMaterial(0) != Material)
	{
		Plate->SetMaterial(0, Material);
	}
}

void ScreenPlate::Place(UStaticMeshComponent* Plate, const FVector& Centre, const FVector& Normal, const FVector& Up, const FVector2D& Size)
{
	if (!Plate)
	{
		return;
	}
	const FVector N = Normal.GetSafeNormal();
	// The plane faces +Z: Z along the face's normal, Y along its up, X across.
	const FRotator Rotation = FRotationMatrix::MakeFromZY(N, Up).Rotator();
	Plate->SetRelativeLocationAndRotation(Centre + N * FaceGapCm, Rotation);
	Plate->SetRelativeScale3D(FVector(FMath::Max(Size.X, 1.0) / 100.0, FMath::Max(Size.Y, 1.0) / 100.0, 1.0));
}

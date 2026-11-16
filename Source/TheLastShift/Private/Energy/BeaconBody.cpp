// SPDX-License-Identifier: MIT

#include "Energy/BeaconBody.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BeaconBody)

namespace BeaconBodyParts
{
	/** Out of reach and never in the way: no collision, no navigation, no overlap, no shadow, no custom depth. */
	static void MakeInert(UStaticMeshComponent* C)
	{
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(false);
		C->SetRenderCustomDepth(false);
		C->bReceivesDecals = false;
	}
}

ABeaconBody::ABeaconBody()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Mount = CreateDefaultSubobject<USceneComponent>(TEXT("Mount"));
	Mount->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Mount);

	Rod = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rod"));
	Rod->SetupAttachment(Mount);
	BeaconBodyParts::MakeInert(Rod);

	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(Mount);
	BeaconBodyParts::MakeInert(Head);

	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
}

void ABeaconBody::Build(UStaticMesh* HeadMesh, UStaticMesh* RodMesh, const FVector& InAimPoint, float InRodLength, bool bInRodReachesGeometry)
{
	AimPoint = InAimPoint;
	RodLength = FMath::Max(0.f, InRodLength);
	bRodReachesGeometry = bInRodReachesGeometry;

	Head->SetStaticMesh(HeadMesh);
	Head->SetRelativeRotation(GetAimDirection().Rotation());
	Head->SetVisibility(HeadMesh != nullptr);

	// The rod mesh is one metre along +Z from its lower end: stretched along its length only.
	Rod->SetStaticMesh(RodMesh);
	Rod->SetRelativeRotation(FRotator::ZeroRotator);
	Rod->SetRelativeScale3D(FVector(1.0, 1.0, RodLength / 100.0));
	Rod->SetVisibility(RodMesh != nullptr && RodLength > 0.f);
}

FVector ABeaconBody::GetAimDirection() const
{
	const FVector Dir = (AimPoint - GetActorLocation()).GetSafeNormal();
	return Dir.IsNearlyZero() ? FVector(0.0, 0.0, -1.0) : Dir;
}

FVector ABeaconBody::GetLensLocation() const
{
	// The centre of the head's front face (its +X extent, halfway up its height), turned with the head.
	const UStaticMesh* Mesh = Head ? Head->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		return GetActorLocation();
	}
	const FBox Box = Mesh->GetBoundingBox();
	return Head->GetComponentTransform().TransformPosition(FVector(FMath::Max(0.0, Box.Max.X), 0.0, Box.GetCenter().Z));
}

FBox ABeaconBody::GetHeadBounds() const
{
	return Head && Head->GetStaticMesh() ? Head->Bounds.GetBox() : FBox(ForceInit);
}

FBox ABeaconBody::GetBodyBounds() const
{
	FBox Box = GetHeadBounds();
	if (Rod && Rod->GetStaticMesh() && RodLength > 0.f)
	{
		Box += Rod->Bounds.GetBox();
	}
	return Box;
}

bool ABeaconBody::HasMesh() const
{
	return (Head && Head->GetStaticMesh()) || (Rod && Rod->GetStaticMesh());
}

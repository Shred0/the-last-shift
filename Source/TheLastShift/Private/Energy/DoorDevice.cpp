// SPDX-License-Identifier: MIT

#include "Energy/DoorDevice.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/InteractableScanComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DoorDevice)

namespace DoorDevice
{
	/** Size of the engine's basic cube mesh used for the panel. */
	static constexpr float CubeMeshSize = 100.f;
}

ADoorDevice::ADoorDevice()
{
	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(GetRootComponent());
	Blocker->SetBoxExtent(FVector(10.f, 200.f, 150.f));
	Blocker->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	Blocker->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Blocker->SetCanEverAffectNavigation(false);

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Blocker);
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Panel->SetStaticMesh(CubeMesh.Object);
	}

	Doorway = CreateDefaultSubobject<UBoxComponent>(TEXT("Doorway"));
	Doorway->SetupAttachment(Blocker);
	Doorway->SetBoxExtent(FVector(80.f, 200.f, 150.f));
	Doorway->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Doorway->SetCollisionResponseToAllChannels(ECR_Ignore);
	Doorway->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Doorway->SetGenerateOverlapEvents(true);
	Doorway->SetCanEverAffectNavigation(false);

	InteractableScan = CreateDefaultSubobject<UInteractableScanComponent>(TEXT("InteractableScan"));
	InteractableScan->AddTarget(Panel);

	FitPanelToBlocker();
}

void ADoorDevice::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	FitPanelToBlocker();
}

void ADoorDevice::FitPanelToBlocker()
{
	if (Blocker && Panel)
	{
		Panel->SetRelativeLocation(FVector::ZeroVector);
		Panel->SetRelativeScale3D(Blocker->GetUnscaledBoxExtent() * 2.f / DoorDevice::CubeMeshSize);
	}
}

void ADoorDevice::BeginPlay()
{
	Doorway->OnComponentEndOverlap.AddUniqueDynamic(this, &ADoorDevice::HandleDoorwayEndOverlap);
	bOpen = true; // so SetOpen(false) below applies the closed state
	SetOpen(false);
	Super::BeginPlay();
}

FVector ADoorDevice::GetDoorSize() const
{
	return Blocker ? Blocker->GetScaledBoxExtent() * 2.f : FVector::ZeroVector;
}

bool ADoorDevice::IsDoorwayOccupied() const
{
	TArray<AActor*> Pawns;
	Doorway->GetOverlappingActors(Pawns, APawn::StaticClass());
	return Pawns.Num() > 0;
}

void ADoorDevice::OnPowerStateChanged(bool bNowPowered)
{
	if (bNowPowered)
	{
		bClosePending = false;
		SetOpen(true);
	}
	else if (bOpen)
	{
		TryClose();
	}
}

void ADoorDevice::TryClose()
{
	if (IsDoorwayOccupied())
	{
		if (!bClosePending)
		{
			UE_LOG(LogEnergy, Log, TEXT("%s: unpowered with a pawn in the doorway, stays open until it is clear."), *GetName());
		}
		bClosePending = true;
		SetActorTickEnabled(true);
		return;
	}
	bClosePending = false;
	SetOpen(false);
}

void ADoorDevice::HandleDoorwayEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (bClosePending && !IsPowered())
	{
		TryClose();
	}
}

void ADoorDevice::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Fallback for the overlap event (a pawn teleported out of the doorway).
	if (bClosePending && !IsPowered())
	{
		TryClose();
		if (!bClosePending && !IsPowered())
		{
			SetActorTickEnabled(false);
		}
	}
}

void ADoorDevice::SetOpen(bool bNewOpen)
{
	if (bOpen == bNewOpen)
	{
		return;
	}
	bOpen = bNewOpen;
	Blocker->SetCollisionEnabled(bNewOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	Panel->SetVisibility(!bNewOpen);
	UE_LOG(LogEnergy, Log, TEXT("%s: %s."), *GetName(), bNewOpen ? TEXT("open") : TEXT("closed"));
	OnOpenChanged.Broadcast(bNewOpen);
}

// SPDX-License-Identifier: MIT

#include "Energy/DoorDevice.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Energy/EnergySubsystem.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Pawn.h"
#include "Run/TheLastShiftSettings.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DoorDevice)

namespace DoorDevice
{
	/** Size of the engine's basic cube mesh used for the panel. */
	static constexpr float CubeMeshSize = 100.f;
	/** Risen, the panel's bottom stops this far above the opening's top, so its face never lies in the lintel's face. */
	static constexpr float ClearGap = 1.f;
}

ADoorDevice::ADoorDevice()
{
	// The blocker is the opening's box: the door's size, read by everything that places or measures a door. It blocks
	// nothing itself: the panel's collision (PanelCollision) blocks, where the panel is.
	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(GetRootComponent());
	Blocker->SetBoxExtent(FVector(10.f, 200.f, 150.f));
	Blocker->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	Blocker->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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

	PanelCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("PanelCollision"));
	PanelCollision->SetupAttachment(Blocker);
	PanelCollision->SetBoxExtent(FVector(10.f, 200.f, 150.f));
	PanelCollision->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	PanelCollision->SetCanEverAffectNavigation(false);
	PanelCollision->SetHiddenInGame(true);

	Doorway = CreateDefaultSubobject<UBoxComponent>(TEXT("Doorway"));
	Doorway->SetupAttachment(Blocker);
	Doorway->SetBoxExtent(FVector(80.f, 200.f, 150.f));
	Doorway->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Doorway->SetCollisionResponseToAllChannels(ECR_Ignore);
	Doorway->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Doorway->SetGenerateOverlapEvents(true);
	Doorway->SetCanEverAffectNavigation(false);

	ApplyPanel();
}

void ADoorDevice::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPanel();
}

float ADoorDevice::GetPanelRise() const
{
	return Blocker ? OpenFraction * (Blocker->GetUnscaledBoxExtent().Z * 2.f + DoorDevice::ClearGap) : 0.f;
}

void ADoorDevice::ApplyPanel()
{
	if (!Blocker || !Panel)
	{
		return;
	}
	const FVector Extent = Blocker->GetUnscaledBoxExtent();
	const float Rise = GetPanelRise();
	// Panel is attached to Blocker, whose centre is the opening's centre: shut at its place, risen straight up by Rise.
	if (ApplyPanelMesh(Panel, PanelMesh))
	{
		// The modelled panel's pivot is its bottom: placed by its bottom centre at scale 1.
		Panel->SetRelativeLocation(FVector(0.f, 0.f, Rise - Extent.Z));
		Panel->SetRelativeScale3D(FVector::OneVector);
	}
	else
	{
		Panel->SetRelativeLocation(FVector(0.f, 0.f, Rise));
		Panel->SetRelativeScale3D(FVector(Extent.X * 2.f * GetPanelDepthShare(), Extent.Y * 2.f, Extent.Z * 2.f) / DoorDevice::CubeMeshSize);
	}
	const bool bShown = IsPanelShown();
	Panel->SetVisibility(bShown);
	if (PanelCollision)
	{
		// The panel's collision is where the panel is, and only while it is shown (a risen, hidden panel blocks nothing).
		PanelCollision->SetBoxExtent(Extent, false);
		PanelCollision->SetRelativeLocation(FVector(0.f, 0.f, Rise));
		PanelCollision->SetCollisionEnabled(bShown ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

bool ADoorDevice::ApplyPanelMesh(UStaticMeshComponent* InPanel, UStaticMesh* Modelled)
{
	if (!InPanel)
	{
		return false;
	}
	if (!Modelled)
	{
		// The engine's cube: a moving object's material (Brief 21), from the one owner of the reference.
		if (UMaterialInterface* Fallback = UTheLastShiftSettings::Get()->SurfaceLook.PanelFallbackMaterial.LoadSynchronous())
		{
			InPanel->SetMaterial(0, Fallback);
		}
		return false;
	}
	if (InPanel->GetStaticMesh() != Modelled)
	{
		InPanel->SetStaticMesh(Modelled);
	}
	// The modelled panel shows its own material: its surface family (Brief 21).
	InPanel->EmptyOverrideMaterials();
	return true;
}

void ADoorDevice::BeginPlay()
{
	Doorway->OnComponentEndOverlap.AddUniqueDynamic(this, &ADoorDevice::HandleDoorwayEndOverlap);
	// Shown in scans through the lidar channel of InteractableScanChannel (one owner: the project settings).
	Panel->SetRenderCustomDepth(true);
	Panel->SetCustomDepthStencilValue(UTheLastShiftSettings::GetInteractableScanChannel());
	// The opening's box blocks nothing: the panel's collision does (a door placed before PanelCollision existed included).
	Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Shut, at rest: the state a door begins in is no motion, nothing announced.
	OpenFraction = 0.f;
	MotionDirection = 0;
	bOpen = false;
	bClosePending = false;
	ApplyPanel();
	Super::BeginPlay();
	LastRunSeconds = GetNow();
}

FVector ADoorDevice::GetDoorSize() const
{
	return Blocker ? Blocker->GetScaledBoxExtent() * 2.f : FVector::ZeroVector;
}

FBox ADoorDevice::GetOpeningBox() const
{
	if (!Blocker)
	{
		return FBox(ForceInit);
	}
	const FVector Extent = Blocker->GetUnscaledBoxExtent();
	return FBox(-Extent, Extent).TransformBy(Blocker->GetComponentTransform());
}

bool ADoorDevice::IsDoorwayOccupied() const
{
	if (!Doorway || Doorway->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		return false;
	}
	TArray<AActor*> Pawns;
	Doorway->GetOverlappingActors(Pawns, APawn::StaticClass());
	return Pawns.Num() > 0;
}

float ADoorDevice::GetMoveSeconds()
{
	return UTheLastShiftSettings::Get()->GetDoorMoveSeconds();
}

void ADoorDevice::OnPowerStateChanged(bool bNowPowered)
{
	if (bNowPowered)
	{
		bClosePending = false;
		StartMotion(1, TEXT("powered"));
	}
	else if (IsOpen())
	{
		TryClose();
	}
}

void ADoorDevice::OnDeviceTimeStateChanged(ETimeState NewState)
{
	// The motion stood still while Stopped: it goes on from now, with no step for the stopped time.
	LastRunSeconds = GetNow();
}

void ADoorDevice::TryClose()
{
	if (IsDoorwayOccupied())
	{
		if (!bClosePending)
		{
			UE_LOG(LogEnergy, Log, TEXT("%s: due to close with a pawn in the doorway: stays open until it is clear."), *GetName());
		}
		bClosePending = true;
		// A door closing when the pawn enters opens again from where it stands.
		if (MotionDirection < 0)
		{
			StartMotion(1, TEXT("a pawn in the doorway"));
		}
		SetActorTickEnabled(true);
		return;
	}
	bClosePending = false;
	StartMotion(-1, IsPowered() ? TEXT("closing") : TEXT("unpowered"));
}

void ADoorDevice::HandleDoorwayEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	// While Stopped a door starts no motion: the next tick in Normal time closes it.
	if (bClosePending && !IsPowered() && !IsTimeStopped())
	{
		TryClose();
	}
}

void ADoorDevice::StartMotion(int32 Direction, const TCHAR* Why)
{
	Direction = FMath::Clamp(Direction, -1, 1);
	if (Direction == MotionDirection)
	{
		return;
	}
	if (Direction == 0 || (Direction > 0 && OpenFraction >= 1.f) || (Direction < 0 && OpenFraction <= 0.f))
	{
		// Already where it would go: no motion.
		MotionDirection = 0;
		return;
	}
	MotionDirection = Direction;
	LastRunSeconds = GetNow();
	MotionStartRun = LastRunSeconds;
	UE_LOG(LogEnergy, Log, TEXT("%s: starts to %s from %.3f open (%s), %.2f s for the whole travel."), *GetName(), Direction > 0 ? TEXT("open") : TEXT("close"),
		OpenFraction, Why, GetMoveSeconds());
	UpdateOpenState();
	SetActorTickEnabled(true);
	OnMotionStarted.Broadcast(this, Direction > 0, true);
}

void ADoorDevice::AdvanceMotion(double Step)
{
	if (MotionDirection == 0)
	{
		return;
	}
	OpenFraction = FMath::Clamp(OpenFraction + MotionDirection * static_cast<float>(Step / GetMoveSeconds()), 0.f, 1.f);
	ApplyPanel();
	if ((MotionDirection > 0 && OpenFraction >= 1.f) || (MotionDirection < 0 && OpenFraction <= 0.f))
	{
		const bool bOpened = MotionDirection > 0;
		MotionDirection = 0;
		UE_LOG(LogEnergy, Log, TEXT("%s: %s (%.3f s of the run's clock after the motion began)."), *GetName(), bOpened ? TEXT("open") : TEXT("shut"),
			GetNow() - MotionStartRun);
		UpdateOpenState();
		OnMotionEnded.Broadcast(this);
	}
}

void ADoorDevice::UpdateOpenState()
{
	const bool bNowOpen = IsOpen();
	if (bNowOpen == bOpen)
	{
		return;
	}
	bOpen = bNowOpen;
	OnOpenChanged.Broadcast(bNowOpen);
}

void ADoorDevice::SetPanelShareAtOnce(float InShare)
{
	OpenFraction = FMath::Clamp(InShare, 0.f, 1.f);
	MotionDirection = 0;
	ApplyPanel();
	UpdateOpenState();
}

void ADoorDevice::FinishMotionAtOnce()
{
	if (MotionDirection == 0)
	{
		return;
	}
	AdvanceMotion(GetMoveSeconds() * 2.0);
}

void ADoorDevice::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetNow();
	const double Step = FMath::Max(Now - LastRunSeconds, 0.0);
	LastRunSeconds = Now;
	// The run's clock stands still behind the pause menu by itself; while Stopped the door stands still too, and starts
	// no motion.
	if (IsTimeStopped())
	{
		return;
	}
	if (!IsPowered() && (bClosePending || MotionDirection < 0))
	{
		if (IsDoorwayOccupied())
		{
			// Due to close, or closing, with the pawn in the doorway: open (again) and wait for it to leave.
			TryClose();
		}
		else if (bClosePending)
		{
			// The doorway is clear (also when the pawn left it by a teleport): it starts to close.
			TryClose();
		}
	}
	AdvanceMotion(Step);
	if (MotionDirection == 0 && !bClosePending)
	{
		// At rest: the tick is the powered timer's again.
		SetActorTickEnabled(IsTimed());
	}
}

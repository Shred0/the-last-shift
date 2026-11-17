// SPDX-License-Identifier: MIT

#include "Prologue/PrologueHatch.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Energy/DoorDevice.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueLineTrigger.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueHatch)

namespace PrologueHatchImpl
{
	/** Size of the engine's basic cube mesh used for the panel. */
	static constexpr float CubeMeshSize = 100.f;
	/** The panel is this share of the opening's depth, so that raised it sits inside the wall above the opening. */
	static constexpr float PanelDepthShare = 0.75f;
	/** The panel reaches this far into the wall on each side and below, so no light shows at its edges. */
	static constexpr float PanelOverlap = 20.f;

	static const TCHAR* LexToString(EPrologueHatchState State)
	{
		switch (State)
		{
		case EPrologueHatchState::Open: return TEXT("Open");
		case EPrologueHatchState::Closing: return TEXT("Closing");
		case EPrologueHatchState::Closed: return TEXT("Closed");
		case EPrologueHatchState::DoorLines: return TEXT("DoorLines");
		case EPrologueHatchState::DoorDelay: return TEXT("DoorDelay");
		case EPrologueHatchState::DoorOpened: return TEXT("DoorOpened");
		}
		return TEXT("Unknown");
	}
}

APrologueHatch::APrologueHatch()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(GetRootComponent());
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->SetCanEverAffectNavigation(false);
	Panel->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Panel->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Greybox(TEXT("/Game/TheLastShift/Art/M_Greybox.M_Greybox"));
	if (Greybox.Succeeded())
	{
		Panel->SetMaterial(0, Greybox.Object);
	}

	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(GetRootComponent());
	Blocker->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Blocker->SetCanEverAffectNavigation(false);
	Blocker->SetHiddenInGame(true);

	ShipVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ShipVolume"));
	ShipVolume->SetupAttachment(GetRootComponent());
	ShipVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShipVolume->SetCanEverAffectNavigation(false);
	ShipVolume->SetHiddenInGame(true);
}

void APrologueHatch::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const FVector Opening = OpeningSize.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f));
	Blocker->SetBoxExtent(Opening * 0.5f);
	Blocker->SetRelativeLocation(FVector(0.f, 0.f, Opening.Z * 0.5f));
	ShipVolume->SetBoxExtent(ShipVolumeExtent.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f)));
	ShipVolume->SetRelativeLocation(ShipVolumeOffset);
	ApplyPanel();
}

void APrologueHatch::BeginPlay()
{
	Super::BeginPlay();
	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	State = EPrologueHatchState::Open;
	ClosingSeconds = 0.0;
	DoorDelaySeconds = 0.0;
	ApplyPanel();
	UE_LOG(LogTheLastShift, Log, TEXT("%s: open, closes in %.2f s of Normal time once the player has left the ship; exit door %s, %.2f s after the Door lines."), *GetName(),
		UTheLastShiftSettings::Get()->GetHatchCloseSeconds(), *GetNameSafe(ExitDoor), UTheLastShiftSettings::Get()->GetExitDoorDelaySeconds());
}

FBox APrologueHatch::GetShipBox() const
{
	const FVector Extent = ShipVolumeExtent.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f));
	return FBox(-Extent, Extent).TransformBy(FTransform(ShipVolumeOffset) * GetActorTransform());
}

bool APrologueHatch::IsInsideShip(const AActor* Actor) const
{
	return Actor && GetShipBox().IsInside(Actor->GetActorLocation());
}

float APrologueHatch::GetClosedFraction() const
{
	switch (State)
	{
	case EPrologueHatchState::Open: return 0.f;
	case EPrologueHatchState::Closing: return FMath::Clamp(static_cast<float>(ClosingSeconds / UTheLastShiftSettings::Get()->GetHatchCloseSeconds()), 0.f, 1.f);
	default: return 1.f;
	}
}

void APrologueHatch::ApplyPanel()
{
	if (!Panel)
	{
		return;
	}
	using namespace PrologueHatchImpl;
	const FVector Opening = OpeningSize.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f));
	const FVector Size(Opening.X * PanelDepthShare, Opening.Y + 2.f * PanelOverlap, Opening.Z + PanelOverlap);
	Panel->SetRelativeScale3D(Size / CubeMeshSize);
	// Open: raised by the opening's height into the wall above it. Closed: its bottom just below the floor line.
	const float Raised = (1.f - GetClosedFraction()) * Opening.Z;
	Panel->SetRelativeLocation(FVector(0.f, 0.f, Size.Z * 0.5f - PanelOverlap + Raised));
}

bool APrologueHatch::IsChannelTypedOut() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UTextChannelComponent* Channel = Pawn ? Pawn->FindComponentByClass<UTextChannelComponent>() : nullptr;
	return !Channel || Channel->IsTypedOut();
}

void APrologueHatch::SetState(EPrologueHatchState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %s (closing %.2f s, door delay %.2f s of Normal time)."), *GetName(), PrologueHatchImpl::LexToString(NewState), ClosingSeconds,
		DoorDelaySeconds);
	ApplyPanel();
	ReceiveHatchStateChanged(NewState);
}

void APrologueHatch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = TimeState ? TimeState->GetRunSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	const double Step = LastRunSeconds >= 0.0 ? FMath::Max(Now - LastRunSeconds, 0.0) : 0.0;
	LastRunSeconds = Now;
	const bool bStill = TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen());

	switch (State)
	{
	case EPrologueHatchState::Open:
	{
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			break;
		}
		const bool bInside = IsInsideShip(Pawn);
		bPawnWasInside |= bInside;
		if (bPawnWasInside && !bInside)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the player has left the ship."), *GetName());
			ClosingSeconds = 0.0;
			SetState(EPrologueHatchState::Closing);
		}
		break;
	}
	case EPrologueHatchState::Closing:
		if (!bStill)
		{
			ClosingSeconds += Step;
			ApplyPanel();
		}
		if (ClosingSeconds >= UTheLastShiftSettings::Get()->GetHatchCloseSeconds())
		{
			SetState(EPrologueHatchState::Closed);
			APrologueLineTrigger::Say(this, PrologueGroups::Hatch);
		}
		break;
	case EPrologueHatchState::Closed:
		if (IsChannelTypedOut())
		{
			SetState(EPrologueHatchState::DoorLines);
			APrologueLineTrigger::Say(this, PrologueGroups::Door);
		}
		break;
	case EPrologueHatchState::DoorLines:
		if (IsChannelTypedOut())
		{
			DoorDelaySeconds = 0.0;
			SetState(EPrologueHatchState::DoorDelay);
		}
		break;
	case EPrologueHatchState::DoorDelay:
		if (!bStill)
		{
			DoorDelaySeconds += Step;
		}
		if (DoorDelaySeconds >= UTheLastShiftSettings::Get()->GetExitDoorDelaySeconds())
		{
			if (ExitDoor)
			{
				ExitDoor->PowerOn();
			}
			SetState(EPrologueHatchState::DoorOpened);
			SetActorTickEnabled(false);
		}
		break;
	case EPrologueHatchState::DoorOpened:
		break;
	}
}

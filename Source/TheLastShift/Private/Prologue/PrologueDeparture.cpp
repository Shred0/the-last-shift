// SPDX-License-Identifier: MIT

#include "Prologue/PrologueDeparture.h"

#include "Area/AreaExit.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Prologue/PrologueEntryDoor.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueDeparture)

namespace PrologueDepartureImpl
{
	static const TCHAR* LexToString(EPrologueDepartureState State)
	{
		switch (State)
		{
		case EPrologueDepartureState::Waiting: return TEXT("Waiting");
		case EPrologueDepartureState::DoorClosing: return TEXT("DoorClosing");
		case EPrologueDepartureState::HatchOpening: return TEXT("HatchOpening");
		case EPrologueDepartureState::ShipHatchOpening: return TEXT("ShipHatchOpening");
		case EPrologueDepartureState::Boardable: return TEXT("Boardable");
		}
		return TEXT("Unknown");
	}
}

APrologueDeparture::APrologueDeparture()
{
	PrimaryActorTick.bCanEverTick = true;
	Hangar = CreateDefaultSubobject<UBoxComponent>(TEXT("Hangar"));
	SetRootComponent(Hangar);
	Hangar->SetBoxExtent(HangarExtent);
	Hangar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hangar->SetCanEverAffectNavigation(false);
	Hangar->SetHiddenInGame(true);
}

void APrologueDeparture::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Hangar->SetBoxExtent(HangarExtent.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f)));
}

void APrologueDeparture::BeginPlay()
{
	Super::BeginPlay();
	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	State = EPrologueDepartureState::Waiting;
	PhaseSeconds = 0.0;
	SetExitEnabled(false);
	// Brief 31A: on the way home the departure does not run (the entry door stays shut, the ship's exit off).
	if (URunSubsystem::IsReturnStateOf(GetWorld()))
	{
		SetActorTickEnabled(false);
		UE_LOG(LogTheLastShift, Log, TEXT("%s: the way home: the departure does not run."), *GetName());
		return;
	}
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	UE_LOG(LogTheLastShift, Log, TEXT("%s: waiting for the player in the hangar; door %s closes in %.2f s, hatch %s opens in %.2f s, ship's hatch %s in %.2f s (Normal time); exit %s off."),
		*GetName(), *GetNameSafe(EntryDoor), Settings->GetHomeDoorCloseSeconds(), *GetNameSafe(HangarHatch), Settings->GetHomeHatchOpenSeconds(),
		*GetNameSafe(ShipHatch), Settings->GetShipHatchOpenSeconds(), *GetNameSafe(ShipExit));
}

FBox APrologueDeparture::GetHangarBox() const
{
	const FVector Extent = HangarExtent.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f));
	return FBox(-Extent, Extent).TransformBy(GetActorTransform());
}

bool APrologueDeparture::IsWhollyInside(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return false;
	}
	FVector Half(0.f);
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		float Radius = 0.f;
		float HalfHeight = 0.f;
		Character->GetCapsuleComponent()->GetScaledCapsuleSize(Radius, HalfHeight);
		Half = FVector(Radius, Radius, HalfHeight);
	}
	const FVector At = Pawn->GetActorLocation();
	const FBox Box = GetHangarBox();
	return Box.IsInside(At - Half) && Box.IsInside(At + Half);
}

int32 APrologueDeparture::PhaseIndex(EPrologueDepartureState Phase)
{
	switch (Phase)
	{
	case EPrologueDepartureState::DoorClosing: return 0;
	case EPrologueDepartureState::HatchOpening: return 1;
	case EPrologueDepartureState::ShipHatchOpening: return 2;
	default: return INDEX_NONE;
	}
}

float APrologueDeparture::GetPhaseTook(EPrologueDepartureState Phase) const
{
	const int32 Index = PhaseIndex(Phase);
	return Index != INDEX_NONE ? static_cast<float>(PhaseTook[Index]) : -1.f;
}

float APrologueDeparture::GetPhaseStartRun(EPrologueDepartureState Phase) const
{
	const int32 Index = PhaseIndex(Phase);
	return Index != INDEX_NONE ? static_cast<float>(PhaseStartRun[Index]) : -1.f;
}

void APrologueDeparture::SetExitEnabled(bool bEnabled)
{
	UBoxComponent* Volume = ShipExit ? ShipExit->GetVolume() : nullptr;
	if (!Volume)
	{
		return;
	}
	Volume->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	if (bEnabled)
	{
		Volume->UpdateOverlaps();
		ShipExit->TryTravel();
	}
}

void APrologueDeparture::SetState(EPrologueDepartureState NewState, double Now)
{
	const int32 Ended = PhaseIndex(State);
	if (Ended != INDEX_NONE)
	{
		PhaseTook[Ended] = PhaseSeconds;
	}
	State = NewState;
	PhaseSeconds = 0.0;
	const int32 Began = PhaseIndex(NewState);
	if (Began != INDEX_NONE)
	{
		PhaseStartRun[Began] = Now;
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %s (run clock %.3f s%s)."), *GetName(), PrologueDepartureImpl::LexToString(NewState), Now,
		Ended != INDEX_NONE ? *FString::Printf(TEXT(", the last phase took %.3f s of Normal time"), PhaseTook[Ended]) : TEXT(""));
	if (NewState == EPrologueDepartureState::Boardable)
	{
		SetExitEnabled(true);
		SetActorTickEnabled(false);
	}
	OnStateChanged.Broadcast(NewState);
}

void APrologueDeparture::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = TimeState ? TimeState->GetRunSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	const double Step = LastRunSeconds >= 0.0 ? FMath::Max(Now - LastRunSeconds, 0.0) : 0.0;
	LastRunSeconds = Now;
	const bool bStill = TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen());
	if (bStill)
	{
		return;
	}
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();

	switch (State)
	{
	case EPrologueDepartureState::Waiting:
	{
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!IsWhollyInside(Pawn))
		{
			break;
		}
		if (EntryDoor && EntryDoor->IsOpen())
		{
			if (EntryDoor->BeginCloseBehind())
			{
				UE_LOG(LogTheLastShift, Log, TEXT("%s: the player is wholly inside the hangar at %s."), *GetName(), *Pawn->GetActorLocation().ToCompactString());
				SetState(EPrologueDepartureState::DoorClosing, Now);
			}
		}
		else
		{
			// No door, or one already closed: the hatch is next.
			SetState(EPrologueDepartureState::HatchOpening, Now);
		}
		break;
	}
	case EPrologueDepartureState::DoorClosing:
	{
		const float Seconds = Settings->GetHomeDoorCloseSeconds();
		PhaseSeconds = FMath::Min(PhaseSeconds + Step, static_cast<double>(Seconds));
		if (EntryDoor && EntryDoor->IsClosingBehind())
		{
			EntryDoor->SetClosedFraction(static_cast<float>(PhaseSeconds / Seconds));
			if (PhaseSeconds >= Seconds)
			{
				EntryDoor->FinishCloseBehind();
			}
		}
		// "When the door is closed": the power off closes it once its doorway is clear.
		if (PhaseSeconds >= Seconds && (!EntryDoor || !EntryDoor->IsOpen()))
		{
			SetState(EPrologueDepartureState::HatchOpening, Now);
		}
		break;
	}
	case EPrologueDepartureState::HatchOpening:
	{
		const float Seconds = Settings->GetHomeHatchOpenSeconds();
		PhaseSeconds = FMath::Min(PhaseSeconds + Step, static_cast<double>(Seconds));
		if (HangarHatch)
		{
			HangarHatch->SetOpenFraction(static_cast<float>(PhaseSeconds / Seconds));
		}
		if (PhaseSeconds >= Seconds)
		{
			SetState(EPrologueDepartureState::ShipHatchOpening, Now);
		}
		break;
	}
	case EPrologueDepartureState::ShipHatchOpening:
	{
		const float Seconds = Settings->GetShipHatchOpenSeconds();
		PhaseSeconds = FMath::Min(PhaseSeconds + Step, static_cast<double>(Seconds));
		if (ShipHatch)
		{
			ShipHatch->SetOpenFraction(static_cast<float>(PhaseSeconds / Seconds));
		}
		if (PhaseSeconds >= Seconds)
		{
			SetState(EPrologueDepartureState::Boardable, Now);
		}
		break;
	}
	case EPrologueDepartureState::Boardable:
		break;
	}
}

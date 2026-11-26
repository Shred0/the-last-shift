// SPDX-License-Identifier: MIT

#include "Prologue/PrologueReturn.h"

#include "Area/AreaExit.h"
#include "Components/CapsuleComponent.h"
#include "Energy/DoorDevice.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueLineTrigger.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueReturn)

namespace PrologueReturnImpl
{
	static const TCHAR* LexToString(EPrologueReturnState State)
	{
		switch (State)
		{
		case EPrologueReturnState::Departure: return TEXT("Departure");
		case EPrologueReturnState::InShip: return TEXT("InShip");
		case EPrologueReturnState::Landing: return TEXT("Landing");
		case EPrologueReturnState::DoorDelay: return TEXT("DoorDelay");
		case EPrologueReturnState::DoorOpened: return TEXT("DoorOpened");
		}
		return TEXT("Unknown");
	}

	static UTextChannelComponent* ChannelOf(const UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UTextChannelComponent>() : nullptr;
	}
}

APrologueReturn::APrologueReturn()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void APrologueReturn::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	bStarted = bPawnPlaced = bPawnWasInside = bHatchClosing = bLastLandingSeen = false;
	HatchClosingSeconds = DoorDelaySeconds = 0.0;
	SteppedOutRun = LandingDoneRun = DoorOpenedRun = -1.0;
	LastLandingRow = NAME_None;

	if (!URunSubsystem::IsReturnStateOf(World))
	{
		// The departure: the way home's door stays shut (nothing powers it) and its corridor's exit is off.
		State = EPrologueReturnState::Departure;
		if (CorridorExit)
		{
			CorridorExit->SetExitEnabled(false);
		}
		SetActorTickEnabled(false);
		UE_LOG(LogTheLastShift, Log, TEXT("%s: the departure: the way home's door %s shut, its corridor's exit %s off."), *GetName(), *GetNameSafe(HomeDoor), *GetNameSafe(CorridorExit));
		return;
	}

	State = EPrologueReturnState::InShip;
	ApplyReturnPose();
	if (CorridorExit)
	{
		CorridorExit->SetExitEnabled(true);
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the way home: the ship at %s yaw %.0f, the hangar's hatch open; the door %s opens %.2f s after the Landing lines."), *GetName(),
		*ReturnShipLocation.ToCompactString(), ReturnShipYaw, *GetNameSafe(HomeDoor), UTheLastShiftSettings::Get()->GetExitDoorDelaySeconds());
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->NotifyReturnEntered(World);
	}
}

void APrologueReturn::ApplyReturnPose()
{
	if (!Ship)
	{
		UE_LOG(LogTheLastShift, Warning, TEXT("%s: no ship to turn."), *GetName());
		return;
	}
	const FTransform From = Ship->GetActorTransform();
	const FTransform To(FRotator(0.f, ReturnShipYaw, 0.f), ReturnShipLocation, From.GetScale3D());
	// Every piece of the ship keeps its place on the ship: its transform relative to the ship, applied to the new pose.
	for (AActor* Piece : TArray<AActor*>{ ShipHatch.Get(), ShipExit.Get() })
	{
		if (Piece)
		{
			const FTransform Relative = Piece->GetActorTransform().GetRelativeTransform(From);
			Piece->SetActorTransform(Relative * To, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	Ship->SetActorTransform(To, false, nullptr, ETeleportType::TeleportPhysics);
	if (ShipExit)
	{
		ShipExit->SetExitEnabled(false);
	}
}

FBox APrologueReturn::GetShipInsideBox() const
{
	const FVector Extent = ShipInsideExtent.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f));
	const FTransform ShipT = Ship ? Ship->GetActorTransform() : GetActorTransform();
	return FBox(-Extent, Extent).TransformBy(FTransform(ShipInsideOffset) * FTransform(ShipT.GetRotation(), ShipT.GetLocation()));
}

bool APrologueReturn::IsInsideShip(const AActor* Actor) const
{
	return Actor && GetShipInsideBox().IsInside(Actor->GetActorLocation());
}

float APrologueReturn::GetHatchClosedFraction() const
{
	if (!bHatchClosing)
	{
		return 0.f;
	}
	return FMath::Clamp(static_cast<float>(HatchClosingSeconds / UTheLastShiftSettings::Get()->GetHatchCloseSeconds()), 0.f, 1.f);
}

bool APrologueReturn::PlacePawn(APawn* Pawn)
{
	if (!Pawn || !Ship)
	{
		return false;
	}
	const FTransform ShipT = Ship->GetActorTransform();
	const FVector Above = ShipT.TransformPosition(PawnStartInShip + FVector(0.f, 0.f, 300.f));
	const FVector Below = ShipT.TransformPosition(PawnStartInShip - FVector(0.f, 0.f, 100.f));
	// Stand on the ship's floor: the first surface below, the capsule's half height above it.
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("ReturnPawnStart"), false, Pawn);
	float HalfHeight = 96.f;
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	const FVector Floor = GetWorld()->LineTraceSingleByChannel(Hit, Above, Below, ECC_Visibility, Params) ? Hit.ImpactPoint : ShipT.TransformPosition(PawnStartInShip);
	const FVector At = Floor + FVector(0.f, 0.f, HalfHeight + 2.f);
	const FRotator Facing(0.f, ShipT.Rotator().Yaw + PawnStartYawInShip, 0.f);
	Pawn->TeleportTo(At, Facing, false, true);
	if (AController* Controller = Pawn->GetController())
	{
		Controller->SetControlRotation(Facing);
	}
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the player starts inside the ship at %s, facing yaw %.0f (inside the ship: %d)."), *GetName(), *Pawn->GetActorLocation().ToCompactString(),
		Facing.Yaw, IsInsideShip(Pawn) ? 1 : 0);
	return true;
}

void APrologueReturn::SetState(EPrologueReturnState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %s (run clock %.3f s, door delay %.2f s of Normal time)."), *GetName(), PrologueReturnImpl::LexToString(NewState),
		TimeState ? TimeState->GetRunSeconds() : -1.0, DoorDelaySeconds);
	OnStateChanged.Broadcast(NewState);
}

void APrologueReturn::StepOut(double Now)
{
	SteppedOutRun = Now;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the player has left the ship: the hangar's hatch closes, the Landing lines."), *GetName());
	bHatchClosing = HangarHatch != nullptr;
	HatchClosingSeconds = 0.0;
	SetState(EPrologueReturnState::Landing);

	const TArray<FName> Rows = UHomeAIComponent::FindPrologueRows(UTheLastShiftSettings::Get()->LoadEpilogueTable(), EpilogueGroups::Landing);
	LastLandingRow = Rows.Num() > 0 ? Rows.Last() : NAME_None;
	APrologueLineTrigger::Say(this, EpilogueGroups::Landing);
	const UTextChannelComponent* Channel = PrologueReturnImpl::ChannelOf(GetWorld());
	// The home AI pushes the rows within Say: nothing in the channel now means nothing was said.
	if (LastLandingRow.IsNone() || !Channel || !Channel->IsBusy())
	{
		FinishLanding(Now, LastLandingRow.IsNone() ? TEXT("the Landing group has no row") : TEXT("no Landing line was said"));
	}
}

void APrologueReturn::UpdateLanding(double Now)
{
	const UTextChannelComponent* Channel = PrologueReturnImpl::ChannelOf(GetWorld());
	if (!Channel)
	{
		FinishLanding(Now, TEXT("no text channel"));
		return;
	}
	bLastLandingSeen |= Channel->GetCurrentRowName() == LastLandingRow;
	// The last Landing row shown, or gone from the queue without being shown (dropped at Silent, refused by a full queue),
	// and everything before it typed out (the engineer's lines before its rows are rows of the group).
	if ((bLastLandingSeen || !Channel->IsQueued(LastLandingRow)) && Channel->IsTypedOut())
	{
		FinishLanding(Now, *FString::Printf(TEXT("%s typed out"), *LastLandingRow.ToString()));
	}
}

void APrologueReturn::FinishLanding(double Now, const TCHAR* Why)
{
	LandingDoneRun = Now;
	DoorDelaySeconds = 0.0;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the Landing lines are done (%s, %.2f s of the run's clock after stepping out)."), *GetName(), Why, Now - SteppedOutRun);
	SetState(EPrologueReturnState::DoorDelay);
}

void APrologueReturn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	const double Now = TimeState ? TimeState->GetRunSeconds() : (World ? World->GetTimeSeconds() : 0.0);
	const double Step = LastRunSeconds >= 0.0 ? FMath::Max(Now - LastRunSeconds, 0.0) : 0.0;
	LastRunSeconds = Now;
	const bool bStill = TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen());
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (!bStarted)
	{
		// After every BeginPlay of the map (the panels start closed in theirs): the hatches open, the pawn in the ship.
		bStarted = true;
		if (ShipHatch)
		{
			ShipHatch->SetOpenAtOnce(1.f);
		}
		if (HangarHatch)
		{
			HangarHatch->SetOpenAtOnce(1.f);
		}
	}
	if (!bPawnPlaced)
	{
		bPawnPlaced = PlacePawn(Pawn);
		return;
	}

	if (bHatchClosing && HangarHatch)
	{
		if (!bStill)
		{
			HatchClosingSeconds += Step;
		}
		const float Seconds = UTheLastShiftSettings::Get()->GetHatchCloseSeconds();
		HangarHatch->SetOpenFraction(1.f - FMath::Clamp(static_cast<float>(HatchClosingSeconds / Seconds), 0.f, 1.f));
		if (HatchClosingSeconds >= Seconds)
		{
			bHatchClosing = false;
			HatchClosingSeconds = Seconds;
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the hangar's hatch has closed (%.2f s of Normal time)."), *GetName(), Seconds);
		}
	}

	switch (State)
	{
	case EPrologueReturnState::InShip:
	{
		if (!Pawn)
		{
			break;
		}
		const bool bInside = IsInsideShip(Pawn);
		bPawnWasInside |= bInside;
		if (bPawnWasInside && !bInside)
		{
			StepOut(Now);
		}
		break;
	}
	case EPrologueReturnState::Landing:
		UpdateLanding(Now);
		break;
	case EPrologueReturnState::DoorDelay:
		if (!bStill)
		{
			DoorDelaySeconds += Step;
		}
		if (DoorDelaySeconds >= UTheLastShiftSettings::Get()->GetExitDoorDelaySeconds())
		{
			if (HomeDoor)
			{
				HomeDoor->PowerOn();
			}
			DoorOpenedRun = Now;
			SetState(EPrologueReturnState::DoorOpened);
		}
		break;
	case EPrologueReturnState::DoorOpened:
		if (!bHatchClosing)
		{
			SetActorTickEnabled(false);
		}
		break;
	case EPrologueReturnState::Departure:
		break;
	}
}

// SPDX-License-Identifier: MIT

#include "Area/AreaExit.h"

#include "Area/AreaGenerator.h"
#include "Area/AreaTypes.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Run/RunSubsystem.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AreaExit)

AAreaExit::AAreaExit()
{
	PrimaryActorTick.bCanEverTick = false;

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	SetRootComponent(Volume);
	Volume->SetBoxExtent(FVector(150.f, 200.f, 150.f));
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
	Volume->SetCanEverAffectNavigation(false);
	Volume->SetHiddenInGame(true);
}

void AAreaExit::BeginPlay()
{
	Super::BeginPlay();
	Volume->OnComponentBeginOverlap.AddUniqueDynamic(this, &AAreaExit::HandleBeginOverlap);
	if (UTimeStateSubsystem* Time = GetWorld()->GetSubsystem<UTimeStateSubsystem>())
	{
		Time->OnTimeStateChanged.AddUniqueDynamic(this, &AAreaExit::HandleTimeStateChanged);
	}
}

void AAreaExit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTimeStateSubsystem* Time = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr)
	{
		Time->OnTimeStateChanged.RemoveDynamic(this, &AAreaExit::HandleTimeStateChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void AAreaExit::TryTravel()
{
	UWorld* World = GetWorld();
	if (bTriggered || !bExitEnabled || !World || !World->IsGameWorld())
	{
		return;
	}
	const APlayerController* Controller = World->GetFirstPlayerController();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	// Inside the box, or past it in the exit corridor (a player who walked past it while Stopped).
	if (!Pawn || (!Volume->IsOverlappingActor(Pawn) && !IsAtOrPastExit(Pawn)))
	{
		return;
	}
	const UTimeStateSubsystem* Time = World->GetSubsystem<UTimeStateSubsystem>();
	if (Time && Time->IsStopped())
	{
		UE_LOG(LogArea, Log, TEXT("%s: player inside while Stopped: no travel until Resume."), *GetName());
		return;
	}
	if (!AAreaGenerator::IsAreaReady(World))
	{
		return;
	}
	URunSubsystem* Run = URunSubsystem::Get(this);
	UE_LOG(LogArea, Log, TEXT("%s: player pawn %s inside or past it in Normal time: travel to the next area."), *GetName(), *Pawn->GetName());
#if !UE_BUILD_SHIPPING
	if (bTestHoldTravel)
	{
		++TestTravelRequests;
		UE_LOG(LogArea, Log, TEXT("%s: the travel is held by a test (%d requests)."), *GetName(), TestTravelRequests);
		return;
	}
#endif
	OnPlayerEntered.Broadcast(Pawn);
	bTriggered = Run && Run->TravelToNextArea();
}

void AAreaExit::SetExitEnabled(bool bEnabled)
{
	if (bExitEnabled != bEnabled)
	{
		UE_LOG(LogArea, Log, TEXT("%s: switched %s."), *GetName(), bEnabled ? TEXT("on") : TEXT("off"));
	}
	bExitEnabled = bEnabled;
}

bool AAreaExit::IsAtOrPastExit(const APawn* Pawn) const
{
	if (!Pawn || !Volume)
	{
		return false;
	}
	// In the exit's own space: from its near face to RunPastExit beyond its far face, inside its section.
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(Pawn->GetActorLocation());
	const FVector Extent = Volume->GetUnscaledBoxExtent();
	const float Past = FMath::Clamp(RunPastExit, 0.f, 100000.f);
	return Local.X >= -Extent.X && Local.X <= Extent.X + Past && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void AAreaExit::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	if (NewState == ETimeState::Normal)
	{
		TryTravel();
	}
}

void AAreaExit::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (Pawn && Pawn->IsPlayerControlled())
	{
		TryTravel();
	}
}

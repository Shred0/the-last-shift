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
	if (bTriggered || !World || !World->IsGameWorld())
	{
		return;
	}
	const APlayerController* Controller = World->GetFirstPlayerController();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn || !Volume->IsOverlappingActor(Pawn))
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
	UE_LOG(LogArea, Log, TEXT("%s: player pawn %s inside in Normal time: travel to the next area."), *GetName(), *Pawn->GetName());
	OnPlayerEntered.Broadcast(Pawn);
	bTriggered = Run && Run->TravelToNextArea();
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

// SPDX-License-Identifier: MIT

#include "Prologue/PrologueLineTrigger.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Run/RunSubsystem.h"
#include "TheLastShift.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueLineTrigger)

FOnAnyPrologueLine APrologueLineTrigger::OnAnyLine;

APrologueLineTrigger::APrologueLineTrigger()
{
	PrimaryActorTick.bCanEverTick = false;
	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	SetRootComponent(Volume);
	Volume->SetBoxExtent(Extent);
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
	Volume->SetCanEverAffectNavigation(false);
	Volume->SetHiddenInGame(true);
}

void APrologueLineTrigger::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Volume->SetBoxExtent(Extent.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f)));
}

void APrologueLineTrigger::BeginPlay()
{
	Super::BeginPlay();
	Volume->OnComponentBeginOverlap.AddUniqueDynamic(this, &APrologueLineTrigger::HandleBeginOverlap);
}

void APrologueLineTrigger::Say(const UObject* WorldContext, FName Group)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || Group.IsNone())
	{
		return;
	}
	UE_LOG(LogTheLastShift, Log, TEXT("Prologue: group %s asked for in %s."), *Group.ToString(), *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
	OnAnyLine.Broadcast(World, Group);
}

void APrologueLineTrigger::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (bFired || !Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}
	// Brief 31A: on the way home the departure's rooms say nothing.
	if (URunSubsystem::IsReturnStateOf(GetWorld()))
	{
		return;
	}
	bFired = true;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: player pawn %s entered: group %s."), *GetName(), *Pawn->GetName(), *Group.ToString());
	Say(this, Group);
	OnFired.Broadcast(Group);
}

// SPDX-License-Identifier: MIT

#include "Area/AreaExit.h"

#include "Area/AreaTypes.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"

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
}

void AAreaExit::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (Pawn && Pawn->IsPlayerControlled())
	{
		UE_LOG(LogArea, Log, TEXT("%s: player pawn %s entered the exit."), *GetName(), *Pawn->GetName());
		OnPlayerEntered.Broadcast(Pawn);
	}
}

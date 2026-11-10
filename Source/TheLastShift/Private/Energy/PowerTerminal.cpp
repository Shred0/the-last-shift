// SPDX-License-Identifier: MIT

#include "Energy/PowerTerminal.h"

#include "Components/StaticMeshComponent.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PoweredDevice.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PowerTerminal)

namespace TerminalTuning
{
	// Mirrors of the UPROPERTY clamps: Blueprint and C++ writes bypass ClampMin/ClampMax.
	static int32 Cost(int32 V) { return FMath::Clamp(V, 0, 99); }
	static float Radius(float V) { return FMath::Clamp(V, 50.f, 1000.f); }
}

APowerTerminal::APowerTerminal()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	Mesh->SetRelativeScale3D(FVector(0.4f, 0.6f, 1.2f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
}

bool APowerTerminal::IsInRange(const APawn* InstigatorPawn) const
{
	if (!InstigatorPawn)
	{
		return false;
	}
	const float Radius = TerminalTuning::Radius(InteractRadius);
	return FVector::DistSquared2D(InstigatorPawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(Radius);
}

APowerTerminal* APowerTerminal::FindNearestInRange(const APawn* Pawn)
{
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	APowerTerminal* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (TActorIterator<APowerTerminal> It(World); It; ++It)
	{
		if (!It->IsInRange(Pawn))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared2D(Pawn->GetActorLocation(), It->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = *It;
		}
	}
	return Best;
}

bool APowerTerminal::TryActivate(APawn* InstigatorPawn)
{
	if (!IsInRange(InstigatorPawn))
	{
		return false;
	}

	UEnergySubsystem* Energy = UEnergySubsystem::Get(this);
	const UTimeStateSubsystem* TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	const int32 Price = TerminalTuning::Cost(Cost);
	const int32 Banked = Energy ? Energy->GetBankedEnergy() : 0;
	const bool bStopped = TimeState && TimeState->IsStopped();

	if (bStopped || !Energy || !Energy->Spend(Price))
	{
		UE_LOG(LogEnergy, Log, TEXT("%s: refused (%s, cost %d, banked %d)."), *GetName(),
			bStopped ? TEXT("time is stopped") : TEXT("not enough energy"), Price, Banked);
		OnInsufficientEnergy.Broadcast(Price, Banked, bStopped);
		return false;
	}

	int32 Powered = 0;
	for (APoweredDevice* Device : Targets)
	{
		if (Device)
		{
			Device->PowerOn();
			++Powered;
		}
	}
	UE_LOG(LogEnergy, Log, TEXT("%s: activated, spent %d, powered %d device(s)."), *GetName(), Price, Powered);
	OnActivated.Broadcast(Price);
	return true;
}

// SPDX-License-Identifier: MIT

#include "Area/AreaTypes.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AreaTypes)

DEFINE_LOG_CATEGORY(LogArea);

const TCHAR* LexToString(EAreaSegmentType Type)
{
	switch (Type)
	{
	case EAreaSegmentType::Corridor: return TEXT("Corridor");
	case EAreaSegmentType::Room: return TEXT("Room");
	case EAreaSegmentType::Catwalk: return TEXT("Catwalk");
	case EAreaSegmentType::Hall: return TEXT("Hall");
	case EAreaSegmentType::Gate: return TEXT("Gate");
	}
	return TEXT("Unknown");
}

bool IsClosedAreaSegment(EAreaSegmentType Type)
{
	return Type == EAreaSegmentType::Corridor || Type == EAreaSegmentType::Room || Type == EAreaSegmentType::Gate;
}

FAreaPawnMetrics GetAreaPawnMetrics(const UWorld* World)
{
	FAreaPawnMetrics Metrics;
	Metrics.Source = TEXT("<defaults>");

	const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	const UClass* GameModeClass = Settings ? Settings->DefaultGameMode.Get() : nullptr;
	const AGameModeBase* GameMode = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
	const UClass* PawnClass = GameMode ? GameMode->DefaultPawnClass.Get() : nullptr;
	const ACharacter* Character = PawnClass ? Cast<ACharacter>(PawnClass->GetDefaultObject()) : nullptr;
	if (!Character)
	{
		Character = GetDefault<ACharacter>();
	}
	else
	{
		Metrics.Source = PawnClass->GetName();
	}

	if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		Metrics.CapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
		Metrics.CapsuleRadius = Capsule->GetUnscaledCapsuleRadius();
	}
	if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Metrics.WalkSpeed = Movement->MaxWalkSpeed;
		Metrics.MaxStepHeight = Movement->MaxStepHeight;
	}
	Metrics.EyeHeight = Character->BaseEyeHeight;
	return Metrics;
}

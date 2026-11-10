// SPDX-License-Identifier: MIT

#include "Area/AreaGeometry.h"

#include "Components/InstancedStaticMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AreaGeometry)

namespace AreaGeometry
{
	static UInstancedStaticMeshComponent* Make(AAreaGeometry* Owner, const TCHAR* Name, bool bCollides, bool bAffectsNavigation)
	{
		UInstancedStaticMeshComponent* Component = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Component->SetupAttachment(Owner->GetRootComponent());
		Component->SetMobility(EComponentMobility::Static);
		if (bCollides)
		{
			Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		}
		else
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		Component->SetCanEverAffectNavigation(bAffectsNavigation);
		return Component;
	}
}

AAreaGeometry::AAreaGeometry()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);

	Floors = AreaGeometry::Make(this, TEXT("Floors"), true, true);
	Walls = AreaGeometry::Make(this, TEXT("Walls"), true, true);
	Curbs = AreaGeometry::Make(this, TEXT("Curbs"), true, true);
	Pillars = AreaGeometry::Make(this, TEXT("Pillars"), true, true);
	Ceilings = AreaGeometry::Make(this, TEXT("Ceilings"), true, false);
	Backdrop = AreaGeometry::Make(this, TEXT("Backdrop"), false, false);
}

TArray<UInstancedStaticMeshComponent*> AAreaGeometry::GetAllComponents() const
{
	return { Floors.Get(), Walls.Get(), Curbs.Get(), Pillars.Get(), Ceilings.Get(), Backdrop.Get() };
}

int32 AAreaGeometry::GetTotalInstanceCount() const
{
	int32 Total = 0;
	for (const UInstancedStaticMeshComponent* Component : GetAllComponents())
	{
		Total += Component ? Component->GetInstanceCount() : 0;
	}
	return Total;
}

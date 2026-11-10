// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AreaGeometry.generated.h"

class UInstancedStaticMeshComponent;

/**
 * The baked geometry of a generated area: one instanced static mesh per kind of block, all the same engine cube and
 * greybox material (assigned by the generator). Floors, walls, curbs and pillars collide and shape the navmesh;
 * ceilings collide but do not affect navigation; the backdrop (non-walkable masses around open segments) neither
 * collides nor affects navigation. No logic at runtime.
 */
UCLASS(NotBlueprintable)
class THELASTSHIFT_API AAreaGeometry : public AActor
{
	GENERATED_BODY()

public:
	AAreaGeometry();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UInstancedStaticMeshComponent> Floors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UInstancedStaticMeshComponent> Walls;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UInstancedStaticMeshComponent> Curbs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UInstancedStaticMeshComponent> Pillars;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UInstancedStaticMeshComponent> Ceilings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UInstancedStaticMeshComponent> Backdrop;

	/** Every instanced component, in a fixed order (floors, walls, curbs, pillars, ceilings, backdrop). */
	TArray<UInstancedStaticMeshComponent*> GetAllComponents() const;

	UFUNCTION(BlueprintPure, Category = "Area")
	int32 GetTotalInstanceCount() const;
};

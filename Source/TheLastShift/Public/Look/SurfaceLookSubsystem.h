// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SurfaceLookSubsystem.generated.h"

class UMaterialParameterCollection;
struct FSurfaceVariant;

/**
 * The look of a map's surfaces (Brief 21). When a map begins it pushes both variants of UTheLastShiftSettings::SurfaceLook
 * (home's and the twin structure's) into the world's instance of MPC_Surface, and which one the map shows (MapVariant:
 * 0 = home, for the maps of SurfaceLook.HomeMaps; 1 = the twin's, for every other map). The surface materials read them
 * from there; a material that always shows home's (the ship) ignores MapVariant. Normal time only: the materials are
 * opaque and never emissive, so nothing of this shows while Stopped or in a scan's capture. The collection's own defaults
 * are placeholders the game never shows.
 */
UCLASS()
class THELASTSHIFT_API USurfaceLookSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void PostInitialize() override;

	/** Pushes SurfaceLook into this world's MPC_Surface again (after a change for the session). False when there is no collection. */
	UFUNCTION(BlueprintCallable, Category = "Surface Look")
	bool ApplyLook();

	/** True when this map shows home's variant. */
	UFUNCTION(BlueprintPure, Category = "Surface Look")
	bool IsHomeLook() const;

	/** The collection the materials read (from the settings), or null when none is set. */
	static UMaterialParameterCollection* GetCollection();

	/** Names of the collection's parameters: Prefix ("Home" or "Twin") + Field. */
	static FName ParamName(bool bHome, const TCHAR* Field);

	/** MapVariant pushed last (0 home, 1 twin; -1 before the first push). */
	int32 GetPushedVariant() const { return PushedVariant; }

private:
	void PushVariant(class UMaterialParameterCollectionInstance* Instance, bool bHome, const FSurfaceVariant& Variant) const;

	int32 PushedVariant = -1;
};

// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeaconBody.generated.h"

class ALightGroupDevice;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * The body of a beacon: a lamp head hung on a rod that goes straight up, high above an open space. The actor stands at
 * the head's mount; the head turns at the mount to face the point it lights (its +X is the direction it lights) and the
 * rod, one metre of mesh along +Z from its lower end, is stretched up to RodLength. The beacon itself is a light group
 * with one light (Device), paid at its own terminal; the body only holds what the planner decided, for the validator and
 * the tests. It is ordinary geometry: no collision, no navigation, no custom depth (scans show it in the scan's own
 * colour), no point source. Without meshes it shows nothing and the beacon is a light alone.
 */
UCLASS(NotBlueprintable)
class THELASTSHIFT_API ABeaconBody : public AActor
{
	GENERATED_BODY()

public:
	ABeaconBody();

	/**
	 * Sets the meshes (either may be null), turns the head from the mount (the actor's location) to InAimPoint and
	 * stretches the rod up by InRodLength. bInRodReachesGeometry: the rod ends in the first geometry above the mount (else
	 * it has the maximum length and its upper end is lost in the dark).
	 */
	void Build(UStaticMesh* HeadMesh, UStaticMesh* RodMesh, const FVector& InAimPoint, float InRodLength, bool bInRodReachesGeometry);

	/** The point the beacon lights: where the route leaves its open segment, at floor height. */
	UFUNCTION(BlueprintPure, Category = "Beacon")
	FVector GetAimPoint() const { return AimPoint; }

	/** Unit direction from the mount to the aim point. */
	UFUNCTION(BlueprintPure, Category = "Beacon")
	FVector GetAimDirection() const;

	/** Where the light sits: the centre of the head's front face (its +X extent, halfway up), turned with the head; the mount itself without a head mesh. */
	UFUNCTION(BlueprintPure, Category = "Beacon")
	FVector GetLensLocation() const;

	UFUNCTION(BlueprintPure, Category = "Beacon", meta = (Units = "cm"))
	float GetRodLength() const { return RodLength; }

	UFUNCTION(BlueprintPure, Category = "Beacon")
	bool DoesRodReachGeometry() const { return bRodReachesGeometry; }

	/** World bounds of the meshes shown (head and rod); invalid when the body has no mesh. */
	UFUNCTION(BlueprintPure, Category = "Beacon")
	FBox GetBodyBounds() const;

	/** World bounds of the head alone; invalid without a head mesh. */
	UFUNCTION(BlueprintPure, Category = "Beacon")
	FBox GetHeadBounds() const;

	/** True when the body has at least one mesh. */
	UFUNCTION(BlueprintPure, Category = "Beacon")
	bool HasMesh() const;

	/** The beacon's light group (one light), paid at its terminal. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Beacon")
	TObjectPtr<ALightGroupDevice> Device;

	/** Index of the open segment it hangs over, in the area's segments. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Beacon")
	int32 Segment = -1;

	/** Mount height above the segment's highest floor or catwalk, as planned. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Beacon", meta = (Units = "cm"))
	float HeightAboveFloor = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Beacon")
	TObjectPtr<UStaticMeshComponent> Head;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Beacon")
	TObjectPtr<UStaticMeshComponent> Rod;

private:
	UPROPERTY(VisibleAnywhere, Category = "Beacon")
	FVector AimPoint = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Beacon", meta = (Units = "cm"))
	float RodLength = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Beacon")
	bool bRodReachesGeometry = false;
};

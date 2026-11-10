// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LidarPointSource.h"
#include "UObject/Object.h"
#include "LidarTestSource.generated.h"

/**
 * tls.LidarTestSource: a throwaway point source, a cube of PointCount points (fixed seed) to check the lidar plugin's
 * injection path in isolation. Debug only, never referenced by game code.
 */
UCLASS(Transient)
class UTlsLidarTestSource : public UObject, public ILidarPointSource
{
	GENERATED_BODY()

public:
	FVector Center = FVector::ZeroVector;
	float Size = 100.f;
	int32 PointCount = 500;
	FLidarSourceSettings Settings;

	virtual void GatherLidarPoints_Implementation(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints) override;
	virtual FLidarSourceSettings GetLidarSourceSettings_Implementation() const override { return Settings; }
};

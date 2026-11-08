// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LidarScanComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * One scan slot: a Niagara instance holding the points of one scan plus the depth target it was spawned from.
 * Slots are recycled oldest-first once MaxAliveScans is reached.
 */
USTRUCT()
struct FLidarScanSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Points;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> DepthTarget;

	/** Monotonic scan index this slot currently shows, INDEX_NONE when idle. */
	int32 ScanIndex = INDEX_NONE;

	/** Real (undilated) world time at which the scan was taken. */
	double StartRealTime = 0.0;

	bool IsActive() const { return ScanIndex != INDEX_NONE; }
};

/**
 * Depth-captured point scan.
 *
 * Attach to a camera. Scan() renders scene depth once from this component's transform into a render target and
 * hands it to a GPU Niagara system that spawns one world-space point per depth sample. Points fade to a non-zero
 * floor; up to MaxAliveScans scans coexist, the oldest is recycled first.
 */
UCLASS(ClassGroup = (Lidar), meta = (BlueprintSpawnableComponent))
class LIDARSCAN_API ULidarScanComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	ULidarScanComponent();

	/** Captures depth once and spawns a new scan, recycling the oldest one if MaxAliveScans is reached. */
	UFUNCTION(BlueprintCallable, Category = "Lidar")
	void Scan();

	/** Removes every alive scan immediately. */
	UFUNCTION(BlueprintCallable, Category = "Lidar")
	void ClearScans();

	/** Pushes the live tunables (points, fade) to every alive scan and trims slots above MaxAliveScans. */
	UFUNCTION(BlueprintCallable, Category = "Lidar")
	void ApplyLiveSettings();

	UFUNCTION(BlueprintPure, Category = "Lidar")
	int32 GetNumAliveScans() const;

	/** Total number of scans taken since BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "Lidar")
	int32 GetScanCount() const { return ScanCount; }

	// --- Capture (applied on the next scan) ---

	/** Width and height of the depth capture; one point per sample. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture", meta = (ClampMin = "64", ClampMax = "1024", UIMin = "64", UIMax = "1024"))
	int32 CaptureResolution = 256;

	/** Samples farther than this (and sky) produce no point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture", meta = (ClampMin = "100.0", ClampMax = "20000.0", Units = "cm"))
	float MaxRange = 3000.f;

	/** Horizontal capture FOV. 0 = use the FOV of the camera this component is attached to (90 if none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture", meta = (ClampMin = "0.0", ClampMax = "170.0", Units = "deg"))
	float FieldOfViewOverride = 0.f;

	/**
	 * Extra horizontal FOV added when matching the camera. The rendered view can be wider than the camera's
	 * nominal FOV (aspect-ratio axis constraint), so a small margin keeps scans reaching the screen edges.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "deg"))
	float FieldOfViewPadding = 10.f;

	/** Pulls each point towards the capture origin to avoid z-fighting with the surface it sits on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture", meta = (ClampMin = "0.0", ClampMax = "50.0", Units = "cm"))
	float SurfaceBias = 2.f;

	/** Excludes the owning actor's primitives from the depth capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture")
	bool bHideOwnerInCapture = true;

	// --- Points (live) ---

	/** World-space sprite size of one point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.1", ClampMax = "20.0", Units = "cm"))
	float PointSize = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points")
	FLinearColor PointColor = FLinearColor(0.4f, 0.85f, 1.f, 1.f);

	/** Emissive multiplier applied to PointColor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float PointIntensity = 4.f;

	// --- Fade (live) ---

	/** Time for a scan to fade from full alpha to AlphaFloor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Fade", meta = (ClampMin = "0.1", ClampMax = "60.0", Units = "s"))
	float FadeDuration = 6.f;

	/** Alpha a scan settles at once faded; never reaches zero while the scan is alive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Fade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AlphaFloor = 0.15f;

	// --- Scans ---

	/** Scans kept alive at once; the oldest is recycled first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Scans", meta = (ClampMin = "1", ClampMax = "6", UIMin = "1", UIMax = "6"))
	int32 MaxAliveScans = 3;

	// --- Assets ---

	/** GPU point system. Must expose the user parameters listed in LidarScanComponent.cpp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lidar|Assets")
	TSoftObjectPtr<UNiagaraSystem> PointSystem;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	bool EnsureInitialized();
	int32 AcquireSlot();
	void DestroySlot(FLidarScanSlot& Slot);
	void TrimSlots();
	UTextureRenderTarget2D* EnsureDepthTarget(FLidarScanSlot& Slot);
	float ResolveHorizontalFOV() const;
	void PushLiveParameters(UNiagaraComponent* Points) const;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> DepthCapture;

	UPROPERTY(Transient)
	TArray<FLidarScanSlot> Slots;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> LoadedPointSystem;

	int32 ScanCount = 0;
};

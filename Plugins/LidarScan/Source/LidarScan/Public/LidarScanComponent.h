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

	/** Fade-out started by ClearScans: total duration (0 = not fading) and time elapsed so far. */
	float FadeOutDuration = 0.f;
	float FadeOutElapsed = 0.f;

	bool IsActive() const { return ScanIndex != INDEX_NONE; }
	bool IsFadingOut() const { return IsActive() && FadeOutDuration > 0.f; }

	/** Multiplier applied to the point alpha by an ongoing fade-out, 1 when not fading. */
	float GetFadeOutAlpha() const
	{
		return FadeOutDuration > 0.f ? FMath::Clamp(1.f - FadeOutElapsed / FadeOutDuration, 0.f, 1.f) : 1.f;
	}
};

/** Scan lifecycle event: index of the scan (monotonic per component) and world time in seconds. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLidarScan, int32, ScanIndex, float, Timestamp);

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

	/**
	 * Removes every alive scan. With FadeOutSeconds > 0 the points fade to zero alpha over that time and are then
	 * released; with 0 they are removed immediately. Scans already fading out are not restarted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar")
	void ClearScans(float FadeOutSeconds = 0.f);

	/** Pushes the live tunables (points, fade) to every alive scan and trims slots above MaxAliveScans. */
	UFUNCTION(BlueprintCallable, Category = "Lidar")
	void ApplyLiveSettings();

	/** Scans currently shown, excluding those fading out after ClearScans. */
	UFUNCTION(BlueprintPure, Category = "Lidar")
	int32 GetAliveScanCount() const;

	/** Total number of scans taken since BeginPlay; also the index the next scan will get. */
	UFUNCTION(BlueprintPure, Category = "Lidar")
	int32 GetScanCount() const { return ScanCount; }

	/** Broadcast right after the depth capture for a scan has been issued. */
	UPROPERTY(BlueprintAssignable, Category = "Lidar|Events")
	FOnLidarScan OnScanStarted;

	/**
	 * Broadcast once the scan's points have been spawned: the frame after OnScanStarted, or two frames after for the
	 * first scan of a slot (its depth target is created then and the spawn waits one frame for it).
	 */
	UPROPERTY(BlueprintAssignable, Category = "Lidar|Events")
	FOnLidarScan OnScanCompleted;

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

	/**
	 * Random offset of each sample, in depth-target texels (per axis, uniform in [-SampleJitter, SampleJitter]),
	 * applied to both the depth read and the ray direction. Breaks up the moire of a regular sampling grid seen in
	 * perspective. Seeded by the scan index, so a scan is stable for its whole life and consecutive scans differ.
	 * Applied on the next scan.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SampleJitter = 0.5f;

	/** Excludes the owning actor's primitives from the depth capture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture")
	bool bHideOwnerInCapture = true;

	// --- Points (live) ---

	/**
	 * Diameter of one point in screen pixels, the same at every depth and from wherever the scan is viewed.
	 * The point system rescales each point every frame from its current view depth, so points stay screen pixels
	 * while the viewer moves inside a scan; FOV and viewport changes apply on the next frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.5", ClampMax = "8.0", UIMin = "0.5", UIMax = "8.0"))
	float PointPixelSize = 3.5f;

	/**
	 * Draw-time pull of each point towards the viewer, in point diameters. A point sits on the surface it was
	 * sampled from; seen at a grazing angle, half of its sprite would sink into that surface and be clipped by the
	 * depth test. Being relative to the point size, the pull is constant on screen and grows with distance
	 * (with the defaults at 1080p and 90 degrees FOV: about 2 cm at 1 m, 20 cm at 10 m); the size is corrected for it,
	 * so points stay PointPixelSize pixels. Points closer than the pull to an occluder can show through its edge.
	 * 0 = off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "32.0", UIMin = "0.0", UIMax = "32.0"))
	float PointDepthOffset = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points")
	FLinearColor PointColor = FLinearColor(0.4f, 0.85f, 1.f, 1.f);

	/**
	 * Emissive multiplier applied to PointColor; 1 = the point material's nominal brightness. Kept low so that the
	 * density compensation can brighten points (up to DensityBoostMax) before the tonemapper turns them white.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "4.0"))
	float PointIntensity = 0.3f;

	/**
	 * View-relative density compensation. Every point is the same size on screen, so the perceived brightness of a
	 * surface is its point density on screen, which grows as (ViewDepth / SampleDepth)^2 when a scan is seen from
	 * farther than it was taken from (and shrinks when seen from closer). Each point's intensity is scaled every
	 * frame by clamp(pow(SampleDepth / ViewDepth, DensityCompensation), DensityBoostMin, DensityBoostMax), where
	 * SampleDepth is the point's depth along the capture's forward axis and ViewDepth its depth along the current
	 * camera's forward axis. From the scan's own pose the factor is 1 everywhere. 2 cancels the density change,
	 * lower values only soften it; 0 = off (every point at PointIntensity).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "3.0"))
	float DensityCompensation = 2.f;

	/**
	 * Surface-angle density compensation, multiplied with the depth term above. A surface patch covers a screen area
	 * proportional to the cosine of the angle between its normal and the viewing direction, so the same points seen
	 * more obliquely than they were sampled crowd together (and spread out when seen more head-on). Each point's
	 * intensity is also scaled by pow(max(cos ViewAngle, e) / max(cos CaptureAngle, e), AngleCompensation), with the
	 * surface normal reconstructed from the depth capture when the scan is taken. Both factors go through the same
	 * DensityBoostMin / DensityBoostMax clamp. From the scan's own pose the term is 1. Samples on a depth
	 * discontinuity (no reliable neighbour) take the capture ray as their normal; points that carry no normal at all
	 * (injected points) keep a term of 1. 1 cancels the change, lower values only soften it; 0 = off (depth term only).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float AngleCompensation = 1.f;

	/** Lower clamp of the density compensation factor: how far points seen from farther or more obliquely than they were taken can dim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.01", UIMax = "1.0"))
	float DensityBoostMin = 0.02f;

	/** Upper clamp of the density compensation factor: how far points seen from closer or more head-on than they were taken can brighten. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "1.0", ClampMax = "16.0", UIMin = "1.0", UIMax = "16.0"))
	float DensityBoostMax = 4.f;

	// --- Fade (live) ---

	/** Time for a scan to fade from full alpha to AlphaFloor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Fade", meta = (ClampMin = "0.1", ClampMax = "60.0", Units = "s"))
	float FadeDuration = 12.f;

	/** Alpha a scan settles at once faded; never reaches zero while the scan is alive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Fade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AlphaFloor = 0.12f;

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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	bool EnsureInitialized();
	int32 AcquireSlot();
	void DestroySlot(FLidarScanSlot& Slot);
	void TrimSlots();
	UTextureRenderTarget2D* EnsureDepthTarget(FLidarScanSlot& Slot, bool& bOutCreated);
	void ActivateScanPoints(int32 ScanIndex);
	float ResolveHorizontalFOV() const;
	float ResolveViewPixelScale() const;
	void UpdateViewPixelScale();
	void PushLiveParameters(const FLidarScanSlot& Slot) const;
	void ReleaseSlot(FLidarScanSlot& Slot);
	void BroadcastScanCompleted(int32 ScanIndex);

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> DepthCapture;

	UPROPERTY(Transient)
	TArray<FLidarScanSlot> Slots;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> LoadedPointSystem;

	int32 ScanCount = 0;

	/** World size of one screen pixel at unit view depth for the current view, last value pushed to the scans. */
	float ViewPixelScale = 0.f;
};

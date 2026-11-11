// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LidarPointSource.h"
#include "UObject/ScriptInterface.h"
#include "LidarScanComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * One scan slot: a Niagara instance holding the points of one scan plus the depth target it was spawned from.
 * Once MaxAliveScans is reached the oldest scan is evicted (its particles removed) and its slot holds the new scan.
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

	/** Pose and coverage of the capture this slot's scan was taken with. */
	UPROPERTY(Transient)
	FLidarScanContext Context;

	/** Real time at which a later scan of the same component started; negative while this is the newest scan. */
	double SupersededRealTime = -1.0;

	/** Fade-out started by ClearScans: total duration (0 = not fading) and time elapsed so far. */
	float FadeOutDuration = 0.f;
	float FadeOutElapsed = 0.f;

	/** Real time until which an idle slot stays activated with nothing to spawn (warm-up); negative when not warming. */
	double WarmUntilRealTime = -1.0;

	bool IsActive() const { return ScanIndex != INDEX_NONE; }
	bool IsWarming() const { return !IsActive() && WarmUntilRealTime >= 0.0; }
	bool IsFadingOut() const { return IsActive() && FadeOutDuration > 0.f; }

	/** Multiplier applied to the point alpha by an ongoing fade-out, 1 when not fading. */
	float GetFadeOutAlpha() const
	{
		return FadeOutDuration > 0.f ? FMath::Clamp(1.f - FadeOutElapsed / FadeOutDuration, 0.f, 1.f) : 1.f;
	}
};

/** Diagnostics for one scan slot: which scan it holds and how many particles its point system currently simulates. */
USTRUCT(BlueprintType)
struct FLidarScanSlotStats
{
	GENERATED_BODY()

	/** Scan the slot holds, -1 when idle. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Diagnostics")
	int32 ScanIndex = INDEX_NONE;

	/** True while the slot's scan is fading out after ClearScans. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Diagnostics")
	bool bFadingOut = false;

	/** Live particles of the captured-points emitter. GPU counts are read back with a latency of a frame or two. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Diagnostics")
	int32 CapturedParticles = 0;

	/** Live particles of the injected-points emitter, same latency. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Diagnostics")
	int32 InjectedParticles = 0;
};

/** Scan lifecycle event: index of the scan (monotonic per component) and world time in seconds. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLidarScan, int32, ScanIndex, float, Timestamp);

/**
 * Depth-captured point scan.
 *
 * Attach to a camera. Scan() renders scene depth once from this component's transform into a render target and
 * hands it to a GPU Niagara system that spawns one world-space point per depth sample. Points fade to a non-zero
 * floor; up to MaxAliveScans scans coexist, beyond that the oldest is removed.
 */
UCLASS(ClassGroup = (Lidar), meta = (BlueprintSpawnableComponent))
class LIDARSCAN_API ULidarScanComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	ULidarScanComponent();

	/** Captures depth once and spawns a new scan, removing the oldest one if MaxAliveScans is reached. */
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

	/** Capture pose and coverage of every alive scan (those counted by GetAliveScanCount), oldest first. */
	UFUNCTION(BlueprintPure, Category = "Lidar")
	TArray<FLidarScanContext> GetAliveScans() const;

	/** One entry per scan slot (idle ones included), in slot order: the scan it holds and its live particle counts. */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Diagnostics")
	TArray<FLidarScanSlotStats> GetScanSlotStats() const;

	/**
	 * Diagnostics: reads back the depth capture of the newest alive scan (a synchronous GPU readback, a few ms).
	 * OutDepth holds OutWidth x OutHeight planar depths along the capture's forward axis, row by row from the top of
	 * the image (0 = no sample); OutContext is that scan's capture pose. False when no scan is alive or the readback failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Diagnostics")
	bool ReadNewestScanDepth(TArray<float>& OutDepth, int32& OutWidth, int32& OutHeight, FLidarScanContext& OutContext) const;

	// --- Injected points ---

	/**
	 * Adds a point source: from the next scan on, its points are gathered right after each depth capture and added to
	 * that scan. Held weakly; a source that is destroyed is dropped silently. Registering twice has no effect.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Injected")
	void RegisterPointSource(TScriptInterface<ILidarPointSource> Source);

	UFUNCTION(BlueprintCallable, Category = "Lidar|Injected")
	void UnregisterPointSource(TScriptInterface<ILidarPointSource> Source);

	/** Number of registered sources that are still alive. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Injected")
	int32 GetPointSourceCount() const;

	/** Counters of the last scan's injection (all zero, ScanIndex -1, before the first scan). */
	UFUNCTION(BlueprintPure, Category = "Lidar|Injected")
	FLidarInjectionStats GetLastInjectionStats() const { return LastInjectionStats; }

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

	/**
	 * Width and height of the depth capture; one point per sample, so a scan has up to CaptureResolution^2 points
	 * (fewer after MinSampleDepth, MaxRange and the near thinning). This is the points-per-scan budget.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture", meta = (ClampMin = "64", ClampMax = "1024", UIMin = "64", UIMax = "1024"))
	int32 CaptureResolution = 256;

	/** Samples farther than this (and sky) produce no point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Capture", meta = (ClampMin = "100.0", ClampMax = "30000.0", Units = "cm"))
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

	/**
	 * Near-field thinning. A regular capture grid puts far more samples per square metre on surfaces close to the
	 * capture origin (the density falls as 1 / depth^2), so the area around the scan reads as a solid sheet. Samples
	 * closer than this depth are kept with probability (Depth / NearThinningDistance)^2, which makes the near field
	 * roughly as dense in world space as a surface at NearThinningDistance; farther samples are all kept. The choice
	 * is seeded per scan, so a scan is stable for its whole life. Depth is the planar depth along the capture's forward
	 * axis. 0 = off. Applied on the next scan.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "1000.0", UIMin = "0.0", UIMax = "1000.0", Units = "cm"))
	float NearThinningDistance = 250.f;

	/** Samples closer than this planar depth produce no point at all. 0 = off. Applied on the next scan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "200.0", UIMin = "0.0", UIMax = "200.0", Units = "cm"))
	float MinSampleDepth = 40.f;

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
	 * Draw-time lift of each point off the surface it was sampled from, along that surface's normal, towards the side
	 * the surface was scanned from. A point sits on its surface; seen at a grazing angle, half of its sprite would sink
	 * into it and be clipped by the depth test. A fixed world distance, unlike a pull towards the viewer, never grows
	 * with viewing distance, so it cannot carry a point through a wall thicker than SurfaceOffset. 0 = off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "5.0", Units = "cm"))
	float SurfaceOffset = 1.5f;

	/**
	 * For points that carry no surface normal (injected points): draw-time pull towards the current viewer, at most
	 * this distance (and never more than half the distance to the viewer). Capped in world units for the same reason
	 * as SurfaceOffset. 0 = off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0", Units = "cm"))
	float ViewOffsetMaxCm = 2.5f;

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

	// --- Injected (applied on the next scan) ---

	/** Upper bound on the points all sources together add to one scan; points beyond it are dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Injected", meta = (ClampMin = "0", ClampMax = "65536", UIMin = "0", UIMax = "65536"))
	int32 MaxInjectedPointsPerScan = 4096;

	/**
	 * Diagnostics: when injected points that use bOccludeByCapture were uploaded, read the depth capture back on the
	 * CPU (a synchronous GPU readback, a few ms) and count how many of them the GPU depth test will remove, into
	 * GetLastInjectionStats().BehindCapturedDepth. The depth test itself always runs on the GPU; this only counts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Injected")
	bool bVerifyInjectedOcclusion = false;

	// --- Scans ---

	/**
	 * Ghost cap: scans kept alive at once. When a new scan needs a slot and all are taken, the oldest scan is removed
	 * (its points disappear in both emitters) and its slot holds the new scan. Lowering it removes the oldest scans at
	 * the next scan (or at once through ApplyLiveSettings).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Scans", meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "16"))
	int32 MaxAliveScans = 3;

	/** Upper clamp of MaxAliveScans. */
	static constexpr int32 MaxAliveScansLimit = 16;

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
	/** Creates and warms up slots until MaxAliveScans exist, so no scan uses a point system instance created that frame. */
	void PrepareSlots();
	/** Adds a slot with its point system component and depth target; INDEX_NONE on failure. */
	int32 CreateSlot();
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

	/** Gathers every source for this scan, filters by coverage and the cap, and hands the points to the slot's system. */
	void InjectPoints(const FLidarScanSlot& Slot, UTextureRenderTarget2D* Target);

	/** CPU count of uploaded points that lie behind the captured depth (see bVerifyInjectedOcclusion). */
	int32 CountBehindCapturedDepth(UTextureRenderTarget2D* Target, const FLidarScanContext& Context) const;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> DepthCapture;

	UPROPERTY(Transient)
	TArray<FLidarScanSlot> Slots;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> LoadedPointSystem;

	int32 ScanCount = 0;

	/** Registered point sources, held weakly. */
	TArray<TWeakObjectPtr<UObject>> PointSources;

	FLidarInjectionStats LastInjectionStats;

	/** Per-scan upload buffers, kept to avoid reallocating at every scan. */
	TArray<FVector> InjectedPositions;
	TArray<FLinearColor> InjectedColors;
	TArray<FVector2D> InjectedParams;
	TArray<FVector2D> InjectedTransient;

	/** World size of one screen pixel at unit view depth for the current view, last value pushed to the scans. */
	float ViewPixelScale = 0.f;
};

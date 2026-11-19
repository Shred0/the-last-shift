// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LidarPointSource.h"
#include "UObject/ScriptInterface.h"
#include "LidarScanComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
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

	/** DepthTarget carries a channel per texel (RGBA32f: R depth, G channel) instead of depth alone (R32f). */
	bool bDepthTargetHasChannels = false;

	/**
	 * Target the point system's spawn writes its samples into while lines are on (RGBA32f, twice the capture's width; layout
	 * in the contract in LidarScanComponent.cpp). Created at run time, the first time the slot scans with lines on.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> SampleTarget;

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

	/** The slot's point system has its second renderer on (points behind geometry, occluded point material). */
	bool bOccludedMaterial = false;

	/** World time at which the scan was taken (the clock of the pulse and of the lines' growth). */
	double StartWorldTime = 0.0;

	/** Last growth fraction of the lines pushed to the slot's system, negative when none. */
	float PushedLinkGrowth = -1.f;

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

	/** Live particles of the lines emitter (one per link once the scan's links are found), same latency. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Diagnostics")
	int32 LineParticles = 0;
};

/** Counters of the neighbour search of one scan (see ULidarScanComponent::LinkNeighbours). */
USTRUCT(BlueprintType)
struct FLidarLinkStats
{
	GENERATED_BODY()

	/** Scan the search ran for, -1 before the first search. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 ScanIndex = INDEX_NONE;

	/** Captured points of the scan (samples that spawn a point). */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 Points = 0;

	/** Links handed to the point system (at most MaxLinksPerScan). */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 Links = 0;

	/** Links found over MaxLinksPerScan and dropped. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 Dropped = 0;

	/** Most links on one point (never above LinkNeighbours). */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 MaxDegree = 0;

	/** Longest link, cm (never above LinkMaxDistance). */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	float LongestLink = 0.f;

	/** Time of the search on its worker thread, ms. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	float SearchMilliseconds = 0.f;

	/** Real time from the scan to the links reaching its point system, s. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	float ReadySeconds = 0.f;

	/**
	 * The scan got no links because its samples could not be trusted: a newer scan started before its samples were read
	 * back, or the read-back live samples did not match its captured points (Points then holds the read-back count).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	bool bRejected = false;

	/** Texels of the sample target the spawn did not write (0 when every texel of the capture was written). */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 UnwrittenTexels = 0;
};

/** One link of a scan (diagnostics): the two points' surface positions, their texels in the capture and their channels. */
USTRUCT(BlueprintType)
struct FLidarLink
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	FVector Start = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	FVector End = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 StartTexel = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 EndTexel = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 StartChannel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Lidar|Links")
	int32 EndChannel = 0;
};

struct FLidarLinkJob;

/**
 * Look of one colour channel: captured points whose surface carries the channel's custom depth stencil value take this
 * colour and intensity instead of the component's PointColor.
 */
USTRUCT(BlueprintType)
struct FLidarChannel
{
	GENERATED_BODY()

	/** Point colour of the channel. RGB is multiplied by the component's PointIntensity, like PointColor; A is ignored (the alpha follows the scan). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Channels")
	FLinearColor Color = FLinearColor::White;

	/** Extra brightness factor for the channel's points, on top of PointIntensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Channels", meta = (ClampMin = "0.0", ClampMax = "16.0", UIMin = "0.0", UIMax = "16.0"))
	float IntensityMultiplier = 1.f;
};

/** A registered point source with its priority and registration order (ties keep the order). */
struct FLidarPointSourceEntry
{
	TWeakObjectPtr<UObject> Source;
	int32 Priority = 0;
	int32 Order = 0;
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
	 * that scan. Held weakly; a source that is destroyed is dropped silently. Sources are served by Priority, higher
	 * first, ties in registration order; when MaxInjectedPointsPerScan is reached the remaining (lowest-priority)
	 * points are dropped. Registering a source again only changes its priority; it keeps its place among equals.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Injected")
	void RegisterPointSource(TScriptInterface<ILidarPointSource> Source, int32 Priority = 0);

	UFUNCTION(BlueprintCallable, Category = "Lidar|Injected")
	void UnregisterPointSource(TScriptInterface<ILidarPointSource> Source);

	/** Number of registered sources that are still alive. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Injected")
	int32 GetPointSourceCount() const;

	/** Priority the source was registered with; 0 when it is not registered. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Injected")
	int32 GetPointSourcePriority(TScriptInterface<ILidarPointSource> Source) const;

	/** Injected points the last scan dropped because MaxInjectedPointsPerScan was reached (0 before the first scan). */
	UFUNCTION(BlueprintPure, Category = "Lidar|Injected")
	int32 GetLastDroppedInjectedPoints() const { return LastInjectionStats.OverCap; }

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
	 * Emissive multiplier applied to PointColor; 1 = the point material's nominal brightness: the level of one point
	 * seen on its own, at any distance. Kept low so that the angle compensation can brighten points (up to
	 * DensityBoostMax) before the tonemapper turns them white.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "4.0"))
	float PointIntensity = 0.3f;

	/**
	 * Density compensation by on-screen crowding. Every point is the same size on screen, so a point reads the same at
	 * any distance as long as it stands apart from its neighbours; once the points of a scan merge on screen, the surface
	 * would turn into a bright sheet. Each point's intensity is scaled every frame by
	 * clamp(pow(Spacing / PointPixelSize, DensityCompensation), DensityBoostMin, 1), where Spacing is the on-screen
	 * distance between neighbouring samples of its scan: the width of one capture texel at the point's depth along the
	 * capture's forward axis (SampleDepth * 2 tan(HFOV / 2) / capture width), times max(1, NearThinningDistance /
	 * SampleDepth) for captured points (the thinning spreads them), seen from the point's depth along the current
	 * camera's forward axis. While the points are separate dots (Spacing >= PointPixelSize) each keeps its full level;
	 * once they merge, the surface stays at the level of one dot (2: the factor follows the points' on-screen density).
	 * From the scan's own pose a capture texel is (viewport width / capture width) x (tan(capture HFOV / 2) / tan(view
	 * HFOV / 2)) pixels wide on screen, wider than a point at the usual sizes, so the factor there is 1 (for a 256 capture
	 * on a 1920 viewport, 7.5 pixels against PointPixelSize 3.5). Each scan is dimmed by its own spacing
	 * only: several scans of one surface add up. 0 = off (every point at PointIntensity).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "3.0"))
	float DensityCompensation = 2.f;

	/**
	 * Surface-angle density compensation, multiplied with the depth term above. A surface patch covers a screen area
	 * proportional to the cosine of the angle between its normal and the viewing direction, so the same points seen
	 * more obliquely than they were sampled crowd together (and spread out when seen more head-on). Each point's
	 * intensity is also scaled by pow(max(cos ViewAngle, e) / max(cos CaptureAngle, e), AngleCompensation), with the
	 * surface normal reconstructed from the depth capture when the scan is taken. Both factors go through the same
	 * DensityBoostMin / DensityBoostMax clamp (the depth term alone never exceeds 1). From the scan's own pose the term is 1. Samples on a depth
	 * discontinuity (no reliable neighbour) take the capture ray as their normal; points that carry no normal at all
	 * (injected points) keep a term of 1. 1 cancels the change, lower values only soften it; 0 = off (depth term only).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float AngleCompensation = 1.f;

	/** Lower clamp of the density compensation factor: how far points crowded on screen, or seen more obliquely than they were taken, can dim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.01", UIMax = "1.0"))
	float DensityBoostMin = 0.02f;

	/** Upper clamp of the density compensation factor: how far points seen more head-on than they were taken can brighten (the depth term never brightens). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Points", meta = (ClampMin = "1.0", ClampMax = "16.0", UIMin = "1.0", UIMax = "16.0"))
	float DensityBoostMax = 4.f;

	// --- Fade (live) ---

	/** Time for a scan to fade from full alpha to AlphaFloor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Fade", meta = (ClampMin = "0.1", ClampMax = "60.0", Units = "s"))
	float FadeDuration = 12.f;

	/** Alpha a scan settles at once faded; never reaches zero while the scan is alive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Fade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AlphaFloor = 0.12f;

	// --- Occlusion (live) ---

	/**
	 * Points behind the real geometry: 0 hides them (the engine's depth test, exactly as a component without this
	 * setting: same material, same points, same cost); above 0 they are drawn at this factor of their own intensity,
	 * channel colour kept. At 1 every alive point is drawn as if it had no depth test: the scan looks as if the geometry
	 * were not there. Between 0 and 1 a point behind geometry by less than max(5 cm, 1% of its depth), or within 8 pixels
	 * of an occluder's edge, stays hidden. A point on a surface in view is never treated as hidden. Injected points behind
	 * geometry stay hidden unless their source sets FLidarSourceSettings::bShowBehindGeometry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Occlusion", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OccludedIntensity = 0.f;

	/**
	 * Pulse: for this long after each scan starts, every alive point (all alive scans, the new one included) is at full
	 * intensity, hidden or not. Timed on world time, like the scans' fade, so it stands still while the game is paused.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Occlusion", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0", Units = "s"))
	float LinkSeconds = 1.5f;

	/** Pulse: after LinkSeconds, hidden points go from full intensity to OccludedIntensity over this long (0 = at once). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Occlusion", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0", Units = "s"))
	float OcclusionFadeSeconds = 0.75f;

	/** Sets OccludedIntensity, LinkSeconds and OcclusionFadeSeconds (clamped) and applies them to every alive scan at once. */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Occlusion")
	void SetOcclusion(float InOccludedIntensity, float InLinkSeconds, float InFadeSeconds);

	/**
	 * Factor applied now to points behind geometry that are shown (1 during the link time, OccludedIntensity once the
	 * fade is over). OccludedIntensity when no scan has started yet; 0 when occlusion display is off.
	 */
	UFUNCTION(BlueprintPure, Category = "Lidar|Occlusion")
	float GetOccludedFactor() const;

	/** True when points behind geometry are shown: OccludedIntensity above 0 and the occluded point material loaded. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Occlusion")
	bool IsOcclusionDisplayActive() const;

	/** Number of slots whose point system currently draws points behind geometry with the occluded point material (diagnostics). */
	UFUNCTION(BlueprintPure, Category = "Lidar|Diagnostics")
	int32 GetOccludedMaterialSlotCount() const;

	// --- Links (neighbours, distance, angle and cap applied on the next scan; width and intensity live) ---

	/**
	 * Neighbour lines. 0 = off: no search, no line, and the capture, the points and the cost are those of a component
	 * without lines. Above 0, each captured point of a scan is linked to at most this many of its nearest points of the
	 * same scan and channel within LinkMaxDistance: a link joins two points when each is among the other's nearest
	 * (mutual nearest neighbours), so no point has more links than this. Links are found once per scan, on a worker
	 * thread, from the scan's depth capture read back from the GPU a few frames after the scan; they never join two scans
	 * and never touch injected points. A scan's lines grow from both of their points toward each other over LinkSeconds,
	 * fade, clear and are removed with the scan, and are depth-tested against the real geometry whatever OccludedIntensity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Links", meta = (ClampMin = "0", ClampMax = "8", UIMin = "0", UIMax = "8"))
	int32 LinkNeighbours = 0;

	/** Longest link, between the two points' surface positions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Links", meta = (ClampMin = "1.0", ClampMax = "1000.0", UIMin = "1.0", UIMax = "1000.0", Units = "cm"))
	float LinkMaxDistance = 50.f;

	/**
	 * Largest angle between the surface normals of two linked points. A link across a convex corner would pass inside
	 * the solid and one across a depth discontinuity would hang in the air; both have normals far apart. 180 = no limit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Links", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0", Units = "deg"))
	float LinkMaxNormalAngle = 180.f;

	/** Most links one scan keeps; links found beyond it are dropped (GetLastLinkStats().Dropped). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Links", meta = (ClampMin = "0", ClampMax = "262144", UIMin = "0", UIMax = "262144"))
	int32 MaxLinksPerScan = 65536;

	/** Width of a line in screen pixels, the same at every depth (taken at the line's middle). Live. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Links", meta = (ClampMin = "0.5", ClampMax = "8.0", UIMin = "0.5", UIMax = "8.0"))
	float LinePixelWidth = 1.5f;

	/**
	 * Brightness of a line as a factor of the brightness of its points (same colour, angle compensation and fade). Instead
	 * of the points' depth term a line takes min(1, Spacing / LinePixelWidth), Spacing being the points' on-screen spacing
	 * at the line's middle: a line keeps its level at any distance until the lines of its scan crowd on screen. Live.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Links", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "4.0"))
	float LineIntensity = 0.5f;

	/**
	 * Lift of a line's two ends off their surface, on top of SurfaceOffset, along the points' normals, as a fraction of the
	 * width of one capture texel at the point's depth from the scan (depth * 2 tan(HFOV/2) / CaptureResolution). A sample
	 * takes its texel's depth with a jittered ray, so on a surface seen at an angle it can sit behind the surface by up to
	 * half a texel's depth step, which grows with depth; a depth-tested line ending there is hidden near that end. 0 = off
	 * (lines end where their points are drawn). Points are not moved. Live.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Links", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0"))
	float LineLiftTexels = 0.f;

	/** Most a line's end is lifted by LineLiftTexels. Live. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Links", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0", Units = "cm"))
	float LineLiftMaxCm = 30.f;

	/** Sets the lift of the lines' ends (clamped), at once. */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Links")
	void SetLineLift(float InLiftTexels, float InLiftMaxCm);

	/** Sets the link values (clamped). Neighbours, distance and angle apply from the next scan; width and intensity at once. */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Links")
	void SetLinks(int32 InNeighbours, float InMaxDistance, float InPixelWidth, float InIntensity, float InMaxNormalAngle = 180.f);

	/** Counters of the last scan whose links reached its point system. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Links")
	FLidarLinkStats GetLastLinkStats() const { return LastLinkStats; }

	/** Links the last searched scan dropped over MaxLinksPerScan. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Links")
	int32 GetLastDroppedLinks() const { return LastLinkStats.Dropped; }

	/** Growth of the lines of an alive scan, 0 to 1 over LinkSeconds of world time; -1 when no alive scan has that index. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Links")
	float GetLinkGrowth(int32 ScanIndex) const;

	/** Searches started and not yet handed to their scan (diagnostics). */
	UFUNCTION(BlueprintPure, Category = "Lidar|Diagnostics")
	int32 GetPendingLinkSearches() const { return LinkJobs.Num(); }

	/** Scans that got no links because their samples could not be trusted (see FLidarLinkStats::bRejected), since BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Diagnostics")
	int32 GetRejectedLinkScans() const { return RejectedLinkScans; }

	/** Search results that arrived for a scan already gone (slot reused, cleared) and were dropped untouched, since BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Diagnostics")
	int32 GetDiscardedLinkResults() const { return DiscardedLinkResults; }

	/**
	 * Diagnostics: a finished search waits this long (real time) before it is handed to its scan, so that tests can make a
	 * result arrive after its scan is gone. 0 in normal use.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Diagnostics", meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float LinkResultDelaySeconds = 0.f;

	/**
	 * Diagnostics: the samples the point system spawned for the last searched scan, as read back: per texel its surface
	 * position and its channel (-1 where the texel spawned no live point, -2 where the spawn did not write it), with the
	 * capture's width and the scan's capture pose. False before the first search.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Diagnostics")
	bool GetLastScanSamples(TArray<FVector>& OutPositions, TArray<int32>& OutChannels, int32& OutWidth, FLidarScanContext& OutContext) const;

	/**
	 * Diagnostics: the links of the last searched scan, and per texel of its capture the planar depth of its point (-1
	 * without one) and its number of links. False before the first search.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Diagnostics")
	bool GetLastScanLinks(TArray<FLidarLink>& OutLinks, TArray<float>& OutPointDepths, TArray<int32>& OutDegrees, int32& OutWidth) const;

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

	// --- Channels ---

	/** Number of colour channels besides the default channel (stencil values 1 to NumChannels). */
	static constexpr int32 NumChannels = 4;

	/**
	 * Colour channels. When on, every captured point reads the custom depth stencil value of the surface it was sampled
	 * from (at its own texel, never filtered) and values 1 to NumChannels take that channel's colour and intensity; 0
	 * and larger values keep PointColor. Only the colour and intensity change: size, compensation, offsets, thinning,
	 * jitter, fade and slots are the same for every channel. Needs custom depth with stencil in the project
	 * (r.CustomDepth=3); without it the component warns once and scans as if channels were off. Off: the capture and
	 * the points are exactly those of a component without channels. Applied on the next scan.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar|Channels")
	bool bUseChannels = false;

	/** Channel 1 to NumChannels, in order. Change at runtime with SetChannel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize, Category = "Lidar|Channels")
	TArray<FLidarChannel> Channels;

	/** Sets the colour and intensity multiplier of Channel (1 to NumChannels); alive scans change at once. Ignored out of range. */
	UFUNCTION(BlueprintCallable, Category = "Lidar|Channels")
	void SetChannel(int32 Channel, FLinearColor Color, float IntensityMultiplier = 1.f);

	/** Look of Channel (1 to NumChannels); a default FLidarChannel out of range. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Channels")
	FLidarChannel GetChannel(int32 Channel) const;

	/** True when the next scan will carry channels: bUseChannels, custom depth with stencil enabled and the capture material loaded. */
	UFUNCTION(BlueprintPure, Category = "Lidar|Channels")
	bool AreChannelsActive() const;

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

	/**
	 * GPU point system. Must expose the user parameters listed in LidarScanComponent.cpp.
	 * Every soft asset reference of this class's defaults is added to the cook by the module (LidarScanModule.cpp),
	 * so an asset the component loads at runtime must be held in a soft object property like this one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lidar|Assets")
	TSoftObjectPtr<UNiagaraSystem> PointSystem;

	/**
	 * Post-process material of the capture when channels are on (replaces the tonemapper): writes the scene depth to R
	 * and, where the custom depth surface is the one the sample hit, the custom stencil value to G. Same cook rule as
	 * PointSystem.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lidar|Assets")
	TSoftObjectPtr<UMaterialInterface> ChannelCaptureMaterial;

	/**
	 * Material of the point system's second renderer, on while OccludedIntensity > 0: no depth test, adds only the points
	 * behind geometry (see the contract in LidarScanComponent.cpp). Same cook rule as PointSystem.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lidar|Assets")
	TSoftObjectPtr<UMaterialInterface> OccludedPointMaterial;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	bool EnsureInitialized();
	/** Whether the next scan uses channels; loads the capture material and warns once when something is missing. */
	bool ResolveChannels();
	/** Sets the capture up for a depth-only scan (exactly as without channels) or for depth plus channel. */
	void ConfigureCapture(bool bChannels);
	/** Creates and warms up slots until MaxAliveScans exist, so no scan uses a point system instance created that frame. */
	void PrepareSlots();
	/** Adds a slot with its point system component and depth target; INDEX_NONE on failure. */
	int32 CreateSlot();
	int32 AcquireSlot();
	void DestroySlot(FLidarScanSlot& Slot);
	void TrimSlots();
	UTextureRenderTarget2D* EnsureDepthTarget(FLidarScanSlot& Slot, bool bChannels, bool& bOutCreated);
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

	/** Whether points behind geometry are shown; loads the occluded material and creates its instance once. */
	bool ResolveOcclusion();
	/** Gives every slot the material that matches ResolveOcclusion() and pushes the current pulse factor. */
	void RefreshOcclusion();
	/** Pushes GetOccludedFactor() to the occluded material instance when it changed. */
	void PushOccludedFactor();

	/** Prepares the neighbour search of the scan just captured into the slot; true when the slot's sample target was created now. */
	bool StartLinkSearch(FLidarScanSlot& Slot);
	/** Marks a scan's samples for readback, once its spawn has been issued (from BroadcastScanCompleted). */
	void ReadBackSamples(int32 ScanIndex);
	/** Queues the GPU copy of a job's sample target. */
	void EnqueueSampleCopy(const TSharedPtr<FLidarLinkJob, ESPMode::ThreadSafe>& Job);
	/** Live particles of the captured-points emitter of a slot (GPU count, read back with a frame or two of latency). */
	int32 GetCapturedParticles(const FLidarScanSlot& Slot) const;
	/** Ends a scan's search without links: its lines die, the scan is counted as rejected. */
	void RejectLinks(FLidarScanSlot* Slot, const FLidarLinkJob& Job, int32 Points, const TCHAR* Why);

	/**
	 * 1 x 1 target bound as the sample target of every slot that has none of its own (lines never on in it): the point
	 * system's render target interface needs a bound target, and with User.ExportSamples false nothing is written to it.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> PlaceholderSampleTarget;

	/** The placeholder sample target, created on first use. */
	UTextureRenderTarget2D* GetPlaceholderSampleTarget();

	int32 RejectedLinkScans = 0;
	int32 DiscardedLinkResults = 0;
	TArray<FVector> LastSamplePositions;
	TArray<int32> LastSampleChannels;
	FLidarScanContext LastSampleContext;
	/** Advances the searches (readback ready, search launched, links handed to their slot); true while any is pending. */
	bool TickLinkSearches();
	/** Pushes the growth of the lines of every alive scan when it changed. */
	void PushLinkGrowth(FLidarScanSlot& Slot, bool bForce = false);

	/** Neighbour searches in flight, oldest first. */
	TArray<TSharedPtr<FLidarLinkJob, ESPMode::ThreadSafe>> LinkJobs;

	FLidarLinkStats LastLinkStats;
	TArray<FLidarLink> LastLinks;
	TArray<float> LastLinkPointDepths;
	TArray<int32> LastLinkDegrees;
	int32 LastLinkWidth = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedOccludedMaterial;

	/** Instance of the occluded point material shared by every slot; carries the pulse factor. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OccludedMaterialInstance;

	/** The occluded material could not be loaded; reported once. */
	bool bReportedOcclusionUnavailable = false;

	/** World time at which the newest scan started (the pulse), negative before the first scan. */
	double PulseStartTime = -1.0;

	/** Last factor pushed to OccludedMaterialInstance, negative when none. */
	float PushedOccludedFactor = -1.f;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> DepthCapture;

	UPROPERTY(Transient)
	TArray<FLidarScanSlot> Slots;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> LoadedPointSystem;

	/** Set once the missing PointSystem has been reported, so the error is logged once per component. */
	bool bReportedMissingPointSystem = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedChannelMaterial;

	/** Channels were requested but cannot run (no custom depth with stencil, or no material); reported once. */
	bool bReportedChannelsUnavailable = false;

	/** The capture is currently set up for channels. */
	bool bCaptureHasChannels = false;

	int32 ScanCount = 0;

	/** Registered point sources, held weakly, in registration order. */
	TArray<FLidarPointSourceEntry> PointSources;

	/** Registration order of the next new source. */
	int32 NextSourceOrder = 0;

	FLidarInjectionStats LastInjectionStats;

	/** Per-scan upload buffers, kept to avoid reallocating at every scan. */
	TArray<FVector> InjectedPositions;
	TArray<FLinearColor> InjectedColors;
	TArray<FVector2D> InjectedParams;
	TArray<FVector2D> InjectedTransient;
	TArray<float> InjectedBehindWeight;

	/** World size of one screen pixel at unit view depth for the current view, last value pushed to the scans. */
	float ViewPixelScale = 0.f;
};

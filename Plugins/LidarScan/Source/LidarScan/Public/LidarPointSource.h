// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LidarPointSource.generated.h"

/**
 * Pose and coverage of one scan's depth capture, handed to point sources at scan time and returned for alive scans
 * by ULidarScanComponent::GetAliveScans().
 *
 * Coverage: a world point P is inside the capture when its planar depth d = dot(P - CaptureOrigin, CaptureForward)
 * is in (0, MaxRange) and |dot(P - CaptureOrigin, CaptureRight)| <= d * TanHalfFOV.X and
 * |dot(P - CaptureOrigin, CaptureUp)| <= d * TanHalfFOV.Y.
 */
USTRUCT(BlueprintType)
struct LIDARSCAN_API FLidarScanContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	FVector CaptureOrigin = FVector::ZeroVector;

	/** Unit view direction of the capture. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	FVector CaptureForward = FVector::ForwardVector;

	/** Unit screen-right axis of the capture. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	FVector CaptureRight = FVector::RightVector;

	/** Unit screen-up axis of the capture. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	FVector CaptureUp = FVector::UpVector;

	/** Tangent of half the horizontal (X) and vertical (Y) field of view. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	FVector2D TanHalfFOV = FVector2D(1.0, 1.0);

	/** Planar depth beyond which the capture records nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar", meta = (Units = "cm"))
	float MaxRange = 0.f;

	/** Monotonic scan index (ULidarScanComponent::GetScanCount() at the time of the scan). */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	int32 ScanIndex = INDEX_NONE;

	/** World time of the scan, in seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar", meta = (Units = "s"))
	float Timestamp = 0.f;

	/** True if WorldPosition lies inside the capture coverage (frustum and range); OutDepth is its planar depth. */
	bool IsInCoverage(const FVector& WorldPosition, double& OutDepth) const;
};

/** One point provided by a point source: a world position and a per-point intensity factor. */
USTRUCT(BlueprintType)
struct LIDARSCAN_API FLidarInjectedPoint
{
	GENERATED_BODY()

	FLidarInjectedPoint() = default;
	FLidarInjectedPoint(const FVector& InPosition, float InIntensity = 1.f) : WorldPosition(InPosition), Intensity(InIntensity) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar")
	FVector WorldPosition = FVector::ZeroVector;

	/** Multiplies the source colour and intensity for this point only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float Intensity = 1.f;
};

/** How the alpha of a source's points follows the age of the scan that holds them. */
UENUM(BlueprintType)
enum class ELidarFadeMode : uint8
{
	/** Same age fade as captured points: from full alpha to the scan's alpha floor over its fade duration. */
	ScanFade,
	/** No age fade: full alpha while the scan is alive. */
	None,
	/**
	 * Full alpha for HoldSeconds after the scan, then to zero over VanishSeconds, on real time. Timed from the scan
	 * that holds the points only: later scans never extend it.
	 */
	Transient,
	/**
	 * Full alpha while the scan that holds the points is the component's newest scan; once a later scan starts, to
	 * zero over VanishSeconds on real time, then removed. At most one scan's worth of these points is ever shown.
	 */
	UntilNextScan,
};

/** How the points of one source are drawn and filtered. Read once per scan, when the source is gathered. */
USTRUCT(BlueprintType)
struct LIDARSCAN_API FLidarSourceSettings
{
	GENERATED_BODY()

	/** Point colour. RGB is multiplied by the scan component's PointIntensity, like captured points; A is the opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar")
	FLinearColor Color = FLinearColor::White;

	/** Extra brightness factor for this source's points, on top of the scan component's PointIntensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float IntensityMultiplier = 1.f;

	/** How the points' alpha changes with the age of their scan. ClearScans removes them in every mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar")
	ELidarFadeMode FadeMode = ELidarFadeMode::ScanFade;

	/** Transient only: seconds after the scan during which the points stay at full alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "s", EditCondition = "FadeMode == ELidarFadeMode::Transient"))
	float HoldSeconds = 0.5f;

	/**
	 * Transient: seconds over which the points go from full alpha to zero once the hold is over.
	 * UntilNextScan: seconds over which they go from full alpha to zero once a later scan has started.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "s", EditCondition = "FadeMode == ELidarFadeMode::Transient || FadeMode == ELidarFadeMode::UntilNextScan"))
	float VanishSeconds = 2.0f;

	/**
	 * Points the capture could not have seen are dropped at scan time: outside the capture coverage, or behind the
	 * captured depth at their pixel by more than OcclusionTolerance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar")
	bool bOccludeByCapture = true;

	/** How far behind the captured surface a point may be and still count as visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lidar", meta = (ClampMin = "0.0", ClampMax = "200.0", Units = "cm"))
	float OcclusionTolerance = 10.f;
};

UINTERFACE(BlueprintType, MinimalAPI)
class ULidarPointSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything that adds points to a scan without being rendered. Register the implementing object on a
 * ULidarScanComponent; at every scan, right after the depth capture, the component gathers the source's points and
 * adds them to that scan: same scan instance, same ghost cap, same ClearScans, same screen-space size and intensity
 * rules as captured points (injected points carry no surface normal, so only the depth term of the density
 * compensation applies to them).
 */
class LIDARSCAN_API ILidarPointSource
{
	GENERATED_BODY()

public:
	/** Appends this source's points for the scan described by Context. Called once per scan, on the game thread. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Lidar|Injected")
	void GatherLidarPoints(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints);

	/** Render and filter settings for this source's points. Called once per scan, before GatherLidarPoints. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Lidar|Injected")
	FLidarSourceSettings GetLidarSourceSettings() const;
};

/** Per-scan injection counters, for diagnostics. */
USTRUCT(BlueprintType)
struct LIDARSCAN_API FLidarInjectionStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	int32 ScanIndex = INDEX_NONE;

	/** Registered sources still alive at the scan. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	int32 Sources = 0;

	/** Points returned by all sources. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	int32 Gathered = 0;

	/** Dropped because they were outside the capture coverage (occluding sources only). */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	int32 OutsideCoverage = 0;

	/** Dropped because MaxInjectedPointsPerScan was reached. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	int32 OverCap = 0;

	/** Handed to the point system. Points behind the captured depth are removed from these on the GPU, at spawn. */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	int32 Uploaded = 0;

	/**
	 * Of the uploaded points, how many the GPU depth test removes: computed on the CPU from a readback of the depth
	 * capture, only when ULidarScanComponent::bVerifyInjectedOcclusion is on; -1 otherwise.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Lidar")
	int32 BehindCapturedDepth = -1;
};

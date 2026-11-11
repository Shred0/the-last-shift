// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LidarPointSource.h"
#include "Time/TimeState.h"
#include "Entity.generated.h"

class APawn;
class UScanDirectorComponent;
class UTimeStateSubsystem;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogEntity, Log, All);

UENUM(BlueprintType)
enum class EEntityState : uint8
{
	/** Not yet awakened: never appears. */
	Dormant,
	/** Appears on every Stop. */
	Awake
};

/** What contact with the figure does to the Stop (set on each area map's entity). */
UENUM(BlueprintType)
enum class EEntityContactResponse : uint8
{
	/** Back to the Stop anchor, normal time (exit reason Rewound). */
	Rewind,
	/** Time resumes where the player stands; the outside world jumps forward (exit reason Jumped). */
	Jump,
	/** Back to the Stop anchor, and the outside world jumps forward (exit reason RewoundAndJumped). */
	RewindAndJump
};

/** Exit reason that a contact response ends the Stop with. */
THELASTSHIFT_API EStopExitReason GetExitReasonForContact(EEntityContactResponse Response);

inline const TCHAR* LexToString(EEntityContactResponse Response)
{
	switch (Response)
	{
	case EEntityContactResponse::Rewind: return TEXT("Rewind");
	case EEntityContactResponse::Jump: return TEXT("Jump");
	case EEntityContactResponse::RewindAndJump: return TEXT("RewindAndJump");
	}
	return TEXT("Unknown");
}

/** How the last spawn point was chosen. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FEntitySpawnReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	FVector Location = FVector::ZeroVector;

	/** Path distance the spawn aimed for (GetSpawnDistanceForNextStop at the spawn). */
	UPROPERTY(BlueprintReadOnly, Category = "Entity", meta = (Units = "cm"))
	float TargetDistance = 0.f;

	/** Navigation path distance from the chosen point to the player. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity", meta = (Units = "cm"))
	float PathDistance = 0.f;

	/** Navigation queries made and distinct candidates evaluated. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	int32 Attempts = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	int32 Candidates = 0;

	/** Candidates with no line of sight from the camera and a path distance within tolerance: the ones picked from. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	int32 ValidCandidates = 0;

	/** Candidates with no line of sight from the camera, whatever their distance. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	int32 NoLineOfSightCandidates = 0;

	/** Valid candidates within SpawnRepeatRadius of the previous spawn. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	int32 ValidNearPreviousSpawn = 0;

	/** Drawn at random among the valid candidates (false: no valid candidate, Brief 3 best-score choice). */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	bool bRandomPick = false;

	/** Weight of the chosen candidate and the sum of the weights it was drawn from. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	float PickWeight = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	float TotalWeight = 0.f;

	/** Distance from the previous spawn point; -1 for the first spawn. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity", meta = (Units = "cm"))
	float DistanceFromPreviousSpawn = -1.f;

	/** Every valid candidate was within SpawnRepeatRadius of the previous spawn, so the repeat weight was ignored. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	bool bOnlyNearPreviousSpawn = false;

	/** Criteria met by the chosen point. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	bool bNoLineOfSight = false;

	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	bool bDistanceInTolerance = false;

	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	bool bHiddenFromScans = false;

	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	bool bInForwardHemisphere = false;

	/** No candidate reached the target distance; the farthest reachable point was used. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity")
	bool bFallback = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEntityAwakened);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEntitySpawned, FVector, Location, bool, bHadLineOfSight);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEntityPathRefreshed, int32, BeatIndex, float, PathDistance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEntityContact);

/**
 * The thing that closes in while time is stopped. No mesh: it exists only as points injected into lidar scans
 * (ILidarPointSource, registered through the player's scan director), in two forms: the figure (a procedural humanoid
 * refracted in RefractionCount copies around the vertical axis through its head) and the trail (the stretch of its
 * navigation path ahead of it, jittered: the tell).
 *
 * Dormant until UTimeStateSubsystem's cumulative stopped time reaches AwakenThresholdSeconds; the check runs on the
 * director's beats, so it always awakens (and first appears) on a beat. Once awake it appears on every Stop at a
 * navmesh point drawn at random among the valid candidates (no line of sight from the camera, path distance within
 * SpawnDistanceTolerance of GetSpawnDistanceForNextStop()), weighted up when not visible from any alive scan and when
 * in the player's forward hemisphere, weighted down within SpawnRepeatRadius of the previous spawn. While Stopped it
 * moves continuously at Speed along a navigation path to the player, recomputed on every beat; it never moves in
 * Normal time. Within ContactRadius of the player it ends the Stop through the director as ContactResponse says. On a
 * voluntary Resume the next spawn distance becomes clamp(DistanceAtExit - ApproachPerExit, MinSpawnDistance,
 * InitialSpawnDistance); after any contact it is MinSpawnDistance. Its points are shown by the latest scan only: full
 * until the next scan starts, then gone over PointVanishSeconds, so at most one snapshot of it is ever on screen.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API AEntity : public AActor, public ILidarPointSource
{
	GENERATED_BODY()

public:
	AEntity();

	// --- Awakening ---

	/** Cumulative stopped time (UTimeStateSubsystem) at which the entity awakens, checked on beats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Awakening", meta = (ClampMin = "0.0", ClampMax = "600.0", Units = "s"))
	float AwakenThresholdSeconds = 18.f;

	// --- Spawn ---

	/** Path distance of the first appearance, and the upper bound of every later one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "100.0", ClampMax = "10000.0", Units = "cm"))
	float InitialSpawnDistance = 1500.f;

	/** Lower bound of the spawn distance; the spawn distance after any contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "0.0", ClampMax = "10000.0", Units = "cm"))
	float MinSpawnDistance = 600.f;

	/** How much closer than the last exit distance the next Stop starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "0.0", ClampMax = "5000.0", Units = "cm"))
	float ApproachPerExit = 300.f;

	/** Distinct navmesh candidates evaluated per spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "1", ClampMax = "64"))
	int32 SpawnCandidates = 16;

	/** A candidate's path distance counts as on target within this fraction of the target distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SpawnDistanceTolerance = 0.2f;

	/** Weight factor of a valid candidate not visible from any alive scan origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float SpawnHiddenWeight = 4.f;

	/** Weight factor of a valid candidate in the player's forward hemisphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float SpawnForwardWeight = 2.f;

	/** Valid candidates closer than this to the previous spawn point get SpawnRepeatWeight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "0.0", ClampMax = "5000.0", Units = "cm"))
	float SpawnRepeatRadius = 400.f;

	/**
	 * Weight factor of a valid candidate within SpawnRepeatRadius of the previous spawn. 0 = never repeat while any
	 * other valid candidate exists. Ignored (and logged) when every valid candidate is near the previous spawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpawnRepeatWeight = 0.f;

	/** Seed of the random pick among valid candidates (the navmesh sampling itself uses the engine's random stream). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Spawn")
	int32 SpawnSeed = 2026;

	// --- Movement ---

	/** Speed along the navigation path while time is Stopped (66.7 cm/s = 3 m per default 4.5 s beat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Movement", meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm/s"))
	float Speed = 66.6667f;

	/** Distance between the figure's feet and the player's feet that counts as contact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Movement", meta = (ClampMin = "10.0", ClampMax = "1000.0", Units = "cm"))
	float ContactRadius = 150.f;

	// --- Contact ---

	/** What contact does to the Stop. Any contact loses the residues collected during the Stop (residue system). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Contact")
	EEntityContactResponse ContactResponse = EEntityContactResponse::Rewind;

	/**
	 * A voluntary Resume with the figure manifested and its path distance at the exit (GetLastExitPathDistance, the
	 * distance the exit already records) within this radius is a graze: the home jumps by a fraction of the act's jump.
	 * 0 = off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Contact", meta = (ClampMin = "0.0", ClampMax = "5000.0", Units = "cm"))
	float GrazeRadius = 400.f;

	// --- Trail ---

	/** Length of path ahead of the figure covered by the trail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Trail", meta = (ClampMin = "0.0", ClampMax = "5000.0", Units = "cm"))
	float TrailReach = 600.f;

	/** Distance along the path between two trail points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Trail", meta = (ClampMin = "5.0", ClampMax = "500.0", Units = "cm"))
	float TrailPointSpacing = 5.f;

	/** Sideways jitter of trail points, uniform in [-TrailLateralJitter, TrailLateralJitter]. New for every scan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Trail", meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	float TrailLateralJitter = 12.f;

	/** Height of trail points above the path, uniform in [0, TrailVerticalJitter]. New for every scan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Trail", meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	float TrailVerticalJitter = 30.f;

	// --- Figure ---

	/** Points of one copy of the figure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Figure", meta = (ClampMin = "10", ClampMax = "2000"))
	int32 FigurePointCount = 250;

	/** Copies of the figure around the vertical axis through its head. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Figure", meta = (ClampMin = "1", ClampMax = "8"))
	int32 RefractionCount = 4;

	/** Yaw between consecutive copies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Figure", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float RefractionAngleStep = 90.f;

	/**
	 * Lean of every copy away from the vertical, pivoting on the head: the copies share the head and their bodies fan
	 * out in RefractionCount directions. 0 = the copies only turn in place.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Figure", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "deg"))
	float RefractionTilt = 15.f;

	/** Seed of the figure's point set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Figure")
	int32 FigureSeed = 1977;

	// --- Render ---

	// Colour of every entity point: ScanColors.Entity of the project settings (UTheLastShiftSettings), its one owner.

	/** Brightness of entity points relative to captured points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Render", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float IntensityMultiplier = 1.f;

	/**
	 * Entity points stay at full intensity until the next scan starts; then they go to zero over this time and are
	 * removed (lidar fade mode UntilNextScan).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Render", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "s"))
	float PointVanishSeconds = 0.3f;

	/** How far behind the captured surface an entity point may be and still be drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Render", meta = (ClampMin = "0.0", ClampMax = "200.0", Units = "cm"))
	float OcclusionTolerance = 15.f;

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "Entity")
	EEntityState GetState() const { return State; }

	/** True while it is present in the current Stop. */
	UFUNCTION(BlueprintPure, Category = "Entity")
	bool IsManifested() const { return bManifested; }

	/** Feet of the figure, on the navmesh (last position when not manifested). */
	UFUNCTION(BlueprintPure, Category = "Entity")
	FVector GetFigureLocation() const { return FigureLocation; }

	/**
	 * True when a figure standing at FigureBase (feet, on the navmesh) can be seen from Eye: the same line-of-sight
	 * traces the spawn uses (any of the figure's visibility heights unobstructed on the visibility channel).
	 */
	UFUNCTION(BlueprintPure, Category = "Entity")
	bool CanBeSeenFrom(const FVector& Eye, const FVector& FigureBase) const { return HasLineOfSight(Eye, FigureBase); }

	/** Fresh navigation path distance from the figure to the player; -1 when not manifested or unreachable. */
	UFUNCTION(BlueprintPure, Category = "Entity", meta = (Units = "cm"))
	float GetPathDistanceToPlayer() const;

	/** 1 - PathDistance / InitialSpawnDistance, clamped to [0, 1]; when not manifested, the value at the last exit (1 after a contact); 0 while dormant. */
	UFUNCTION(BlueprintPure, Category = "Entity")
	float GetProximity01() const;

	/** Path distance the next appearance aims for. */
	UFUNCTION(BlueprintPure, Category = "Entity", meta = (Units = "cm"))
	float GetSpawnDistanceForNextStop() const;

	/** Path distance recorded at the last exit from a Stop while manifested; -1 before the first. */
	UFUNCTION(BlueprintPure, Category = "Entity", meta = (Units = "cm"))
	float GetLastExitPathDistance() const { return LastExitPathDistance; }

	/**
	 * Records the exit of the current Stop now, if it has not been recorded yet (once per Stop). The entity records it
	 * itself when the time state changes; a listener of the scan director's OnStopExited calls this first so that it
	 * reads this Stop's exit whatever the order of the time-state listeners. No-op when nothing is pending.
	 */
	void RecordStopExit(EStopExitReason Reason);

	/**
	 * True when the last Stop ended with a voluntary Resume while the figure was manifested and its recorded exit path
	 * distance was within GrazeRadius (GrazeRadius > 0). False after any other exit, and while dormant.
	 */
	UFUNCTION(BlueprintPure, Category = "Entity")
	bool WasLastExitGraze() const { return bLastExitGraze; }

	UFUNCTION(BlueprintPure, Category = "Entity")
	FEntitySpawnReport GetLastSpawnReport() const { return LastSpawnReport; }

	/** Current navigation path, figure first, player last. */
	UFUNCTION(BlueprintPure, Category = "Entity")
	TArray<FVector> GetCurrentPath() const { return PathPoints; }

	/** Points returned by the last GatherLidarPoints (before the lidar's occlusion), figure and trail. */
	UFUNCTION(BlueprintPure, Category = "Entity")
	int32 GetLastGatheredFigurePoints() const { return LastGatheredFigurePoints; }

	UFUNCTION(BlueprintPure, Category = "Entity")
	int32 GetLastGatheredTrailPoints() const { return LastGatheredTrailPoints; }

	// --- Actions ---

	/** Awakens now (OnAwakened) and, if time is Stopped, appears at once. No-op when already awake. */
	UFUNCTION(BlueprintCallable, Category = "Entity")
	void Awaken();

	/** Overrides the spawn distance of the next appearance (clamped to [MinSpawnDistance, InitialSpawnDistance]). */
	UFUNCTION(BlueprintCallable, Category = "Entity")
	void SetSpawnDistanceForNextStop(float Distance);

	/** Forgets the previous spawn point and restarts the spawn pick's random stream from SpawnSeed. */
	UFUNCTION(BlueprintCallable, Category = "Entity")
	void ResetSpawnHistory();

	/** Appears now at the given path distance (or GetSpawnDistanceForNextStop() if <= 0). Needs time Stopped; awakens if dormant. */
	UFUNCTION(BlueprintCallable, Category = "Entity")
	bool SpawnNow(float Distance = 0.f);

	// --- Events ---

	/** Once, when the cumulative stopped time first reaches the threshold. */
	UPROPERTY(BlueprintAssignable, Category = "Entity|Events")
	FOnEntityAwakened OnAwakened;

	/** Every appearance: where, and whether no candidate without line of sight from the camera was found. */
	UPROPERTY(BlueprintAssignable, Category = "Entity|Events")
	FOnEntitySpawned OnSpawned;

	/** Every beat while manifested, after the path refresh: beat index and path distance to the player. */
	UPROPERTY(BlueprintAssignable, Category = "Entity|Events")
	FOnEntityPathRefreshed OnPathRefreshed;

	/** The figure reached the player; the ContactResponse exit follows immediately. */
	UPROPERTY(BlueprintAssignable, Category = "Entity|Events")
	FOnEntityContact OnContact;

	// --- ILidarPointSource ---

	virtual void GatherLidarPoints_Implementation(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints) override;
	virtual FLidarSourceSettings GetLidarSourceSettings_Implementation() const override;

	/** Debug lines for the figure, path and trail (development builds; tls.Entity.Draw). */
	bool bDebugDraw = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	UFUNCTION()
	void HandleBeat(int32 BeatIndex);

	/** Finds the time state and the player's scan director, subscribes and registers as a point source. */
	bool EnsureBound();
	void Unbind();

	APawn* GetPlayerPawn() const;
	bool GetPlayerNavLocation(FVector& OutLocation) const;

	/** Chooses a spawn point at TargetDistance and appears there. */
	bool Manifest(float TargetDistance, const TCHAR* Why);
	void Unmanifest();

	bool FindPath(const FVector& From, const FVector& To, TArray<FVector>& OutPoints, float& OutLength) const;
	bool RefreshPath();
	void AdvanceAlongPath(float Distance);
	float GetRemainingPathLength() const;

	/** True if any of the figure's sample heights at FigureBase is visible from Eye. */
	bool HasLineOfSight(const FVector& Eye, const FVector& FigureBase) const;
	bool IsVisibleFromScans(const FVector& FigureBase, const TArray<FLidarScanContext>& Scans) const;

	/** Ground under the figure (the navmesh sits slightly above it). */
	FVector GetFigureGround() const;

	void RecordExit(EStopExitReason Reason);
	void CheckContact();
	void DrawDebug() const;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	TWeakObjectPtr<UScanDirectorComponent> Director;

	EEntityState State = EEntityState::Dormant;
	bool bManifested = false;

	FVector FigureLocation = FVector::ZeroVector;
	float FigureYaw = 0.f;

	/** Navigation path from the figure (first point, moved as it advances) to the player. */
	TArray<FVector> PathPoints;
	double LastPathRefreshTime = -1.0;

	/** < 0 = not set yet (InitialSpawnDistance). */
	float NextSpawnDistance = -1.f;
	float ProximityAtExit = 0.f;
	float LastExitPathDistance = -1.f;

	/** A Stop began and its exit has not been recorded yet. */
	bool bExitPending = false;
	bool bLastExitGraze = false;

	FEntitySpawnReport LastSpawnReport;

	/** Random stream of the spawn pick and the seed it was started from. */
	FRandomStream SpawnRng;
	int32 SpawnRngSeed = 0;
	bool bSpawnRngInitialized = false;

	bool bHasPreviousSpawn = false;
	FVector PreviousSpawnLocation = FVector::ZeroVector;

	/** Figure point set and the tunables it was built from. */
	TArray<FVector> FigureShape;
	int32 FigureShapeCount = -1;
	int32 FigureShapeSeed = 0;

	int32 LastGatheredFigurePoints = 0;
	int32 LastGatheredTrailPoints = 0;

	mutable float CachedPathDistance = -1.f;
	mutable uint64 CachedPathDistanceFrame = MAX_uint64;
};

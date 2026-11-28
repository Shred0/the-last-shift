// SPDX-License-Identifier: MIT

#include "Entity/Entity.h"

#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Entity/EntityFigureShape.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Entity)

DEFINE_LOG_CATEGORY(LogEntity);

TOptional<EEntityForm> AEntity::SessionForm;

namespace EntityTuning
{
	// Mirrors of the UPROPERTY clamps: Blueprint and C++ writes bypass ClampMin/ClampMax.
	static float Threshold(float V) { return FMath::Clamp(V, 0.f, 600.f); }
	static float Initial(float V) { return FMath::Clamp(V, 100.f, 10000.f); }
	static float Speed(float V) { return FMath::Clamp(V, 0.f, 1000.f); }
	static float Contact(float V) { return FMath::Clamp(V, 10.f, 1000.f); }
	static float SightingShare(float V) { return FMath::Clamp(V, 0.f, 0.95f); }
	static int32 SightingScans(int32 V) { return FMath::Clamp(V, 1, 8); }

	/** Heights above the feet sampled for line-of-sight and scan visibility checks (shins, waist, head). */
	static constexpr float VisibilityHeights[] = { 30.f, 100.f, 165.f };

	/** Two candidates closer than this are the same place. */
	static constexpr float CandidateMinSeparation = 100.f;

	/** Shortest time between two path refreshes made because the path ran out between beats. */
	static constexpr double ExhaustedPathRefreshInterval = 0.25;
}

EStopExitReason GetExitReasonForContact(EEntityContactResponse Response)
{
	switch (Response)
	{
	case EEntityContactResponse::Jump: return EStopExitReason::Jumped;
	case EEntityContactResponse::RewindAndJump: return EStopExitReason::RewoundAndJumped;
	case EEntityContactResponse::Rewind:
	default: return EStopExitReason::Rewound;
	}
}

AEntity::AEntity()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetCanBeDamaged(false);
}

// --- Lifecycle ---

void AEntity::BeginPlay()
{
	Super::BeginPlay();
	FigureLocation = GetActorLocation();
	FigureYaw = GetActorRotation().Yaw;
	// The form's points are computed once, here, behind the travel screen (not at the first scan).
	RefreshFigureShape();

	EnsureBound();
	// The player pawn may not be possessed yet on the first frame.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { EnsureBound(); }));
}

void AEntity::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearWalls(TEXT("end of play"));
	Unbind();
	Super::EndPlay(EndPlayReason);
}

bool AEntity::EnsureBound()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!TimeState)
	{
		TimeState = World->GetSubsystem<UTimeStateSubsystem>();
		if (TimeState)
		{
			TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &AEntity::HandleTimeStateChanged);
		}
	}

	const APawn* Pawn = GetPlayerPawn();
	UScanDirectorComponent* NewDirector = Pawn ? Pawn->FindComponentByClass<UScanDirectorComponent>() : nullptr;
	if (NewDirector && NewDirector != Director.Get())
	{
		if (UScanDirectorComponent* Old = Director.Get())
		{
			Old->OnBeat.RemoveDynamic(this, &AEntity::HandleBeat);
			Old->UnregisterPointSource(TScriptInterface<ILidarPointSource>(this));
		}
		Director = NewDirector;
		NewDirector->OnBeat.AddUniqueDynamic(this, &AEntity::HandleBeat);
		NewDirector->RegisterPointSource(TScriptInterface<ILidarPointSource>(this));
		UE_LOG(LogEntity, Log, TEXT("%s: bound to %s's scan director."), *GetName(), *GetNameSafe(Pawn));
	}
	return TimeState && Director.IsValid();
}

void AEntity::Unbind()
{
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.RemoveDynamic(this, &AEntity::HandleTimeStateChanged);
		TimeState = nullptr;
	}
	if (UScanDirectorComponent* Current = Director.Get())
	{
		Current->OnBeat.RemoveDynamic(this, &AEntity::HandleBeat);
		Current->UnregisterPointSource(TScriptInterface<ILidarPointSource>(this));
	}
	Director.Reset();
}

APawn* AEntity::GetPlayerPawn() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

bool AEntity::GetPlayerNavLocation(FVector& OutLocation) const
{
	const APawn* Pawn = GetPlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	OutLocation = Pawn->GetNavAgentLocation();
	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(OutLocation, Projected, FVector(100.f, 100.f, 250.f)))
		{
			OutLocation = Projected.Location;
		}
	}
	return true;
}

// --- Time state and beats ---

void AEntity::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	EnsureBound();

	if (NewState == ETimeState::Stopped)
	{
		bExitPending = true;
		bLastExitGraze = false;
		bSightedThisStop = false;
		bLastStopSighted = false;
		bContactThisStop = false;
		bLastStopContact = false;
		SightingStreak = 0;
		++StopSerial;
		bWallsChosenThisStop = false;
		if (State == EEntityState::Awake)
		{
			Manifest(GetSpawnDistanceForNextStop(), TEXT("stop"));
			// Brief 30: the Stop's walls, chosen now and standing until it ends.
			RaiseWalls(TEXT("stop"));
		}
		return;
	}

	if (OldState == ETimeState::Stopped)
	{
		RecordStopExit(Reason);
	}
	Unmanifest();
	// However the Stop ended, its walls are gone with it.
	ClearWalls(TEXT("the Stop ended"));
}

void AEntity::RecordStopExit(EStopExitReason Reason)
{
	if (!bExitPending)
	{
		return;
	}
	bExitPending = false;
	bLastExitGraze = false;
	// Found at the end of a Stop with a clear sighting or a contact, however the Stop ends; a contact is the first one too.
	const bool bContact = bContactThisStop;
	const bool bSighted = bSightedThisStop;
	bLastStopSighted = bSighted;
	bLastStopContact = bContact;
	bSightedThisStop = false;
	bContactThisStop = false;
	SightingStreak = 0;
	if (URunSubsystem* Run = URunSubsystem::Get(this); Run && (bContact || bSighted))
	{
		if (bContact)
		{
			Run->MarkFirstContact(FString::Printf(TEXT("%s: the Stop ended %s with a contact"), *GetName(), LexToString(Reason)));
		}
		Run->MarkEntityFound(FString::Printf(TEXT("%s: %s in the Stop that ended %s"), *GetName(), bContact ? TEXT("a contact") : TEXT("a clear sighting"), LexToString(Reason)),
			bContact ? EEntityFoundBy::Contact : EEntityFoundBy::Sighting);
	}
	if (!bManifested)
	{
		return;
	}
	RecordExit(Reason);
	const float Radius = FMath::Clamp(GrazeRadius, 0.f, 5000.f);
	bLastExitGraze = Reason == EStopExitReason::Resumed && Radius > 0.f && LastExitPathDistance >= 0.f && LastExitPathDistance <= Radius;
	if (bLastExitGraze)
	{
		UE_LOG(LogEntity, Log, TEXT("%s: graze: resumed with the figure at path distance %.1f cm (GrazeRadius %.1f cm)."), *GetName(), LastExitPathDistance, Radius);
	}
}

void AEntity::HandleBeat(int32 BeatIndex)
{
	if (!TimeState || !TimeState->IsStopped())
	{
		return;
	}

	if (State == EEntityState::Dormant)
	{
		const float Cumulative = TimeState->GetCumulativeStoppedSeconds();
		const float Threshold = EntityTuning::Threshold(AwakenThresholdSeconds);
		UE_LOG(LogEntity, Log, TEXT("%s: beat %d dormant, cumulative stopped %.3f s / %.3f s."), *GetName(), BeatIndex, Cumulative, Threshold);
		if (Cumulative + 1.e-3f >= Threshold)
		{
			Awaken();
		}
		return;
	}

	if (!bManifested)
	{
		// The spawn at the Stop failed (no player or navmesh then): try again on each beat.
		Manifest(GetSpawnDistanceForNextStop(), TEXT("beat retry"));
		return;
	}

	RefreshPath();
	const float Distance = GetPathDistanceToPlayer();
	UE_LOG(LogEntity, Log, TEXT("%s: beat %d path refreshed, path distance %.1f cm, figure at %s."), *GetName(), BeatIndex, Distance, *FigureLocation.ToString());
	OnPathRefreshed.Broadcast(BeatIndex, Distance);
}

void AEntity::Awaken()
{
	if (State == EEntityState::Awake)
	{
		return;
	}
	State = EEntityState::Awake;
	UE_LOG(LogEntity, Log, TEXT("%s: awakened (cumulative stopped %.3f s)."), *GetName(), TimeState ? TimeState->GetCumulativeStoppedSeconds() : 0.f);
	OnAwakened.Broadcast();

	if (TimeState && TimeState->IsStopped() && !bManifested)
	{
		Manifest(GetSpawnDistanceForNextStop(), TEXT("awakening"));
	}
	// Brief 30: awakened inside a Stop, it raises that Stop's walls now.
	if (TimeState && TimeState->IsStopped() && !bWallsChosenThisStop)
	{
		RaiseWalls(TEXT("awakening"));
	}
}

void AEntity::SetSpawnDistanceForNextStop(float Distance)
{
	NextSpawnDistance = Distance;
}

void AEntity::ResetSpawnHistory()
{
	bHasPreviousSpawn = false;
	SpawnRng.Initialize(SpawnSeed);
	SpawnRngSeed = SpawnSeed;
	bSpawnRngInitialized = true;
	// Brief 30: the walls' own stream starts again with it, and no strategic wall is remembered.
	bWallRngInitialized = false;
	LastStrategicPassage = INDEX_NONE;
}

bool AEntity::SpawnNow(float Distance)
{
	EnsureBound();
	if (!TimeState || !TimeState->IsStopped())
	{
		UE_LOG(LogEntity, Warning, TEXT("%s: SpawnNow needs time Stopped."), *GetName());
		return false;
	}
	if (Distance > 0.f)
	{
		SetSpawnDistanceForNextStop(Distance);
	}
	if (State == EEntityState::Dormant)
	{
		State = EEntityState::Awake;
		OnAwakened.Broadcast();
	}
	const bool bManifestedNow = Manifest(GetSpawnDistanceForNextStop(), TEXT("SpawnNow"));
	if (!bWallsChosenThisStop)
	{
		RaiseWalls(TEXT("SpawnNow"));
	}
	return bManifestedNow;
}

// --- Queries ---

float AEntity::GetSpawnDistanceForNextStop() const
{
	const float Initial = EntityTuning::Initial(InitialSpawnDistance);
	const float Min = FMath::Clamp(MinSpawnDistance, 0.f, Initial);
	return NextSpawnDistance < 0.f ? Initial : FMath::Clamp(NextSpawnDistance, Min, Initial);
}

float AEntity::GetPathDistanceToPlayer() const
{
	if (!bManifested)
	{
		return -1.f;
	}
	if (CachedPathDistanceFrame == GFrameCounter)
	{
		return CachedPathDistance;
	}

	CachedPathDistanceFrame = GFrameCounter;
	CachedPathDistance = -1.f;
	FVector Goal;
	TArray<FVector> Points;
	float Length = 0.f;
	if (GetPlayerNavLocation(Goal) && FindPath(FigureLocation, Goal, Points, Length))
	{
		CachedPathDistance = Length;
	}
	return CachedPathDistance;
}

float AEntity::GetProximity01() const
{
	if (State == EEntityState::Dormant)
	{
		return 0.f;
	}
	if (!bManifested)
	{
		return ProximityAtExit;
	}
	const float Distance = GetPathDistanceToPlayer();
	return Distance < 0.f ? 0.f : FMath::Clamp(1.f - Distance / EntityTuning::Initial(InitialSpawnDistance), 0.f, 1.f);
}

// --- Navigation ---

bool AEntity::FindPath(const FVector& From, const FVector& To, TArray<FVector>& OutPoints, float& OutLength) const
{
	OutPoints.Reset();
	OutLength = 0.f;
	UWorld* World = GetWorld();
	const UNavigationPath* Path = World ? UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, GetPlayerPawn()) : nullptr;
	if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
	{
		return false;
	}
	OutPoints = Path->PathPoints;
	OutLength = static_cast<float>(Path->GetPathLength());
	return true;
}

bool AEntity::RefreshPath()
{
	FVector Goal;
	TArray<FVector> Points;
	float Length = 0.f;
	LastPathRefreshTime = GetRunSeconds();
	if (!GetPlayerNavLocation(Goal) || !FindPath(FigureLocation, Goal, Points, Length))
	{
		UE_LOG(LogEntity, Warning, TEXT("%s: no navigation path from %s to the player; keeping the old path."), *GetName(), *FigureLocation.ToString());
		return false;
	}
	PathPoints = MoveTemp(Points);
	PathPoints[0] = FigureLocation;
	CachedPathDistanceFrame = MAX_uint64;
	return true;
}

void AEntity::AdvanceAlongPath(float Distance)
{
	while (Distance > 0.f && PathPoints.Num() >= 2)
	{
		const FVector Segment = PathPoints[1] - PathPoints[0];
		const float Length = Segment.Size();
		if (Length > UE_KINDA_SMALL_NUMBER)
		{
			FigureYaw = Segment.Rotation().Yaw;
		}
		if (Length <= Distance)
		{
			Distance -= Length;
			PathPoints.RemoveAt(0, EAllowShrinking::No);
		}
		else
		{
			PathPoints[0] += Segment * (Distance / Length);
			Distance = 0.f;
		}
	}
	if (PathPoints.Num() > 0)
	{
		FigureLocation = PathPoints[0];
	}
	CachedPathDistanceFrame = MAX_uint64;
}

float AEntity::GetRemainingPathLength() const
{
	float Length = 0.f;
	for (int32 Index = 1; Index < PathPoints.Num(); ++Index)
	{
		Length += FVector::Dist(PathPoints[Index - 1], PathPoints[Index]);
	}
	return Length;
}

// --- Spawn ---

bool AEntity::HasLineOfSight(const FVector& Eye, const FVector& FigureBase) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EntityLineOfSight), false, this);
	if (const APawn* Pawn = GetPlayerPawn())
	{
		Params.AddIgnoredActor(Pawn);
	}
	for (const float Height : EntityTuning::VisibilityHeights)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, FigureBase + FVector(0.f, 0.f, Height), ECC_Visibility, Params))
		{
			return true;
		}
	}
	return false;
}

bool AEntity::IsVisibleFromScans(const FVector& FigureBase, const TArray<FLidarScanContext>& Scans) const
{
	for (const FLidarScanContext& Scan : Scans)
	{
		bool bAnyInCoverage = false;
		for (const float Height : EntityTuning::VisibilityHeights)
		{
			double Depth = 0.0;
			bAnyInCoverage |= Scan.IsInCoverage(FigureBase + FVector(0.f, 0.f, Height), Depth);
		}
		if (bAnyInCoverage && HasLineOfSight(Scan.CaptureOrigin, FigureBase))
		{
			return true;
		}
	}
	return false;
}

bool AEntity::Manifest(float TargetDistance, const TCHAR* Why)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	APawn* Pawn = GetPlayerPawn();
	FVector PlayerLocation;
	if (!NavSys || !Pawn || !GetPlayerNavLocation(PlayerLocation))
	{
		UE_LOG(LogEntity, Warning, TEXT("%s: cannot appear (%s): no navigation system or player."), *GetName(), Why);
		return false;
	}

	FVector Eye;
	FRotator ViewRotation;
	if (const AController* Controller = Pawn->GetController())
	{
		Controller->GetPlayerViewPoint(Eye, ViewRotation);
	}
	else
	{
		Pawn->GetActorEyesViewPoint(Eye, ViewRotation);
	}
	const FVector Forward2D = FVector(ViewRotation.Vector().X, ViewRotation.Vector().Y, 0.f).GetSafeNormal();
	const TArray<FLidarScanContext> Scans = Director.IsValid() ? Director->GetRecentScanOrigins() : TArray<FLidarScanContext>();

	struct FCandidate
	{
		FVector Location;
		TArray<FVector> Path; // candidate -> player
		float PathLength = 0.f;
		bool bNoLineOfSight = false;
		bool bInTolerance = false;
		bool bHidden = false;
		bool bForward = false;
		bool bValid = false;
		bool bNearPrevious = false;
		float Score = 0.f;
	};
	TArray<FCandidate> Candidates;

	const int32 Wanted = FMath::Clamp(SpawnCandidates, 1, 64);
	const int32 MaxAttempts = Wanted * 4;
	const float Tolerance = FMath::Clamp(SpawnDistanceTolerance, 0.f, 0.5f) * TargetDistance;
	const float RepeatRadius = FMath::Clamp(SpawnRepeatRadius, 0.f, 5000.f);
	int32 Attempts = 0;
	int32 ValidCount = 0;

	FVector Farthest = FVector::ZeroVector;
	float FarthestLength = -1.f;

	// Gather until enough candidates are valid (no line of sight, on distance) or the queries run out.
	while (ValidCount < Wanted && Attempts < MaxAttempts)
	{
		++Attempts;

		// Every other query looks ahead of the player, so the forward hemisphere is well sampled.
		const bool bAhead = (Attempts % 2) == 1 && !Forward2D.IsNearlyZero();
		const FVector QueryOrigin = bAhead ? PlayerLocation + Forward2D * TargetDistance : PlayerLocation;
		const float QueryRadius = bAhead ? TargetDistance * 0.75f : TargetDistance * 1.5f;

		FNavLocation Random;
		if (!NavSys->GetRandomPointInNavigableRadius(QueryOrigin, QueryRadius, Random))
		{
			continue;
		}

		// Walk TargetDistance along the path from the player towards the random point: a point at that path distance.
		TArray<FVector> Outbound;
		float OutboundLength = 0.f;
		if (!FindPath(PlayerLocation, Random.Location, Outbound, OutboundLength))
		{
			continue;
		}
		if (OutboundLength > FarthestLength)
		{
			FarthestLength = OutboundLength;
			Farthest = Random.Location;
		}
		if (OutboundLength < TargetDistance - Tolerance)
		{
			continue;
		}

		FVector Location = Outbound.Last();
		float Walked = 0.f;
		for (int32 Index = 1; Index < Outbound.Num(); ++Index)
		{
			const float Segment = FVector::Dist(Outbound[Index - 1], Outbound[Index]);
			if (Walked + Segment >= TargetDistance)
			{
				Location = FMath::Lerp(Outbound[Index - 1], Outbound[Index], (TargetDistance - Walked) / FMath::Max(Segment, 1.f));
				break;
			}
			Walked += Segment;
		}

		FNavLocation OnNav;
		if (NavSys->ProjectPointToNavigation(Location, OnNav, FVector(50.f, 50.f, 200.f)))
		{
			Location = OnNav.Location;
		}

		bool bDuplicate = false;
		for (const FCandidate& Existing : Candidates)
		{
			bDuplicate |= FVector::DistSquared(Existing.Location, Location) < FMath::Square(EntityTuning::CandidateMinSeparation);
		}
		if (bDuplicate)
		{
			continue;
		}

		// The figure's own route back to the player decides its real distance.
		FCandidate Candidate;
		Candidate.Location = Location;
		if (!FindPath(Location, PlayerLocation, Candidate.Path, Candidate.PathLength))
		{
			continue;
		}
		Candidate.bNoLineOfSight = !HasLineOfSight(Eye, Location);
		Candidate.bInTolerance = FMath::Abs(Candidate.PathLength - TargetDistance) <= Tolerance;
		Candidate.bHidden = !IsVisibleFromScans(Location, Scans);
		Candidate.bForward = FVector::DotProduct(Location - PlayerLocation, Forward2D) > 0.f;
		Candidate.bValid = Candidate.bNoLineOfSight && Candidate.bInTolerance;
		Candidate.bNearPrevious = bHasPreviousSpawn && FVector::DistSquared(Location, PreviousSpawnLocation) < FMath::Square(RepeatRadius);
		Candidate.Score = (Candidate.bNoLineOfSight ? 1000.f : 0.f) + (Candidate.bInTolerance ? 100.f : 0.f)
			+ (Candidate.bHidden ? 10.f : 0.f) + (Candidate.bForward ? 1.f : 0.f)
			- FMath::Min(FMath::Abs(Candidate.PathLength - TargetDistance) / FMath::Max(TargetDistance, 1.f), 0.99f);
		ValidCount += Candidate.bValid ? 1 : 0;
		Candidates.Add(MoveTemp(Candidate));
	}

	FEntitySpawnReport Report;
	Report.TargetDistance = TargetDistance;
	Report.Attempts = Attempts;
	Report.Candidates = Candidates.Num();
	Report.ValidCandidates = ValidCount;

	if (!bSpawnRngInitialized || SpawnRngSeed != SpawnSeed)
	{
		SpawnRng.Initialize(SpawnSeed);
		SpawnRngSeed = SpawnSeed;
		bSpawnRngInitialized = true;
	}

	const FCandidate* Best = nullptr;
	if (ValidCount > 0)
	{
		// Weighted random pick among the valid candidates.
		const float HiddenWeight = FMath::Clamp(SpawnHiddenWeight, 1.f, 100.f);
		const float ForwardWeight = FMath::Clamp(SpawnForwardWeight, 1.f, 100.f);
		const float RepeatWeight = FMath::Clamp(SpawnRepeatWeight, 0.f, 1.f);

		int32 NearPreviousCount = 0;
		for (const FCandidate& Candidate : Candidates)
		{
			NearPreviousCount += (Candidate.bValid && Candidate.bNearPrevious) ? 1 : 0;
		}
		Report.ValidNearPreviousSpawn = NearPreviousCount;
		Report.bOnlyNearPreviousSpawn = NearPreviousCount == ValidCount;

		TArray<float, TInlineAllocator<64>> Weights;
		Weights.SetNumZeroed(Candidates.Num());
		float Total = 0.f;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FCandidate& Candidate = Candidates[Index];
			if (!Candidate.bValid)
			{
				continue;
			}
			float Weight = (Candidate.bHidden ? HiddenWeight : 1.f) * (Candidate.bForward ? ForwardWeight : 1.f);
			if (Candidate.bNearPrevious && !Report.bOnlyNearPreviousSpawn)
			{
				Weight *= RepeatWeight;
			}
			Weights[Index] = Weight;
			Total += Weight;
		}

		const float Draw = SpawnRng.FRand() * Total;
		float Accumulated = 0.f;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Weights[Index] <= 0.f)
			{
				continue;
			}
			Best = &Candidates[Index];
			Report.PickWeight = Weights[Index];
			Accumulated += Weights[Index];
			if (Draw < Accumulated)
			{
				break;
			}
		}
		Report.TotalWeight = Total;
		Report.bRandomPick = Best != nullptr;

		if (Report.bOnlyNearPreviousSpawn && bHasPreviousSpawn)
		{
			UE_LOG(LogEntity, Log, TEXT("%s: every valid candidate (%d) is within %.0f cm of the previous spawn %s; repeat weight ignored."),
				*GetName(), ValidCount, RepeatRadius, *PreviousSpawnLocation.ToString());
		}
	}
	else
	{
		// No valid candidate: Brief 3 behaviour, the best score.
		for (const FCandidate& Candidate : Candidates)
		{
			if (!Best || Candidate.Score > Best->Score)
			{
				Best = &Candidate;
			}
		}
		UE_LOG(LogEntity, Warning, TEXT("%s: no valid spawn candidate (no line of sight and within %.0f%% of %.1f cm) among %d; best score used."),
			*GetName(), FMath::Clamp(SpawnDistanceTolerance, 0.f, 0.5f) * 100.f, TargetDistance, Candidates.Num());
	}

	FCandidate FallbackCandidate;
	if (!Best)
	{
		// Nothing far enough: the farthest reachable point seen.
		if (FarthestLength < 0.f || !FindPath(Farthest, PlayerLocation, FallbackCandidate.Path, FallbackCandidate.PathLength))
		{
			UE_LOG(LogEntity, Warning, TEXT("%s: cannot appear (%s): no reachable navmesh point found in %d queries."), *GetName(), Why, Attempts);
			return false;
		}
		FallbackCandidate.Location = Farthest;
		FallbackCandidate.bNoLineOfSight = !HasLineOfSight(Eye, Farthest);
		FallbackCandidate.bInTolerance = FMath::Abs(FallbackCandidate.PathLength - TargetDistance) <= Tolerance;
		FallbackCandidate.bHidden = !IsVisibleFromScans(Farthest, Scans);
		FallbackCandidate.bForward = FVector::DotProduct(Farthest - PlayerLocation, Forward2D) > 0.f;
		Best = &FallbackCandidate;
		Report.bFallback = true;
	}

	Report.Location = Best->Location;
	Report.PathDistance = Best->PathLength;
	Report.bNoLineOfSight = Best->bNoLineOfSight;
	Report.bDistanceInTolerance = Best->bInTolerance;
	Report.bHiddenFromScans = Best->bHidden;
	Report.bInForwardHemisphere = Best->bForward;
	Report.DistanceFromPreviousSpawn = bHasPreviousSpawn ? static_cast<float>(FVector::Dist(Best->Location, PreviousSpawnLocation)) : -1.f;
	for (const FCandidate& Candidate : Candidates)
	{
		Report.NoLineOfSightCandidates += Candidate.bNoLineOfSight ? 1 : 0;
	}
	LastSpawnReport = Report;
	bHasPreviousSpawn = true;
	PreviousSpawnLocation = Best->Location;

	FigureLocation = Best->Location;
	PathPoints = Best->Path;
	PathPoints[0] = FigureLocation;
	FigureYaw = (PathPoints.Num() >= 2 ? (PathPoints[1] - PathPoints[0]) : (PlayerLocation - FigureLocation)).Rotation().Yaw;
	LastPathRefreshTime = GetRunSeconds();
	CachedPathDistanceFrame = MAX_uint64;
	bManifested = true;
	SetActorLocation(FigureLocation);
	SetActorTickEnabled(true);

	int32 NoLineOfSightCount = 0;
	int32 InToleranceCount = 0;
	int32 HiddenCount = 0;
	int32 ForwardCount = 0;
	for (const FCandidate& Candidate : Candidates)
	{
		NoLineOfSightCount += Candidate.bNoLineOfSight ? 1 : 0;
		InToleranceCount += Candidate.bInTolerance ? 1 : 0;
		HiddenCount += Candidate.bHidden ? 1 : 0;
		ForwardCount += Candidate.bForward ? 1 : 0;
	}
	UE_LOG(LogEntity, Log,
		TEXT("%s: appears (%s) at %s, path distance %.1f cm (target %.1f). %d queries, %d candidates (no LOS %d, on distance %d, hidden from %d scans %d, forward %d, valid %d, valid near previous %d). Chosen: %s weight %.2f/%.2f, noLOS=%d onDistance=%d hiddenFromScans=%d forward=%d, %.0f cm from previous spawn%s."),
		*GetName(), Why, *FigureLocation.ToString(), Report.PathDistance, TargetDistance, Attempts, Candidates.Num(),
		NoLineOfSightCount, InToleranceCount, Scans.Num(), HiddenCount, ForwardCount, Report.ValidCandidates, Report.ValidNearPreviousSpawn,
		Report.bRandomPick ? TEXT("random") : TEXT("best score"), Report.PickWeight, Report.TotalWeight,
		Report.bNoLineOfSight ? 1 : 0, Report.bDistanceInTolerance ? 1 : 0, Report.bHiddenFromScans ? 1 : 0,
		Report.bInForwardHemisphere ? 1 : 0, Report.DistanceFromPreviousSpawn,
		Report.bFallback ? TEXT(" (fallback: farthest reachable point)") : TEXT(""));

	OnSpawned.Broadcast(FigureLocation, !Report.bNoLineOfSight);
	return true;
}

void AEntity::Unmanifest()
{
	bManifested = false;
	bHoldFigure = false;
	PathPoints.Reset();
	CachedPathDistanceFrame = MAX_uint64;
	SetActorTickEnabled(false);
}

void AEntity::RecordExit(EStopExitReason Reason)
{
	const float Initial = EntityTuning::Initial(InitialSpawnDistance);
	const float Min = FMath::Clamp(MinSpawnDistance, 0.f, Initial);

	// Every reason but a voluntary Resume is a contact: the next Stop starts at the minimum distance.
	if (Reason != EStopExitReason::Resumed)
	{
		LastExitPathDistance = 0.f;
		NextSpawnDistance = Min;
		ProximityAtExit = 1.f;
	}
	else
	{
		float Distance = GetPathDistanceToPlayer();
		if (Distance < 0.f)
		{
			Distance = GetRemainingPathLength();
		}
		LastExitPathDistance = Distance;
		NextSpawnDistance = FMath::Clamp(Distance - FMath::Max(ApproachPerExit, 0.f), Min, Initial);
		ProximityAtExit = FMath::Clamp(1.f - Distance / Initial, 0.f, 1.f);
	}
	UE_LOG(LogEntity, Log, TEXT("%s: stop exited (%s) at path distance %.1f cm: proximity %.3f, next spawn distance %.1f cm."),
		*GetName(), LexToString(Reason), LastExitPathDistance, ProximityAtExit, NextSpawnDistance);
}

// --- Per frame (Stopped only) ---

void AEntity::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Neither the movement nor the contact check runs while the pause menu freezes the run.
	if (!bManifested || !TimeState || !TimeState->IsStopped() || TimeState->IsRunFrozen())
	{
		return;
	}

	// A figure held by PlaceFigure stands still for the rest of the Stop.
	if (!bHoldFigure)
	{
		AdvanceAlongPath(EntityTuning::Speed(Speed) * DeltaSeconds);
		SetActorLocation(FigureLocation);

		// Between beats the player can move past the end of the path: chase the new position instead of idling.
		if (PathPoints.Num() < 2 && GetRunSeconds() - LastPathRefreshTime >= EntityTuning::ExhaustedPathRefreshInterval)
		{
			RefreshPath();
		}
	}

	CheckContact();

#if ENABLE_DRAW_DEBUG
	if (bDebugDraw && bManifested)
	{
		DrawDebug();
	}
#endif
}

double AEntity::GetRunSeconds() const
{
	if (TimeState)
	{
		return TimeState->GetRunSeconds();
	}
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void AEntity::CheckContact()
{
	const APawn* Pawn = GetPlayerPawn();
	if (!Pawn || !bManifested || (TimeState && TimeState->IsRunFrozen()))
	{
		return;
	}

	const float Distance = FVector::Dist(FigureLocation, Pawn->GetNavAgentLocation());
	if (Distance > EntityTuning::Contact(ContactRadius))
	{
		return;
	}

	const EStopExitReason Reason = GetExitReasonForContact(ContactResponse);
	UE_LOG(LogEntity, Log, TEXT("%s: contact at %.1f cm, %.3f s into the stop: response %s, exit %s."), *GetName(), Distance,
		TimeState ? TimeState->GetCurrentStopDuration() : 0.f, LexToString(ContactResponse), LexToString(Reason));
	bContactThisStop = true;
	OnContact.Broadcast();

	// Every response resumes time, which unmanifests the entity through HandleTimeStateChanged.
	if (UScanDirectorComponent* Current = Director.Get(); Current && bManifested)
	{
		Current->ForceExit(Reason);
	}
}

// --- Points ---

FVector AEntity::GetFigureGround() const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EntityGround), false, this);
	if (const APawn* Pawn = GetPlayerPawn())
	{
		Params.AddIgnoredActor(Pawn);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, FigureLocation + FVector(0.f, 0.f, 60.f), FigureLocation - FVector(0.f, 0.f, 120.f), ECC_Visibility, Params))
	{
		return Hit.ImpactPoint;
	}
	return FigureLocation;
}

void AEntity::GatherLidarPoints_Implementation(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints)
{
	LastGatheredFigurePoints = 0;
	LastGatheredTrailPoints = 0;
	LastGatheredWallPoints = 0;
	if (!bManifested || !TimeState || !TimeState->IsStopped())
	{
		// A scan without the figure is not a clear sighting: the row starts again.
		SightingStreak = 0;
		// Brief 30: the Stop's walls stand even when the figure failed to appear.
		GatherWallPoints(Context, OutPoints);
		return;
	}

	// Figure: the act's form (Brief 29), or the procedural figure.
	RefreshFigureShape();
	TArray<FVector> Figure;
	BuildFigurePoints(Figure);
	const int32 FirstFigure = OutPoints.Num();
	OutPoints.Reserve(OutPoints.Num() + Figure.Num() + 64);
	for (const FVector& Point : Figure)
	{
		OutPoints.Emplace(Point);
	}
	LastGatheredFigurePoints = OutPoints.Num() - FirstFigure;

	// Trail: the path ahead of the figure, jittered anew for every scan.
	const float Spacing = FMath::Clamp(TrailPointSpacing, 5.f, 500.f);
	const float Reach = FMath::Clamp(TrailReach, 0.f, 5000.f);
	const float Lateral = FMath::Clamp(TrailLateralJitter, 0.f, 500.f);
	const float Vertical = FMath::Clamp(TrailVerticalJitter, 0.f, 500.f);
	FRandomStream Rng(HashCombineFast(GetTypeHash(Context.ScanIndex), GetTypeHash(GetUniqueID())));

	const int32 FirstTrail = OutPoints.Num();
	float Along = Spacing; // the first trail point sits one spacing ahead of the feet
	float SegmentStart = 0.f;
	for (int32 Index = 1; Index < PathPoints.Num() && Along <= Reach; ++Index)
	{
		const FVector A = PathPoints[Index - 1];
		const FVector B = PathPoints[Index];
		const float Length = FVector::Dist(A, B);
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector Direction = (B - A) / Length;
		const FVector Side = FVector(-Direction.Y, Direction.X, 0.f).GetSafeNormal();
		while (Along <= SegmentStart + Length && Along <= Reach)
		{
			const float Jittered = FMath::Clamp(Along + Rng.FRandRange(-0.5f, 0.5f) * Spacing - SegmentStart, 0.f, Length);
			const FVector OnPath = A + Direction * Jittered;
			// Brief 47: the trail stands still (no tremor, no pulse); only the figure's points ask the lidar for motion.
			OutPoints.Emplace(OnPath + Side * Rng.FRandRange(-Lateral, Lateral) + FVector(0.f, 0.f, Rng.FRandRange(0.f, Vertical)), 1.f, 0.f);
			Along += Spacing;
		}
		SegmentStart += Length;
	}
	LastGatheredTrailPoints = OutPoints.Num() - FirstTrail;

	// Brief 30: the walls' points, after the figure's and the trail's: no part of the figure, so the check below never sees them.
	GatherWallPoints(Context, OutPoints);

	// Until it is found: did this scan keep more than ClearSightingShare of the figure (never the trail)? ClearSightingScans
	// such scans in a row make a clear sighting, and the Stop has it.
	if (!bSightedThisStop && !IsFound())
	{
		const double Start = FPlatformTime::Seconds();
		const int32 FigureCount = LastGatheredFigurePoints;
		const int32 Needed = FMath::FloorToInt32(EntityTuning::SightingShare(ClearSightingShare) * FigureCount);
		int32 Kept = 0;
		int32 Traces = 0;
		bLastKeptCheckResult = KeepsMoreThan(Context, OutPoints, FirstFigure, FigureCount, Needed, bMeasureFullShare, Kept, Traces);
		LastKeptCheckMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		LastKeptCheckScan = Context.ScanIndex;
		LastKeptCheckTraces = Traces;
		LastKeptCheckKept = Kept;
		LastKeptCheckFigurePoints = FigureCount;
		SightingStreak = bLastKeptCheckResult ? SightingStreak + 1 : 0;
		bSightedThisStop = SightingStreak >= EntityTuning::SightingScans(ClearSightingScans);
		UE_LOG(LogEntity, Log, TEXT("%s: scan %d keeps %s%d of the figure's %d points (more than %d needed: %s; %d traces, %.3f ms); %d in a row%s."), *GetName(),
			Context.ScanIndex, bMeasureFullShare ? TEXT("") : TEXT("at least "), Kept, FigureCount, Needed, bLastKeptCheckResult ? TEXT("yes") : TEXT("no"), Traces,
			LastKeptCheckMs, SightingStreak, bSightedThisStop ? TEXT(": a clear sighting") : TEXT(""));
	}
}

bool AEntity::KeepsMoreThan(const FLidarScanContext& Context, const TArray<FLidarInjectedPoint>& Points, int32 First, int32 Count, int32 Needed, bool bExact,
	int32& OutKept, int32& OutTraces) const
{
	OutKept = 0;
	OutTraces = 0;
	const UWorld* World = GetWorld();
	const int32 Begin = FMath::Max(First, 0);
	const int32 End = FMath::Min(Begin + FMath::Max(Count, 0), Points.Num());
	if (!World || End <= Begin)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EntityScanKept), false, this);
	if (const APawn* Pawn = GetPlayerPawn())
	{
		Params.AddIgnoredActor(Pawn);
	}
	const float Tolerance = FMath::Clamp(OcclusionTolerance, 0.f, 200.f);
	for (int32 Index = Begin; Index < End; ++Index)
	{
		// Certain either way: more than Needed kept already, or too few points left to get there.
		if (!bExact && (OutKept > Needed || OutKept + (End - Index) <= Needed))
		{
			break;
		}
		const FVector& Point = Points[Index].WorldPosition;
		double Depth = 0.0;
		// The lidar drops what lies outside the capture's coverage (bOccludeByCapture); the rest is depth tested.
		if (!Context.IsInCoverage(Point, Depth))
		{
			continue;
		}
		++OutTraces;
		FHitResult Hit;
		const float Length = static_cast<float>(FVector::Dist(Context.CaptureOrigin, Point));
		if (!World->LineTraceSingleByChannel(Hit, Context.CaptureOrigin, Point, ECC_Visibility, Params) || Hit.Distance >= Length - Tolerance)
		{
			++OutKept;
		}
	}
	return OutKept > Needed;
}

// --- Form (Brief 29) ---

void AEntity::SetSessionForm(TOptional<EEntityForm> Form)
{
	SessionForm = Form;
}

TOptional<EEntityForm> AEntity::GetSessionForm()
{
	return SessionForm;
}

EEntityForm AEntity::GetRequestedForm() const
{
	if (SessionForm.IsSet())
	{
		return SessionForm.GetValue();
	}
	return UTheLastShiftSettings::Get()->GetEntityFormOfAct(URunSubsystem::GetActIndexOf(GetWorld()));
}

void AEntity::RefreshFigureShape()
{
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	const EEntityForm Requested = GetRequestedForm();
	const bool bModelledOn = Settings->EntityForms.bModelledFigure;
	const FEntityFormTuning* Tuning = bModelledOn ? Settings->FindEntityForm(Requested) : nullptr;
	const FSoftObjectPath MeshPath = Tuning ? Tuning->Mesh.ToSoftObjectPath() : FSoftObjectPath();
	const int32 ModelledCount = Tuning ? FMath::Clamp(Tuning->PointCount, 10, 4000) : 0;
	const int32 ProceduralCount = FMath::Clamp(FigurePointCount, 10, 2000);
	if (bShapeBuilt && Requested == ShapeRequestedForm && bModelledOn == bShapeModelledOn && MeshPath == ShapeMeshPath
		&& ModelledCount == FigureShapeCount && ProceduralCount == FigureShapeProceduralCount && FigureSeed == FigureShapeSeed)
	{
		return;
	}

	bShapeBuilt = true;
	ShapeRequestedForm = Requested;
	bShapeModelledOn = bModelledOn;
	ShapeMeshPath = MeshPath;
	FigureShapeCount = ModelledCount;
	FigureShapeProceduralCount = ProceduralCount;
	FigureShapeSeed = FigureSeed;
	ShapeMesh = nullptr;
	ShapeForm = EEntityForm::Procedural;

	if (Tuning)
	{
		const double Start = FPlatformTime::Seconds();
		UStaticMesh* Mesh = MeshPath.IsNull() ? nullptr : Tuning->Mesh.LoadSynchronous();
		if (Mesh && FEntityFigureShape::BuildFromMesh(Mesh, ModelledCount, FigureSeed, FigureShape))
		{
			ShapeMesh = Mesh;
			ShapeForm = Requested;
			UE_LOG(LogEntity, Log, TEXT("%s: figure: form %s, %d points spread over %s (seed %d, %.1f ms)."), *GetName(), LexToString(ShapeForm), FigureShape.Num(),
				*Mesh->GetPathName(), FigureSeed, (FPlatformTime::Seconds() - Start) * 1000.0);
			return;
		}
		UE_LOG(LogEntity, Warning, TEXT("%s: figure: form %s has no mesh that can be read (%s): the procedural figure instead."), *GetName(), LexToString(Requested),
			MeshPath.IsNull() ? TEXT("none set") : *MeshPath.ToString());
	}
	else if (Requested != EEntityForm::Procedural)
	{
		UE_LOG(LogEntity, Log, TEXT("%s: figure: %s: the procedural figure."), *GetName(),
			bModelledOn ? *FString::Printf(TEXT("form %s has no entry in EntityForms"), LexToString(Requested)) : TEXT("modelled figures are off"));
	}
	FEntityFigureShape::Build(ProceduralCount, FigureSeed, FigureShape);
	UE_LOG(LogEntity, Log, TEXT("%s: figure: procedural, %d points x %d copies."), *GetName(), FigureShape.Num(), FMath::Clamp(RefractionCount, 1, 8));
}

int32 AEntity::GetFigurePointCountNow() const
{
	return ShapeForm == EEntityForm::Procedural ? FigureShape.Num() * FMath::Clamp(RefractionCount, 1, 8) : FigureShape.Num();
}

float AEntity::GetFormYawOffset() const
{
	const FEntityFormTuning* Tuning = ShapeForm != EEntityForm::Procedural ? UTheLastShiftSettings::Get()->FindEntityForm(ShapeForm) : nullptr;
	return Tuning ? FMath::Clamp(Tuning->YawOffsetDeg, -180.f, 180.f) : 0.f;
}

FTransform AEntity::GetFigureTransform() const
{
	return FTransform(FRotator(0.f, FigureYaw + GetFormYawOffset(), 0.f), GetFigureGround());
}

void AEntity::BuildFigurePoints(TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	const FVector Ground = GetFigureGround();
	if (ShapeForm != EEntityForm::Procedural)
	{
		// The form's mesh as it stands: pivot on the ground under the figure, front (+X) along its direction of travel turned
		// by the form's YawOffsetDeg, upright. Only the points turn: the figure's place and its travel stay as they are.
		const FQuat Rotation = FRotator(0.f, FigureYaw + GetFormYawOffset(), 0.f).Quaternion();
		OutPoints.Reserve(FigureShape.Num());
		for (const FVector& Local : FigureShape)
		{
			OutPoints.Add(Ground + Rotation.RotateVector(Local));
		}
		return;
	}

	const FVector Head = FEntityFigureShape::GetHeadPivot();
	const int32 Copies = FMath::Clamp(RefractionCount, 1, 8);
	const float Step = FMath::Clamp(RefractionAngleStep, 0.f, 180.f);
	const float Tilt = FMath::Clamp(RefractionTilt, 0.f, 60.f);
	OutPoints.Reserve(FigureShape.Num() * Copies);
	for (int32 Copy = 0; Copy < Copies; ++Copy)
	{
		// Lean around the head's local lateral axis, then turn around the vertical axis through the head.
		const FQuat Rotation = FRotator(Tilt, FigureYaw + Copy * Step, 0.f).Quaternion();
		for (const FVector& Local : FigureShape)
		{
			OutPoints.Add(Ground + Head + Rotation.RotateVector(Local - Head));
		}
	}
}

bool AEntity::MeasureFigureKept(const FLidarScanContext& Context, bool bExact, int32& OutKept, int32& OutTraces, int32& OutPoints) const
{
	TArray<FVector> Figure;
	BuildFigurePoints(Figure);
	TArray<FLidarInjectedPoint> Points;
	Points.Reserve(Figure.Num());
	for (const FVector& Point : Figure)
	{
		Points.Emplace(Point);
	}
	OutPoints = Points.Num();
	const int32 Needed = FMath::FloorToInt32(EntityTuning::SightingShare(ClearSightingShare) * OutPoints);
	return KeepsMoreThan(Context, Points, 0, OutPoints, Needed, bExact, OutKept, OutTraces);
}

bool AEntity::PlaceFigure(const FVector& Location, bool bHold)
{
	if (!bManifested)
	{
		UE_LOG(LogEntity, Warning, TEXT("%s: PlaceFigure needs the figure manifested (a Stop with the entity awake)."), *GetName());
		return false;
	}
	FVector At = Location;
	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation OnNav;
		if (NavSys->ProjectPointToNavigation(Location, OnNav, FVector(100.f, 100.f, 300.f)))
		{
			At = OnNav.Location;
		}
	}
	FigureLocation = At;
	PathPoints.Reset();
	PathPoints.Add(At);
	RefreshPath();
	FVector Player = At;
	GetPlayerNavLocation(Player);
	const FVector Ahead = PathPoints.Num() >= 2 ? PathPoints[1] - PathPoints[0] : Player - At;
	if (!FVector(Ahead.X, Ahead.Y, 0.f).IsNearlyZero())
	{
		FigureYaw = Ahead.Rotation().Yaw;
	}
	SetActorLocation(FigureLocation);
	CachedPathDistanceFrame = MAX_uint64;
	bHoldFigure = bHold;
	UE_LOG(LogEntity, Log, TEXT("%s: figure placed at %s (asked %s), yaw %.1f, path distance %.1f cm%s."), *GetName(), *FigureLocation.ToString(), *Location.ToString(),
		FigureYaw, GetPathDistanceToPlayer(), bHold ? TEXT(", held still for the rest of the Stop") : TEXT(""));
	return true;
}

bool AEntity::IsFound() const
{
	const URunSubsystem* Run = URunSubsystem::Get(this);
	return Run && Run->IsEntityFound();
}

FLinearColor AEntity::GetPointColor() const
{
	const FScanColors& Colors = UTheLastShiftSettings::GetScanColors();
	return IsFound() ? Colors.Entity : Colors.Scan;
}

FLidarSourceSettings AEntity::GetLidarSourceSettings_Implementation() const
{
	FLidarSourceSettings Settings;
	// The scan's colour until it is found, its own after (the two owners of ScanColors).
	Settings.Color = GetPointColor();
	Settings.IntensityMultiplier = IntensityMultiplier;
	// Shown by the latest scan only: full until the next scan starts, then gone, so there is one snapshot at most.
	Settings.FadeMode = ELidarFadeMode::UntilNextScan;
	Settings.VanishSeconds = FMath::Clamp(PointVanishSeconds, 0.f, 30.f);
	Settings.bOccludeByCapture = true;
	Settings.OcclusionTolerance = OcclusionTolerance;
	// Once a scan has shown it, it stays visible when geometry comes between it and the player, like the scan's points.
	// The capture still decides whether a scan picks it up (bOccludeByCapture): a scan does not reveal it through a wall.
	Settings.bShowBehindGeometry = true;
	// Brief 47: the figure trembles and pulses (EntityForms, or the session's values); its points ask for all of it
	// (FLidarInjectedPoint::MotionWeight 1), the trail's and the walls' for none (0), so they stand still. One source keeps
	// the order the cap cuts in: the figure first, then the trail, then the walls.
	const FVector4f Motion = GetFigureMotionNow();
	Settings.TremorRadius = Motion.X;
	Settings.TremorRate = Motion.Y;
	Settings.PulseShare = Motion.Z;
	Settings.PulseRate = Motion.W;
	return Settings;
}

namespace
{
	TOptional<FVector4f> GSessionFigureMotion;
}

FVector4f AEntity::GetFigureMotionNow()
{
	if (GSessionFigureMotion.IsSet())
	{
		const FVector4f& M = GSessionFigureMotion.GetValue();
		return FVector4f(FMath::Clamp(M.X, 0.f, 50.f), FMath::Clamp(M.Y, 0.f, 30.f), FMath::Clamp(M.Z, 0.f, 1.f), FMath::Clamp(M.W, 0.f, 30.f));
	}
	const FEntityForms& Forms = UTheLastShiftSettings::Get()->EntityForms;
	return FVector4f(FMath::Clamp(Forms.FigureTremorRadiusCm, 0.f, 50.f), FMath::Clamp(Forms.FigureTremorRate, 0.f, 30.f),
		FMath::Clamp(Forms.FigurePulseShare, 0.f, 1.f), FMath::Clamp(Forms.FigurePulseRate, 0.f, 30.f));
}

void AEntity::SetSessionFigureMotion(TOptional<FVector4f> Motion)
{
	GSessionFigureMotion = Motion;
}

TOptional<FVector4f> AEntity::GetSessionFigureMotion()
{
	return GSessionFigureMotion;
}

void AEntity::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	DrawDebugSphere(World, FigureLocation, 20.f, 12, FColor::Red, false, -1.f, SDPG_Foreground);
	DrawDebugLine(World, FigureLocation, FigureLocation + FVector(0.f, 0.f, FEntityFigureShape::HeadHeight), FColor::Red, false, -1.f, SDPG_Foreground, 2.f);
	DrawDebugCircle(World, FigureLocation, FMath::Clamp(ContactRadius, 10.f, 1000.f), 32, FColor::Orange, false, -1.f, SDPG_Foreground, 1.f, FVector::ForwardVector, FVector::RightVector, false);

	float Walked = 0.f;
	const float Reach = FMath::Clamp(TrailReach, 0.f, 5000.f);
	for (int32 Index = 1; Index < PathPoints.Num(); ++Index)
	{
		const float Segment = FVector::Dist(PathPoints[Index - 1], PathPoints[Index]);
		const FColor Color = Walked < Reach ? FColor::Yellow : FColor(120, 120, 0);
		DrawDebugLine(World, PathPoints[Index - 1], PathPoints[Index], Color, false, -1.f, SDPG_Foreground, 3.f);
		Walked += Segment;
	}
#endif
}

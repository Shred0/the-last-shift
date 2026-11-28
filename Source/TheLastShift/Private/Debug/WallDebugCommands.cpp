// SPDX-License-Identifier: MIT

// Brief 30, the entity's walls. tls.Entity.Walls [0|1] [Count] switches the walls off or on and sets their count for the
// session (no argument: logs the Stop's walls and the area's candidates). tls.WallCheck [Quit] (an area map, PIE): the
// candidate passages by kind (none in or at the exit corridor, at a gate's door or within WallClearOfDevices of a terminal;
// the layout hash and the validation unchanged); no wall while dormant, walls from the start of a Stop once awake, none left
// after a Resume, a contact or a graze, a new choice every Stop; the strategic wall (on the player's path to the nearest
// residue not collected, then, that residue collected, to the next thing not taken; never the same passage two Stops in a
// row); the limits over 200 Stops at seeded poses; the blocking (the player stopped across the whole opening, the entity's
// path, the navmesh and the traces as without it, the player through in Normal time); the points (inside the opening,
// nothing dropped under the cap, the scan's colour then the entity's, the latest scan only); no sighting, contact or graze
// from a wall; the switches; shots of a wall across a corridor's mouth and across a walkway from 6 m, in the entity's own
// colour, to Saved/Screenshots/Brief30. It puts back the run's state, the energy, the residues, the entity's tuning, the
// settings and the session's switches (a residue it collects stays spent). Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Entity/EntityWall.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "LidarPointSource.h"
#include "LidarScanComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Residue/Residue.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace WallDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner on the core ticker ---

	struct FStep
	{
		FString Label;
		TFunction<float()> Do;
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	struct FRun
	{
		FString Name;
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		bool bQuit = false;
		TFunction<void()> Cleanup;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FRun GRun;

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), *GRun.Name, bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), *GRun.Name, *What);
	}

	static double Real() { return FPlatformTime::Seconds(); }

	static FStep Do(const FString& Label, TFunction<float()> Fn)
	{
		FStep Step;
		Step.Label = Label;
		Step.Do = MoveTemp(Fn);
		return Step;
	}

	static FStep WaitFor(const FString& Label, TFunction<bool()> Until, float Timeout, TFunction<float()> Then = nullptr)
	{
		FStep Step;
		Step.Label = Label;
		Step.Until = MoveTemp(Until);
		Step.Timeout = Timeout;
		Step.Do = MoveTemp(Then);
		return Step;
	}

	static void Finish(const TCHAR* Why)
	{
		if (GRun.Cleanup)
		{
			GRun.Cleanup();
			GRun.Cleanup = nullptr;
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls Brief 30 test"));
		}
	}

	static UWorld* W() { return ResolveGameWorld(nullptr); }

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		const double Now = Real();
		if (Now < GRun.WaitUntilReal)
		{
			return true;
		}
		if (GRun.Next >= GRun.Steps.Num())
		{
			Finish(TEXT(""));
			return false;
		}
		const FStep Step = GRun.Steps[GRun.Next];
		if (Step.Until)
		{
			if (GRun.ConditionStartReal < 0.0)
			{
				GRun.ConditionStartReal = Now;
			}
			const bool bMet = W() && Step.Until();
			const bool bTimedOut = Now - GRun.ConditionStartReal > Step.Timeout;
			if (!bMet && !bTimedOut)
			{
				return true;
			}
			Check(bMet, FString::Printf(TEXT("%s (waited %.2f s%s)"), *Step.Label, Now - GRun.ConditionStartReal, bTimedOut && !bMet ? TEXT(", TIMEOUT") : TEXT("")));
			GRun.ConditionStartReal = -1.0;
			if (!bMet)
			{
				Finish(*FString::Printf(TEXT(", aborted at step '%s'"), *Step.Label));
				return false;
			}
		}
		++GRun.Next;
		const float Delay = Step.Do ? Step.Do() : 0.f;
		if (Delay < 0.f)
		{
			Finish(*FString::Printf(TEXT(", aborted at step '%s'"), *Step.Label));
			return false;
		}
		GRun.WaitUntilReal = Real() + Delay;
		return true;
	}

	// --- World access ---

	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static ACharacter* Character() { return Cast<ACharacter>(Pawn()); }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static AAreaGenerator* Gen() { return AAreaGenerator::Find(W()); }
	static const AAreaInfo* Info() { return AAreaInfo::Find(W()); }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }

	static AEntity* FindEntity()
	{
		UWorld* World = W();
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static int32 CountWallActors()
	{
		int32 Count = 0;
		UWorld* World = W();
		for (TActorIterator<AEntityWall> It(World); World && It; ++It)
		{
			++Count;
		}
		return Count;
	}

	static float CapsuleRadius() { const ACharacter* C = Character(); return C ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 34.f; }
	static float CapsuleHalf() { const ACharacter* C = Character(); return C ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f; }

	static FVector EyeOf(const APawn* P)
	{
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() : (P ? P->GetActorLocation() + FVector(0.f, 0.f, 62.f) : FVector::ZeroVector);
	}

	static float EyeAboveFloor()
	{
		const ACharacter* C = Character();
		return C ? CapsuleHalf() + 2.f + static_cast<float>(EyeOf(C).Z - C->GetActorLocation().Z) : 160.f;
	}

	static bool OnNav(const FVector& Point, FVector& Out, const FVector& Extent = FVector(60.f, 60.f, 300.f))
	{
		UWorld* World = W();
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		FNavLocation Location;
		if (!Nav || !Nav->ProjectPointToNavigation(Point, Location, Extent))
		{
			Out = Point;
			return false;
		}
		Out = Location.Location;
		return true;
	}

	static void StandOn(const FVector& Floor, const FVector& LookAt)
	{
		ACharacter* C = Character();
		if (!C)
		{
			return;
		}
		C->SetActorLocation(Floor + FVector(0.f, 0.f, CapsuleHalf() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		C->GetCharacterMovement()->StopMovementImmediately();
		if (APlayerController* Controller = PC())
		{
			Controller->SetControlRotation((LookAt - EyeOf(C)).Rotation());
		}
	}

	static FVector PlayerNav()
	{
		const APawn* P = Pawn();
		FVector Out = P ? P->GetNavAgentLocation() : FVector::ZeroVector;
		OnNav(Out, Out, FVector(100.f, 100.f, 250.f));
		return Out;
	}

	static bool StopNow()
	{
		if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped())
		{
			T->Stop();
		}
		return IsStopped();
	}

	static void ResumeNow()
	{
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->Resume();
		}
	}

	static FString MapShort()
	{
		const UWorld* World = W();
		return World ? FPaths::GetBaseFilename(World->GetOutermost()->GetName()).Replace(TEXT("UEDPIE_0_"), TEXT("")) : FString();
	}

	static float RouteS(const FVector& Location, int32* OutSegment = nullptr)
	{
		const AAreaInfo* I = Info();
		int32 Segment = INDEX_NONE;
		float Offset = 0.f;
		const float S = I ? I->ProjectToRoute(Location, Segment, Offset) : 0.f;
		if (OutSegment)
		{
			*OutSegment = Segment;
		}
		return S;
	}

	static int32 SegmentUnder(const FVector& Location)
	{
		const AAreaInfo* I = Info();
		int32 Segment = INDEX_NONE;
		RouteS(Location, &Segment);
		if (I)
		{
			const int32 Rect = I->FindFloorRect(Location, 30.f);
			if (I->FloorRects.IsValidIndex(Rect))
			{
				Segment = I->FloorRects[Rect].Segment;
			}
		}
		return Segment;
	}

	static bool NavPath(const FVector& From, const FVector& To, TArray<FVector>& Out, float& Length)
	{
		const AEntity* E = FindEntity();
		FVector A, B;
		OnNav(From, A, FVector(120.f, 120.f, 300.f));
		OnNav(To, B, FVector(120.f, 120.f, 300.f));
		return E && E->FindNavPath(A, B, Out, Length);
	}

	/** A floor point of the route at S, on the navmesh. */
	static bool RouteFloor(float S, FVector& Out)
	{
		const AAreaInfo* I = Info();
		int32 Unused = 0;
		return I && OnNav(I->GetRoutePointAtDistance(S, Unused), Out, FVector(60.f, 60.f, 300.f));
	}

	// --- The rules as the test reads them (computed here from the world, not taken from the entity's report) ---

	/** The nearest residue not collected (not regenerating) or terminal still offering a payment ahead of S, else behind. */
	static const AActor* NearestNotTaken(float PlayerS, bool* OutAhead = nullptr, const AActor* Skip = nullptr)
	{
		UWorld* World = W();
		const AActor* Best = nullptr;
		float BestS = 0.f;
		bool bBestAhead = false;
		auto Consider = [&](const AActor* A)
		{
			if (A == Skip)
			{
				return;
			}
			const float S = RouteS(A->GetActorLocation());
			const bool bAhead = S >= PlayerS + 50.f;
			if (!Best || (bAhead && !bBestAhead) || (bAhead == bBestAhead && (bAhead ? S < BestS : S > BestS)))
			{
				Best = A;
				BestS = S;
				bBestAhead = bAhead;
			}
		};
		for (TActorIterator<AResidue> It(World); It; ++It)
		{
			if (!It->bRegenerates && It->GetState() == EResidueState::Available)
			{
				Consider(*It);
			}
		}
		for (TActorIterator<APowerTerminal> It(World); It; ++It)
		{
			if (It->OffersPayment())
			{
				Consider(*It);
			}
		}
		if (OutAhead)
		{
			*OutAhead = bBestAhead;
		}
		return Best;
	}

	/** The player's paths to the regenerating residues whose gate (the next door along the route) is still ahead of the player. */
	static void ProtectedPaths(const FVector& Player, TArray<TArray<FVector>>& Out)
	{
		UWorld* World = W();
		const float PlayerS = RouteS(Player);
		TArray<float> Doors;
		for (TActorIterator<ADoorDevice> It(World); It; ++It)
		{
			Doors.Add(RouteS(It->GetActorLocation()));
		}
		for (TActorIterator<AResidue> It(World); It; ++It)
		{
			if (!It->bRegenerates)
			{
				continue;
			}
			const float S = RouteS(It->GetActorLocation());
			float Gate = -1.f;
			for (const float D : Doors)
			{
				if (D > S && (Gate < 0.f || D < Gate))
				{
					Gate = D;
				}
			}
			TArray<FVector> Path;
			float Length = 0.f;
			if (Gate > PlayerS && NavPath(Player, It->GetActorLocation(), Path, Length))
			{
				Out.Add(MoveTemp(Path));
			}
		}
	}

	/** True when a path out of the player's segment (past one of its joints) crosses none of the walls. */
	static bool WayOutOpen(const FVector& Player, const TArray<AEntityWall*>& Walls, int32& OutWays)
	{
		OutWays = 0;
		const AAreaGenerator* G = Gen();
		const int32 Segment = SegmentUnder(Player);
		if (!G)
		{
			return true;
		}
		bool bAnyOpen = false;
		for (const FAreaPassage& P : G->GetPassages())
		{
			if (!P.IsJoint() || (P.Segment != Segment && P.OtherSegment != Segment))
			{
				continue;
			}
			++OutWays;
			TArray<FVector> Path;
			float Length = 0.f;
			if (!NavPath(Player, P.Centre + P.Along * (P.Segment == Segment ? 150.f : -150.f), Path, Length))
			{
				continue;
			}
			bool bOpen = true;
			for (const AEntityWall* Wall : Walls)
			{
				bOpen &= !AEntity::PathCrossesPassage(Path, Wall->GetPassage(), Wall->GetHeight());
			}
			bAnyOpen |= bOpen;
		}
		return OutWays == 0 || bAnyOpen;
	}

	// --- Shots ---

	enum class EHue : uint8 { Scan, Entity };

	struct FShot
	{
		FString Label;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
	};
	static FShot GShot;
	static bool bShotReady = false;
	static FDelegateHandle GShotHandle;
	/** The folder under Saved/Screenshots the next shots go to (Brief 47's width shots go to Brief47). */
	static FString GShotFolder = TEXT("Brief30");

	/** The hue classes of the Brief 20 tests (8-bit sRGB after tonemapping): the scan's cyan, the entity's red. */
	static bool IsHue(const FColor& C, EHue Hue)
	{
		const int32 R = C.R, G = C.G, B = C.B;
		switch (Hue)
		{
		case EHue::Scan: return B > 6 && G * 10 >= B * 6 && R * 10 < G * 8;
		case EHue::Entity: return R > 12 && R * 10 > G * 25 && R * 10 > B * 25;
		}
		return false;
	}

	static int32 CountHue(const FShot& Shot, EHue Hue)
	{
		int32 Count = 0;
		for (const FColor& C : Shot.Pixels)
		{
			Count += IsHue(C, Hue) ? 1 : 0;
		}
		return Count;
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		GShot.Width = Width;
		GShot.Height = Height;
		GShot.Pixels = Pixels;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), GShotFolder, GShot.Label + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		Note(FString::Printf(TEXT("shot %s saved (%d x %d): %s"), *GShot.Label, Width, Height, *Path));
		bShotReady = true;
	}

	static void AddShot(TArray<FStep>& S, TFunction<FString()> Label, TFunction<void(const FShot&)> Then)
	{
		S.Add(Do(TEXT("shot"), [Label]()
		{
			if (!GShotHandle.IsValid())
			{
				GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
			}
			GShot = FShot();
			GShot.Label = Label();
			bShotReady = false;
			FScreenshotRequest::RequestScreenshot(FString(), false, false);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("shot taken"), []() { return bShotReady; }, 5.f, [Then]()
		{
			if (Then)
			{
				Then(GShot);
			}
			return 0.f;
		}));
	}

	// =====================================================================================================
	// tls.WallCheck
	// =====================================================================================================

	/** A candidate with a floor 6 m off it on one side (in sight of its middle) and floors on both sides near it. */
	struct FWallPose
	{
		bool bValid = false;
		int32 Candidate = INDEX_NONE;
		FAreaPassage Where;
		/** +1: the pose stands on the side Along points to; -1 the other. */
		float Side = 1.f;
		FVector Six = FVector::ZeroVector;
		FVector Near = FVector::ZeroVector;
		FVector Far = FVector::ZeroVector;
		FVector FarAway = FVector::ZeroVector;
		FVector NearStart = FVector::ZeroVector;
	};

	struct FCheck
	{
		int32 Act = 0;
		FString Form;
		bool bSavedFound = false;
		bool bSavedContact = false;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		int32 SavedBanked = 0;
		float SavedSpeed = 66.6667f;
		TOptional<bool> SavedSessionWalls;
		TOptional<int32> SavedSessionCount;
		FEntityForms SavedForms;
		TArray<FActTuning> SavedActs;
		FActTuning SavedTestAct;
		TArray<TPair<TWeakObjectPtr<AResidue>, FVector>> MovedResidues;
		int32 WallCount = 0;
		int32 WallPoints = 0;

		// Criterion 2
		bool bWasDormant = false;
		FVector TimingPose = FVector::ZeroVector;
		bool bTimingPose = false;
		int32 LastSerial = 0;
		int32 PoseTry = 0;

		// Criterion 3
		FVector StrategicPose = FVector::ZeroVector;
		TWeakObjectPtr<AResidue> StrategicResidue;
		TWeakObjectPtr<const AActor> NextTarget;
		int32 FirstStrategic = INDEX_NONE;
		int32 SearchTry = 0;
		bool bStrategicFound = false;

		// Criterion 4
		int32 SeededDone = 0;
		FRandomStream SeededRng;
		int32 SeededFewer = 0;
		int32 SeededWithStrategic = 0;
		int32 SeededRaised[5] = { 0, 0, 0, 0, 0 };
		int32 BadCount = 0;
		int32 BadNear = 0;
		int32 BadProtected = 0;
		int32 BadWayOut = 0;
		int32 BadUnderPlayer = 0;
		int32 BadRepeat = 0;
		int32 PrevStrategic = INDEX_NONE;
		float NearestSeen = TNumericLimits<float>::Max();

		// Criteria 5, 6, 7, 9
		FWallPose Mouth;
		FWallPose Walk;
		TArray<FVector> PathBefore;
		float PathBeforeLength = 0.f;
		FHitResult TraceBefore;
		bool bTraceBeforeHit = false;
		int32 EntityHueFound = 0;
		double WalkStart = 0.0;

		// Brief 47: the strands' width on screen before and after (px), the form whose look the check sets for the session.
		EEntityForm LookForm = EEntityForm::Procedural;
		float WidthBeforePx = 0.f;
		float WidthAfterPx = 0.f;
		float ThicknessAfter = 0.f;
		TOptional<FVector3f> SavedLook;
	};
	static FCheck G;

	/**
	 * Brief 47: the width across its strands of the first wall's points for the newest scan, on screen (px): for each point the
	 * distance from its strand's line in the opening's plane, twice their 90th percentile, seen from the camera at the wall's
	 * centre. Also counts the points outside the opening.
	 */
	static float StrandWidthPx(int32& OutPoints, int32& OutOutside)
	{
		OutPoints = 0;
		OutOutside = 0;
		const AEntity* E = FindEntity();
		const AEntityWall* Wall = E && E->GetWalls().Num() > 0 ? E->GetWalls()[0] : nullptr;
		const ULidarScanComponent* L = Lidar();
		const APlayerController* C = PC();
		if (!Wall || !L || !C)
		{
			return 0.f;
		}
		int32 Newest = INDEX_NONE;
		for (const FLidarScanContext& Context : L->GetAliveScans())
		{
			Newest = FMath::Max(Newest, Context.ScanIndex);
		}
		TArray<FLidarInjectedPoint> Built;
		Wall->BuildPoints(Newest, Built);
		const FAreaPassage& P = Wall->GetPassage();
		TArray<double> Distances;
		int32 Index = 0;
		for (const FEntityWallStrand& Strand : Wall->GetStrands())
		{
			for (int32 I = 0; I < Strand.Points && Index < Built.Num(); ++I, ++Index)
			{
				const FVector D = Built[Index].WorldPosition - P.Centre;
				const FVector2D UV(FVector::DotProduct(D, P.Across), D.Z);
				double Best = TNumericLimits<double>::Max();
				for (int32 K = 0; K <= 400; ++K)
				{
					Best = FMath::Min(Best, FVector2D::Distance(UV, Strand.At(K / 400.0)));
				}
				Distances.Add(Best);
				OutOutside += Wall->IsInsideOpening(Built[Index].WorldPosition, 0.5f) ? 0 : 1;
			}
		}
		OutPoints = Built.Num();
		if (Distances.Num() == 0)
		{
			return 0.f;
		}
		Distances.Sort();
		const double WidthCm = 2.0 * Distances[FMath::Min(Distances.Num() - 1, Distances.Num() * 9 / 10)];
		FVector CamLoc;
		FRotator CamRot;
		C->GetPlayerViewPoint(CamLoc, CamRot);
		const FVector Right = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y);
		const FVector Mid = P.Centre + FVector(0.f, 0.f, Wall->GetHeight() * 0.5f);
		FVector2D A, B;
		if (!C->ProjectWorldLocationToScreen(Mid, A, false) || !C->ProjectWorldLocationToScreen(Mid + Right * WidthCm, B, false))
		{
			return 0.f;
		}
		Note(FString::Printf(TEXT("47: the strands' width: %.1f cm across (twice the 90th percentile of %d points' distance from their line), %.1f px on screen at %.0f cm"),
			WidthCm, Distances.Num(), FVector2D::Distance(A, B), FVector::Dist(CamLoc, Mid)));
		return static_cast<float>(FVector2D::Distance(A, B));
	}

	static void Cleanup()
	{
		for (const TPair<TWeakObjectPtr<AResidue>, FVector>& Moved : G.MovedResidues)
		{
			if (AResidue* Residue = Moved.Key.Get())
			{
				Residue->SetActorLocation(Moved.Value);
			}
		}
		G.MovedResidues.Reset();
		ResumeNow();
		UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
		Settings->EntityForms = G.SavedForms;
		if (G.SavedActs.Num() > 0)
		{
			Settings->ActTuning = G.SavedActs;
			Settings->TestActTuning = G.SavedTestAct;
		}
		AEntity::SetSessionWalls(G.SavedSessionWalls);
		AEntity::SetSessionWallCount(G.SavedSessionCount);
		AEntity::SetSessionWallLook(G.LookForm, G.SavedLook);
		GShotFolder = TEXT("Brief30");
		if (AEntity* E = FindEntity())
		{
			E->Speed = G.SavedSpeed;
			E->ClearWalls(TEXT("tls.WallCheck: end"));
		}
		if (URunSubsystem* R = Run())
		{
			R->SetEntityFound(G.bSavedFound, TEXT("tls.WallCheck: end"));
			R->SetFirstContact(G.bSavedContact, TEXT("tls.WallCheck: end"));
		}
		if (UHomeStateSubsystem* H = Home())
		{
			H->RestoreState(G.SavedBattery, G.SavedHome);
		}
		if (UEnergySubsystem* En = Energy())
		{
			En->RestoreBankedEnergy(G.SavedBanked);
		}
		if (GShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(GShotHandle);
			GShotHandle.Reset();
		}
	}

	static int64 ReferenceHash(int32 Act)
	{
		// The layout hashes of the four areas at their seeds before Brief 30 (tls.BackdropCheck's reference).
		switch (Act)
		{
		case 1: return static_cast<int64>(0x7f1032152054f0a3ull);
		case 2: return static_cast<int64>(0xf2a87c443cf7c500ull);
		case 3: return static_cast<int64>(0xf7db963f34395881ull);
		case 4: return static_cast<int64>(0x6cf097ae1c4679cfull);
		default: return 0;
		}
	}

	static bool InSight(const FVector& Eye, const FVector& Target)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsWallSight), false, FindEntity());
		Params.AddIgnoredActor(Pawn());
		FHitResult Hit;
		return !W()->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility, Params);
	}

	static FWallPose FindPose(bool bWalkway)
	{
		FWallPose Pose;
		AEntity* E = FindEntity();
		if (!E)
		{
			return Pose;
		}
		const TArray<FEntityWallCandidate>& Candidates = E->GetWallCandidates();
		const TArray<EAreaPassageKind> Kinds = bWalkway ? TArray<EAreaPassageKind>{ EAreaPassageKind::Walkway }
			: TArray<EAreaPassageKind>{ EAreaPassageKind::CorridorMouth, EAreaPassageKind::RoomMouth, EAreaPassageKind::GateMouth, EAreaPassageKind::SideMouth };
		const float EyeAbove = EyeAboveFloor();
		for (const EAreaPassageKind Kind : Kinds)
		{
			for (int32 I = 0; I < Candidates.Num(); ++I)
			{
				const FEntityWallCandidate& C = Candidates[I];
				if (C.bExcluded || C.Where.Kind != Kind || C.Where.HalfWidth < 100.f)
				{
					continue;
				}
				for (const float Side : { -1.f, 1.f })
				{
					const FAreaPassage& P = C.Where;
					FVector Six, Near, Far, FarAway, NearStart;
					auto Floor = [&P](float Along, FVector& Out)
					{
						const FVector Want = P.Centre + P.Along * Along;
						return OnNav(Want + FVector(0.f, 0.f, 50.f), Out, FVector(40.f, 40.f, 250.f)) && FVector::Dist2D(Out, Want) < 60.f && FMath::Abs(Out.Z - P.Centre.Z) < 80.f;
					};
					if (!Floor(Side * 600.f, Six) || !Floor(Side * 200.f, NearStart) || !Floor(Side * 300.f, Near) || !Floor(-Side * 300.f, Far))
					{
						continue;
					}
					if (!Floor(-Side * 900.f, FarAway) && !Floor(-Side * 600.f, FarAway))
					{
						FarAway = Far;
					}
					if (!InSight(Six + FVector(0.f, 0.f, EyeAbove), P.Centre + FVector(0.f, 0.f, 150.f)))
					{
						continue;
					}
					Pose.bValid = true;
					Pose.Candidate = I;
					Pose.Where = P;
					Pose.Side = Side;
					Pose.Six = Six;
					Pose.Near = Near;
					Pose.Far = Far;
					Pose.FarAway = FarAway;
					Pose.NearStart = NearStart;
					return Pose;
				}
			}
		}
		return Pose;
	}

	/** Out of every scan's way: behind the player, as far along the route as can be found. */
	static void PutFigureAway()
	{
		AEntity* E = FindEntity();
		if (!E || !E->IsManifested())
		{
			return;
		}
		const APawn* P = Pawn();
		const FVector Back = P ? -P->GetControlRotation().Vector().GetSafeNormal2D() : FVector(-1.f, 0.f, 0.f);
		E->PlaceFigure(PlayerNav() + Back * 1500.f, true);
	}

	static void StandAtSix(const FWallPose& Pose)
	{
		StandOn(Pose.Six, Pose.Where.Centre + FVector(0.f, 0.f, 180.f));
	}

	/** Stop with the entity manifested, the figure out of the way, the Stop's walls replaced by the given candidates. */
	static void AddWallStop(TArray<FStep>& S, const FString& Label, TFunction<const FWallPose&()> Pose, int32 Copies)
	{
		S.Add(Do(Label + TEXT(": Stop at 6 m"), [Pose]()
		{
			ResumeNow();
			StandAtSix(Pose());
			StopNow();
			return 0.1f;
		}));
		S.Add(WaitFor(Label + TEXT(": Stopped, the entity manifested"), []() { const AEntity* E = FindEntity(); return IsStopped() && E && E->IsManifested(); }, 4.f, [Label, Pose, Copies]()
		{
			AEntity* E = FindEntity();
			StandAtSix(Pose());
			PutFigureAway();
			StandAtSix(Pose());
			TArray<int32> At;
			for (int32 K = 0; K < Copies; ++K)
			{
				At.Add(Pose().Candidate);
			}
			Check(E->RaiseWallsAtForTest(At), FString::Printf(TEXT("%s: %d wall(s) raised at candidate %d (%s)"), *Label, Copies, Pose().Candidate, LexToString(Pose().Where.Kind)));
			return 0.f;
		}));
	}

	static void StartCheck(bool bQuit)
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.Name = TEXT("tls.WallCheck");
		GRun.bQuit = bQuit;
		G = FCheck();
		G.SeededRng.Initialize(30);
		TArray<FStep>& S = GRun.Steps;

		// --- Setup ---
		S.Add(WaitFor(TEXT("setup: an area map, ready, with the player's lidar and an entity"), []()
		{
			const UWorld* World = W();
			const AAreaGenerator* Generator = World ? AAreaGenerator::Find(World) : nullptr;
			const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			return Generator && Generator->IsReady() && (!Flow || !Flow->IsTransitioning()) && Lidar() && Pawn() && FindEntity() && Run() && Home() && Energy() && AAreaInfo::Find(World);
		}, 30.f, []()
		{
			AEntity* E = FindEntity();
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			G.Act = URunSubsystem::GetActIndexOf(W());
			G.bSavedFound = Run()->IsEntityFound();
			G.bSavedContact = Run()->HasFirstContact();
			G.SavedBattery = Home()->GetBatteryFraction();
			G.SavedHome = Home()->GetHomeSecondsElapsed();
			G.SavedBanked = Energy()->GetBankedEnergy();
			G.SavedSpeed = E->Speed;
			G.SavedSessionWalls = AEntity::GetSessionWalls();
			G.SavedSessionCount = AEntity::GetSessionWallCount();
			G.SavedForms = Settings->EntityForms;
			G.SavedActs = Settings->ActTuning;
			G.SavedTestAct = Settings->TestActTuning;
			GRun.Cleanup = &Cleanup;
			AEntity::SetSessionWalls(TOptional<bool>());
			AEntity::SetSessionWallCount(TOptional<int32>());
			Run()->SetEntityFound(false, TEXT("tls.WallCheck: start"));
			Run()->SetFirstContact(false, TEXT("tls.WallCheck: start"));
			E->Speed = 0.f;
			E->ResetSpawnHistory();
			G.WallCount = E->GetWallCountNow();
			G.WallPoints = E->GetWallPointsNow();
			G.Form = LexToString(E->GetRequestedForm());
			const FEntityFormTuning* Form = Settings->FindEntityForm(E->GetRequestedForm());
			Note(FString::Printf(TEXT("map %s, act %d, form %s: walls %s, WallCount %d, WallPoints %d, strands %d, bow %.2f, thickness %.1f cm; range %.0f m, min %.0f m, clear of devices %.0f cm, block depth %.0f cm"),
				*MapShort(), G.Act, *G.Form, Settings->EntityForms.bEntityWalls ? TEXT("on") : TEXT("off"), G.WallCount, G.WallPoints, Form ? Form->WallStrands : -1, Form ? Form->WallBow : -1.f,
				Form ? Form->WallThickness : -1.f, Settings->EntityForms.WallRangeM, Settings->EntityForms.WallMinDistanceM, Settings->EntityForms.WallClearOfDevices, Settings->EntityForms.WallBlockDepth));
			Check(E->AreWallsOn() && G.WallCount == (G.Act == 4 ? 3 : 2) && G.WallPoints == 250, FString::Printf(TEXT("walls on, WallCount %d (act %d), WallPoints %d"), G.WallCount, G.Act, G.WallPoints));
			return 0.f;
		}));

		// --- Criterion 1: the candidates ---
		S.Add(Do(TEXT("1 candidates"), []()
		{
			AEntity* E = FindEntity();
			const AAreaGenerator* Gn = Gen();
			const AAreaInfo* I = Info();
			const TArray<FAreaPassage>& Passages = Gn->GetPassages();
			const TArray<FEntityWallCandidate>& Candidates = E->GetWallCandidates();
			int32 ByKind[5] = { 0, 0, 0, 0, 0 };
			int32 UsableByKind[5] = { 0, 0, 0, 0, 0 };
			TMap<FString, int32> Why;
			int32 Usable = 0;
			int32 BadExit = 0;
			int32 BadDoor = 0;
			int32 BadTerminal = 0;
			float NearestDoor = TNumericLimits<float>::Max();
			float NearestTerminal = TNumericLimits<float>::Max();
			auto Dist = [](const FVector& Point, const FAreaPassage& P)
			{
				const FVector A = P.Centre - P.Across * P.HalfWidth;
				const FVector B = P.Centre + P.Across * P.HalfWidth;
				return static_cast<float>(FMath::PointDistToSegment(FVector(Point.X, Point.Y, 0.0), FVector(A.X, A.Y, 0.0), FVector(B.X, B.Y, 0.0)));
			};
			for (const FEntityWallCandidate& C : Candidates)
			{
				const int32 K = FMath::Clamp(static_cast<int32>(C.Where.Kind), 0, 4);
				++ByKind[K];
				if (C.bExcluded)
				{
					Why.FindOrAdd(C.Why) += 1;
					continue;
				}
				++Usable;
				++UsableByKind[K];
				auto IsExit = [I](int32 Seg) { return I->Segments.IsValidIndex(Seg) && I->Segments[Seg].bExitCorridor; };
				BadExit += (IsExit(C.Where.Segment) || IsExit(C.Where.OtherSegment) || IsExit(SegmentUnder(C.Where.Centre))) ? 1 : 0;
				for (TActorIterator<ADoorDevice> It(W()); It; ++It)
				{
					const float D = Dist(It->GetActorLocation(), C.Where);
					NearestDoor = FMath::Min(NearestDoor, D);
					BadDoor += D < 200.f ? 1 : 0;
				}
				for (TActorIterator<APowerTerminal> It(W()); It; ++It)
				{
					const float D = Dist(It->GetActorLocation(), C.Where);
					NearestTerminal = FMath::Min(NearestTerminal, D);
					BadTerminal += D < 200.f ? 1 : 0;
				}
			}
			FString Excluded;
			for (const TPair<FString, int32>& Pair : Why)
			{
				Excluded += FString::Printf(TEXT("%s%s %d"), Excluded.IsEmpty() ? TEXT("") : TEXT(", "), *Pair.Key, Pair.Value);
			}
			Note(FString::Printf(TEXT("CANDIDATES %s: %d passages (corridor mouths %d, room mouths %d, gate mouths %d, side mouths %d, walkways %d); usable %d (%d, %d, %d, %d, %d); excluded: %s"),
				*MapShort(), Candidates.Num(), ByKind[0], ByKind[1], ByKind[2], ByKind[3], ByKind[4], Usable, UsableByKind[0], UsableByKind[1], UsableByKind[2], UsableByKind[3], UsableByKind[4],
				Excluded.IsEmpty() ? TEXT("none") : *Excluded));
			Check(Candidates.Num() == Passages.Num() && Usable > 0, FString::Printf(TEXT("1: every passage of the plan is a candidate or excluded with its reason (%d passages, %d usable)"), Passages.Num(), Usable));
			Check(BadExit == 0, FString::Printf(TEXT("1: no usable candidate in the exit corridor or at its mouth (%d)"), BadExit));
			Check(BadDoor == 0, FString::Printf(TEXT("1: no usable candidate at a gate's door (%d within 2 m; nearest %.0f cm)"), BadDoor, NearestDoor));
			Check(BadTerminal == 0, FString::Printf(TEXT("1: no usable candidate within 2 m of a terminal (%d; nearest %.0f cm)"), BadTerminal, NearestTerminal));
			const int64 Reference = ReferenceHash(G.Act);
			Check(Reference == 0 || Gn->GetLayoutHash() == Reference, FString::Printf(TEXT("1: the layout hash unchanged (%016llx, reference %016llx)"), static_cast<uint64>(Gn->GetLayoutHash()), static_cast<uint64>(Reference)));
			Check(Gn->bLastValidationPassed && Gn->LastSeedsTried == 1, FString::Printf(TEXT("1: the validation passed at the configured seed (%s, seeds tried %d)"),
				Gn->bLastValidationPassed ? TEXT("passed") : TEXT("FAILED"), Gn->LastSeedsTried));
			return 0.f;
		}));

		// --- Criterion 2: timing ---
		S.Add(Do(TEXT("2 dormant"), []()
		{
			AEntity* E = FindEntity();
			G.bWasDormant = E->GetState() == EEntityState::Dormant;
			if (!G.bWasDormant)
			{
				Note(TEXT("2: the entity is already awake (not a fresh session): the dormant case is not measured"));
				return 0.f;
			}
			StopNow();
			Check(IsStopped() && E->GetWalls().Num() == 0 && CountWallActors() == 0 && E->GetLastWallReport().Raised == 0,
				FString::Printf(TEXT("2: no wall while dormant (walls %d, actors %d, report '%s')"), E->GetWalls().Num(), CountWallActors(), *E->GetLastWallReport().StrategicWhy));
			ResumeNow();
			return 0.1f;
		}));
		S.Add(WaitFor(TEXT("2: a pose where a Stop raises walls"), []()
		{
			AEntity* E = FindEntity();
			if (E->GetState() == EEntityState::Dormant)
			{
				E->Awaken();
			}
			const float Length = Gen()->GetRouteLengthBeforeWayOut();
			for (int32 K = 0; K < 4; ++K)
			{
				const float At = 1500.f + G.PoseTry * 700.f;
				++G.PoseTry;
				if (At > Length)
				{
					return false;
				}
				FVector Floor;
				if (!RouteFloor(At, Floor))
				{
					continue;
				}
				StandOn(Floor, Floor + Info()->GetRouteDirectionAtDistance(At) * 500.f + FVector(0.f, 0.f, 150.f));
				StopNow();
				const int32 Walls = E->GetWalls().Num();
				const int32 Serial = E->GetStopSerial();
				const FEntityWallReport Report = E->GetLastWallReport();
				if (Walls > 0)
				{
					G.TimingPose = Floor;
					G.bTimingPose = true;
					G.LastSerial = Serial;
					Check(Report.StopSerial == Serial && Walls == CountWallActors(), FString::Printf(TEXT("2: awake, the Stop raises %d wall(s) from its start (route %.0f m; report of Stop %d, Stop %d)"),
						Walls, At / 100.f, Report.StopSerial, Serial));
					ResumeNow();
					Check(E->GetWalls().Num() == 0 && CountWallActors() == 0, FString::Printf(TEXT("2: a Resume leaves no wall (walls %d, actors %d)"), E->GetWalls().Num(), CountWallActors()));
					return true;
				}
				ResumeNow();
			}
			return false;
		}, 60.f));
		S.Add(Do(TEXT("2 a new choice at the next Stop"), []()
		{
			AEntity* E = FindEntity();
			StandOn(G.TimingPose, G.TimingPose + FVector(500.f, 0.f, 150.f));
			StopNow();
			const FEntityWallReport& Report = E->GetLastWallReport();
			Check(E->GetStopSerial() == G.LastSerial + 1 && Report.StopSerial == E->GetStopSerial(),
				FString::Printf(TEXT("2: the next Stop chooses again (Stop %d after %d, its report of Stop %d, %d wall(s))"), E->GetStopSerial(), G.LastSerial, Report.StopSerial, Report.Raised));
			return 0.1f;
		}));
		S.Add(WaitFor(TEXT("2 contact: the entity manifested"), []() { const AEntity* E = FindEntity(); return E->IsManifested(); }, 3.f, []()
		{
			AEntity* E = FindEntity();
			Note(FString::Printf(TEXT("2: %d wall(s) stand before the contact"), E->GetWalls().Num()));
			E->PlaceFigure(PlayerNav() + FVector(40.f, 0.f, 0.f), true);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("2 contact ends the Stop"), []() { return !IsStopped(); }, 3.f, []()
		{
			AEntity* E = FindEntity();
			Check(E->WasContactThisStop() && E->GetWalls().Num() == 0 && CountWallActors() == 0,
				FString::Printf(TEXT("2: a contact leaves no wall (contact %d, walls %d, actors %d)"), E->WasContactThisStop() ? 1 : 0, E->GetWalls().Num(), CountWallActors()));
			return 0.3f;
		}));
		S.Add(Do(TEXT("2 graze: Stop"), []()
		{
			StandOn(G.TimingPose, G.TimingPose + FVector(500.f, 0.f, 150.f));
			StopNow();
			return 0.1f;
		}));
		S.Add(WaitFor(TEXT("2 graze: the entity manifested"), []() { const AEntity* E = FindEntity(); return E->IsManifested(); }, 3.f, []()
		{
			AEntity* E = FindEntity();
			const float Here = RouteS(PlayerNav());
			bool bPlaced = false;
			for (const float Offset : { 300.f, -300.f, 250.f, -250.f, 350.f, -350.f })
			{
				FVector Floor;
				if (RouteFloor(Here + Offset, Floor))
				{
					E->PlaceFigure(Floor, true);
					const float Distance = E->GetPathDistanceToPlayer();
					if (Distance > E->ContactRadius + 30.f && Distance < E->GrazeRadius - 30.f)
					{
						bPlaced = true;
						break;
					}
				}
			}
			Note(FString::Printf(TEXT("2 graze: the figure at path distance %.0f cm (contact %.0f, graze %.0f), %d wall(s) stand"), E->GetPathDistanceToPlayer(), E->ContactRadius, E->GrazeRadius,
				E->GetWalls().Num()));
			Check(bPlaced, TEXT("2 graze: the figure placed within GrazeRadius, outside ContactRadius"));
			ResumeNow();
			Check(E->WasLastExitGraze() && E->GetWalls().Num() == 0 && CountWallActors() == 0,
				FString::Printf(TEXT("2: a graze leaves no wall (graze %d, walls %d, actors %d)"), E->WasLastExitGraze() ? 1 : 0, E->GetWalls().Num(), CountWallActors()));
			return 0.2f;
		}));

		// --- Criterion 3: the strategic wall ---
		S.Add(WaitFor(TEXT("3: a pose with a residue not collected ahead and a strategic wall on the way to it"), []()
		{
			AEntity* E = FindEntity();
			TArray<AResidue*> Residues;
			for (TActorIterator<AResidue> It(W()); It; ++It)
			{
				if (!It->bRegenerates && It->GetState() == EResidueState::Available)
				{
					Residues.Add(*It);
				}
			}
			Residues.Sort([](const AResidue& A, const AResidue& B) { return RouteS(A.GetActorLocation()) < RouteS(B.GetActorLocation()); });
			// Trials: residue by residue, from 10 m before it back to 60 m before it, 2 m apart.
			for (int32 K = 0; K < 6; ++K)
			{
				const int32 Trial = G.SearchTry++;
				const int32 R = Trial / 26;
				if (!Residues.IsValidIndex(R))
				{
					return false;
				}
				AResidue* Residue = Residues[R];
				const float At = RouteS(Residue->GetActorLocation()) - 1000.f - (Trial % 26) * 200.f;
				FVector Floor;
				if (At < 200.f || !RouteFloor(At, Floor))
				{
					continue;
				}
				bool bAhead = false;
				if (NearestNotTaken(RouteS(Floor), &bAhead) != Residue || !bAhead)
				{
					continue;
				}
				StandOn(Floor, Floor + Info()->GetRouteDirectionAtDistance(At) * 500.f + FVector(0.f, 0.f, 150.f));
				StopNow();
				const FEntityWallReport Report = E->GetLastWallReport();
				if (Report.StrategicPassage != INDEX_NONE && Report.StrategicTarget == Residue->GetName())
				{
					G.StrategicPose = Floor;
					G.StrategicResidue = Residue;
					G.FirstStrategic = Report.StrategicPassage;
					TArray<FVector> Path;
					float Length = 0.f;
					NavPath(PlayerNav(), Residue->GetActorLocation(), Path, Length);
					const FEntityWallCandidate& C = E->GetWallCandidates()[Report.StrategicPassage];
					const bool bCrossed = AEntity::PathCrossesPassage(Path, C.Where, E->GetWallHeight());
					Check(bCrossed && Report.bStrategicAhead, FString::Printf(TEXT("3: with %s not collected %.0f m ahead, the strategic wall stands at the passage the path to it crosses (#%d %s at route %.0f m, player at %.0f m)"),
						*Residue->GetName(), (RouteS(Residue->GetActorLocation()) - At) / 100.f, Report.StrategicPassage, LexToString(C.Where.Kind), C.Where.RouteDistance / 100.f, At / 100.f));
					ResumeNow();
					return true;
				}
				ResumeNow();
			}
			return false;
		}, 90.f));
		S.Add(Do(TEXT("3 never the same passage two Stops in a row"), []()
		{
			AEntity* E = FindEntity();
			StandOn(G.StrategicPose, G.StrategicPose + FVector(500.f, 0.f, 150.f));
			StopNow();
			const FEntityWallReport& Report = E->GetLastWallReport();
			Check(Report.StrategicPassage != G.FirstStrategic, FString::Printf(TEXT("3: the next Stop at the same pose does not put it at #%d again (now %d%s)"), G.FirstStrategic,
				Report.StrategicPassage, Report.StrategicWhy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s"), *Report.StrategicWhy)));
			ResumeNow();
			// Collect the residue: stand by it and hold.
			AResidue* Residue = G.StrategicResidue.Get();
			FVector Floor;
			OnNav(Residue->GetActorLocation(), Floor, FVector(150.f, 150.f, 400.f));
			StandOn(Floor, Residue->GetActorLocation());
			StopNow();
			Check(Residue->BeginCollect(Pawn()), FString::Printf(TEXT("3: collecting %s"), *Residue->GetName()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("3 the residue collected"), []() { const AResidue* R = G.StrategicResidue.Get(); return R && R->GetState() == EResidueState::Collected; }, 8.f, []()
		{
			ResumeNow();
			const AResidue* R = G.StrategicResidue.Get();
			Check(R->GetState() == EResidueState::Spent, FString::Printf(TEXT("3: %s banked by the Resume (%s)"), *R->GetName(), LexToString(R->GetState())));
			bool bAhead = false;
			G.NextTarget = NearestNotTaken(RouteS(G.StrategicPose), &bAhead);
			Note(FString::Printf(TEXT("3: the next thing not taken from the pose: %s (%s)"), G.NextTarget.IsValid() ? *G.NextTarget->GetName() : TEXT("none"), bAhead ? TEXT("ahead") : TEXT("behind")));
			G.SearchTry = 0;
			return 0.1f;
		}));
		S.Add(WaitFor(TEXT("3: with it collected, the strategic wall before the next thing not taken"), []()
		{
			AEntity* E = FindEntity();
			const AActor* Next = G.NextTarget.Get();
			if (!Next)
			{
				return false;
			}
			for (int32 K = 0; K < 6; ++K)
			{
				const int32 Trial = G.SearchTry++;
				if (Trial > 40)
				{
					// No pose shows it: then at the pose, the target has moved on and every passage the path to it crosses is one the
					// limits forbid (on the way to a regenerating residue of a gate ahead, or within WallMinDistanceM), as the test
					// computes them itself.
					FVector Floor = G.StrategicPose;
					StandOn(Floor, Floor + FVector(500.f, 0.f, 150.f));
					StopNow();
					const FEntityWallReport Report = E->GetLastWallReport();
					const FVector Player = PlayerNav();
					TArray<FVector> Path;
					float Length = 0.f;
					NavPath(Player, Next->GetActorLocation(), Path, Length);
					TArray<TArray<FVector>> Protected;
					ProtectedPaths(Player, Protected);
					const float Min = UTheLastShiftSettings::Get()->EntityForms.WallMinDistanceM * 100.f;
					int32 Crossed = 0;
					int32 Forbidden = 0;
					const TArray<FEntityWallCandidate>& Candidates = E->GetWallCandidates();
					for (const FEntityWallCandidate& C : Candidates)
					{
						if (C.bExcluded || !AEntity::PathCrossesPassage(Path, C.Where, E->GetWallHeight()))
						{
							continue;
						}
						++Crossed;
						bool bForbidden = false;
						for (const TArray<FVector>& P : Protected)
						{
							bForbidden |= AEntity::PathCrossesPassage(P, C.Where, E->GetWallHeight());
						}
						TArray<FVector> To;
						float ToLength = 0.f;
						bForbidden |= !NavPath(Player, C.Where.Centre, To, ToLength) || ToLength < Min;
						Forbidden += bForbidden ? 1 : 0;
					}
					Check(Report.StrategicTarget == Next->GetName() && Report.StrategicPassage == INDEX_NONE && Forbidden == Crossed,
						FString::Printf(TEXT("3: %s collected, the strategic target moves to %s; no pose of 40 shows its wall, and at the pose every passage on the way to it is forbidden by the limits (%d of %d crossed; '%s')"),
							*G.StrategicResidue->GetName(), *Next->GetName(), Forbidden, Crossed, *Report.StrategicWhy));
					ResumeNow();
					return true;
				}
				// The pose first, then on along the route towards the next thing, then back from it.
				const float Base = RouteS(G.StrategicPose);
				const float At = Trial == 0 ? Base : (Trial <= 20 ? Base + Trial * 200.f : RouteS(Next->GetActorLocation()) - 1000.f - (Trial - 20) * 200.f);
				FVector Floor;
				if (At < 200.f || !RouteFloor(At, Floor))
				{
					continue;
				}
				bool bAhead = false;
				if (NearestNotTaken(RouteS(Floor), &bAhead) != Next)
				{
					continue;
				}
				StandOn(Floor, Floor + Info()->GetRouteDirectionAtDistance(At) * 500.f + FVector(0.f, 0.f, 150.f));
				StopNow();
				const FEntityWallReport Report = E->GetLastWallReport();
				const bool bTargetMoved = Report.StrategicTarget == Next->GetName() && Report.StrategicTarget != G.StrategicResidue->GetName();
				if (bTargetMoved && Report.StrategicPassage != INDEX_NONE)
				{
					TArray<FVector> Path;
					float Length = 0.f;
					NavPath(PlayerNav(), Next->GetActorLocation(), Path, Length);
					const FEntityWallCandidate& C = E->GetWallCandidates()[Report.StrategicPassage];
					Check(AEntity::PathCrossesPassage(Path, C.Where, E->GetWallHeight()), FString::Printf(TEXT("3: %s collected, the strategic wall moves to the way to %s (#%d %s at route %.0f m, player at %.0f m%s)"),
						*G.StrategicResidue->GetName(), *Next->GetName(), Report.StrategicPassage, LexToString(C.Where.Kind), C.Where.RouteDistance / 100.f, At / 100.f, Trial == 0 ? TEXT(", the same pose") : TEXT("")));
					ResumeNow();
					return true;
				}
				ResumeNow();
			}
			return false;
		}, 60.f));

		// --- Criterion 4: the limits over 200 Stops at seeded poses ---
		S.Add(WaitFor(TEXT("4: 200 Stops at seeded poses"), []()
		{
			AEntity* E = FindEntity();
			const float Length = Gen()->GetRouteLengthBeforeWayOut();
			const float Min = UTheLastShiftSettings::Get()->EntityForms.WallMinDistanceM * 100.f;
			for (int32 K = 0; K < 8 && G.SeededDone < 200; ++K)
			{
				const float At = G.SeededRng.FRandRange(200.f, FMath::Max(400.f, Length - 200.f));
				const FVector Side = FVector(G.SeededRng.FRandRange(-150.f, 150.f), G.SeededRng.FRandRange(-150.f, 150.f), 0.f);
				int32 Unused = 0;
				FVector Floor;
				if (!OnNav(Info()->GetRoutePointAtDistance(At, Unused) + Side, Floor, FVector(80.f, 80.f, 300.f)))
				{
					RouteFloor(At, Floor);
				}
				StandOn(Floor, Floor + Info()->GetRouteDirectionAtDistance(At) * 500.f + FVector(0.f, 0.f, 150.f));
				StopNow();
				++G.SeededDone;
				const TArray<AEntityWall*> Walls = E->GetWalls();
				const FEntityWallReport Report = E->GetLastWallReport();
				const FVector Player = PlayerNav();
				G.SeededRaised[FMath::Clamp(Walls.Num(), 0, 4)] += 1;
				G.SeededFewer += Walls.Num() < G.WallCount ? 1 : 0;
				G.SeededWithStrategic += Report.StrategicPassage != INDEX_NONE ? 1 : 0;
				G.BadCount += Walls.Num() > G.WallCount ? 1 : 0;
				if (Report.StrategicPassage != INDEX_NONE && Report.StrategicPassage == G.PrevStrategic)
				{
					++G.BadRepeat;
				}
				G.PrevStrategic = Report.StrategicPassage;
				TArray<TArray<FVector>> Protected;
				ProtectedPaths(Player, Protected);
				for (const AEntityWall* Wall : Walls)
				{
					TArray<FVector> Path;
					float PathLength = 0.f;
					const bool bPath = NavPath(Player, Wall->GetPassage().Centre, Path, PathLength);
					G.NearestSeen = FMath::Min(G.NearestSeen, bPath ? PathLength : -1.f);
					G.BadNear += (!bPath || PathLength < Min - 1.f) ? 1 : 0;
					for (const TArray<FVector>& P : Protected)
					{
						G.BadProtected += AEntity::PathCrossesPassage(P, Wall->GetPassage(), Wall->GetHeight()) ? 1 : 0;
					}
					const FVector Local = Pawn()->GetActorLocation() - Wall->GetPassage().Centre;
					G.BadUnderPlayer += (FMath::Abs(FVector::DotProduct(Local, Wall->GetPassage().Along)) < 15.f + CapsuleRadius()
						&& FMath::Abs(FVector::DotProduct(Local, Wall->GetPassage().Across)) < Wall->GetPassage().HalfWidth + CapsuleRadius()) ? 1 : 0;
				}
				int32 Ways = 0;
				if (Walls.Num() > 0 && !WayOutOpen(Player, Walls, Ways))
				{
					++G.BadWayOut;
					Note(FString::Printf(TEXT("4: Stop %d at route %.0f m closes every way out of segment %d (%d ways)"), G.SeededDone, At / 100.f, SegmentUnder(Player), Ways));
				}
				ResumeNow();
			}
			return G.SeededDone >= 200;
		}, 240.f, []()
		{
			Note(FString::Printf(TEXT("4 NUMBERS %s: 200 Stops; walls raised 0:%d 1:%d 2:%d 3:%d 4:%d; fewer than WallCount (%d) in %d; a strategic wall in %d; nearest wall %.1f m of path"),
				*MapShort(), G.SeededRaised[0], G.SeededRaised[1], G.SeededRaised[2], G.SeededRaised[3], G.SeededRaised[4], G.WallCount, G.SeededFewer, G.SeededWithStrategic,
				G.NearestSeen < TNumericLimits<float>::Max() ? G.NearestSeen / 100.f : -1.f));
			Check(G.BadCount == 0, FString::Printf(TEXT("4: at most WallCount walls a Stop (%d over)"), G.BadCount));
			Check(G.BadNear == 0, FString::Printf(TEXT("4: no wall closer than 8 m of path to the player (%d)"), G.BadNear));
			Check(G.BadProtected == 0, FString::Printf(TEXT("4: no wall on the way to a regenerating residue of a gate ahead (%d)"), G.BadProtected));
			Check(G.BadWayOut == 0, FString::Printf(TEXT("4: always one open way out of the player's segment (%d)"), G.BadWayOut));
			Check(G.BadUnderPlayer == 0, FString::Printf(TEXT("4: no wall across the place the player stands in (%d)"), G.BadUnderPlayer));
			Check(G.BadRepeat == 0, FString::Printf(TEXT("4: the strategic wall never at the same passage two Stops in a row (%d)"), G.BadRepeat));
			Check(G.SeededRaised[0] < 200, FString::Printf(TEXT("4: Stops raise walls (%d of 200 raised none)"), G.SeededRaised[0]));
			return 0.f;
		}));

		// --- Poses for criteria 5, 6, 7, 9 ---
		S.Add(Do(TEXT("poses"), []()
		{
			G.Mouth = FindPose(false);
			G.Walk = FindPose(true);
			Check(G.Mouth.bValid, FString::Printf(TEXT("a corridor-mouth candidate with floors 6 m off it and across it: %s"), G.Mouth.bValid
				? *FString::Printf(TEXT("#%d %s at route %.0f m, %.0f cm wide"), G.Mouth.Candidate, LexToString(G.Mouth.Where.Kind), G.Mouth.Where.RouteDistance / 100.f, G.Mouth.Where.HalfWidth * 2.f) : TEXT("none")));
			Check(G.Walk.bValid, FString::Printf(TEXT("a walkway candidate with floors 6 m off it and across it: %s"), G.Walk.bValid
				? *FString::Printf(TEXT("#%d at route %.0f m, %.0f cm wide"), G.Walk.Candidate, G.Walk.Where.RouteDistance / 100.f, G.Walk.Where.HalfWidth * 2.f) : TEXT("none")));
			return G.Mouth.bValid ? 0.f : -1.f;
		}));

		// --- Criterion 5: blocking; 6: points and the cap; 7: not the figure ---
		S.Add(Do(TEXT("5 before: Stop, no wall"), []()
		{
			ResumeNow();
			// The contact of criterion 2 found the entity: not found again, for the scan's colour and the sighting check.
			Run()->SetEntityFound(false, TEXT("tls.WallCheck: criteria 5 to 7"));
			Run()->SetFirstContact(false, TEXT("tls.WallCheck: criteria 5 to 7"));
			StandAtSix(G.Mouth);
			StopNow();
			AEntity* E = FindEntity();
			E->ClearWalls(TEXT("tls.WallCheck: the path without the wall"));
			G.PathBefore.Reset();
			NavPath(G.Mouth.Near, G.Mouth.Far, G.PathBefore, G.PathBeforeLength);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsWallTrace), false, E);
			Params.AddIgnoredActor(Pawn());
			G.bTraceBeforeHit = W()->LineTraceSingleByChannel(G.TraceBefore, G.Mouth.Near + FVector(0.f, 0.f, 150.f), G.Mouth.Far + FVector(0.f, 0.f, 150.f), ECC_Visibility, Params);
			return 0.1f;
		}));
		AddWallStop(S, TEXT("5"), []() -> const FWallPose& { return G.Mouth; }, 1);
		S.Add(Do(TEXT("5 the path, the navmesh, the traces"), []()
		{
			AEntity* E = FindEntity();
			const TArray<AEntityWall*> Walls = E->GetWalls();
			const AEntityWall* Wall = Walls.Num() > 0 ? Walls[0] : nullptr;
			if (!Wall)
			{
				Check(false, TEXT("5: the wall stands"));
				return -1.f;
			}
			TArray<FVector> After;
			float AfterLength = 0.f;
			NavPath(G.Mouth.Near, G.Mouth.Far, After, AfterLength);
			bool bSame = After.Num() == G.PathBefore.Num() && FMath::Abs(AfterLength - G.PathBeforeLength) < 1.f;
			for (int32 I = 0; bSame && I < After.Num(); ++I)
			{
				bSame &= FVector::Dist(After[I], G.PathBefore[I]) < 1.f;
			}
			Check(bSame && AEntity::PathCrossesPassage(After, Wall->GetPassage(), Wall->GetHeight()), FString::Printf(TEXT("5: the entity's path through the passage is the path without the wall (%d points, %.1f cm; before %d, %.1f cm)"),
				After.Num(), AfterLength, G.PathBefore.Num(), G.PathBeforeLength));
			FVector OnMesh;
			Check(!Wall->GetVolume()->CanEverAffectNavigation() && OnNav(Wall->GetPassage().Centre, OnMesh, FVector(50.f, 50.f, 200.f)),
				TEXT("5: the wall's volume never affects the navmesh, and the navmesh is there across its opening"));
			FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsWallTrace), false, E);
			Params.AddIgnoredActor(Pawn());
			FHitResult Hit;
			const bool bHit = W()->LineTraceSingleByChannel(Hit, G.Mouth.Near + FVector(0.f, 0.f, 150.f), G.Mouth.Far + FVector(0.f, 0.f, 150.f), ECC_Visibility, Params);
			FHitResult CameraHit;
			W()->LineTraceSingleByChannel(CameraHit, G.Mouth.Near + FVector(0.f, 0.f, 150.f), G.Mouth.Far + FVector(0.f, 0.f, 150.f), ECC_Camera, Params);
			Check(bHit == G.bTraceBeforeHit && (!bHit || (Hit.GetActor() == G.TraceBefore.GetActor() && FMath::Abs(Hit.Distance - G.TraceBefore.Distance) < 1.f)) && CameraHit.GetActor() != Wall,
				FString::Printf(TEXT("5: a visibility trace through the opening is as without the wall (hit %d, before %d), the camera channel passes it"), bHit ? 1 : 0, G.bTraceBeforeHit ? 1 : 0));
			const UBoxComponent* Volume = Wall->GetVolume();
			Check(!Volume->IsVisible() && Volume->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore && Volume->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore
				&& Volume->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block && Volume->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Ignore && Volume->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Ignore,
				TEXT("5: the volume is never drawn (no scan's capture sees it) and blocks the pawn's channel only"));

			// Capsule sweeps across the opening, at the floor and a jump up: the wall stops every one.
			const ACharacter* C = Character();
			const UCapsuleComponent* Capsule = C->GetCapsuleComponent();
			const FAreaPassage& P = Wall->GetPassage();
			const float R = CapsuleRadius();
			const float Half = CapsuleHalf();
			FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(TlsWallSweep), false, C);
			SweepParams.AddIgnoredActor(E);
			int32 Sweeps = 0;
			int32 Stopped = 0;
			for (const float U : { -1.f, -0.5f, 0.f, 0.5f, 1.f })
			{
				for (const float Up : { 0.f, 60.f })
				{
					const FVector Lateral = P.Across * (U * (P.HalfWidth - R - 8.f));
					const FVector Start = P.Centre + Lateral + P.Along * (G.Mouth.Side * 120.f) + FVector(0.f, 0.f, Half + 6.f + Up);
					const FVector End = P.Centre + Lateral - P.Along * (G.Mouth.Side * 120.f) + FVector(0.f, 0.f, Half + 6.f + Up);
					FHitResult SweepHit;
					W()->SweepSingleByChannel(SweepHit, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(R, Half), SweepParams, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
					++Sweeps;
					Stopped += SweepHit.bBlockingHit && SweepHit.GetActor() == Wall ? 1 : 0;
				}
			}
			Check(Stopped == Sweeps, FString::Printf(TEXT("5: the player's capsule swept through the opening is stopped by the wall %d times in %d (five places across, at the floor and 60 cm up)"), Stopped, Sweeps));

			// 6: the points inside the opening, three scans' worth.
			int32 Points = 0;
			int32 Outside = 0;
			for (int32 Scan = 1; Scan <= 3; ++Scan)
			{
				TArray<FLidarInjectedPoint> Built;
				Wall->BuildPoints(1000 + Scan, Built);
				Points += Built.Num();
				for (const FLidarInjectedPoint& Point : Built)
				{
					Outside += Wall->IsInsideOpening(Point.WorldPosition, 0.5f) ? 0 : 1;
				}
			}
			Check(Points == 3 * G.WallPoints && Outside == 0, FString::Printf(TEXT("6: every point of the wall lies inside its opening (%d points in 3 scans, %d outside; %d strands, %.1f cm thick)"), Points, Outside,
				Wall->GetStrands().Num(), Wall->GetThickness()));
			Check(E->GetLidarSourceSettings_Implementation().Color.Equals(UTheLastShiftSettings::GetScanColors().Scan) && E->GetLidarSourceSettings_Implementation().FadeMode == ELidarFadeMode::UntilNextScan,
				TEXT("6: not found, the walls' points take the scan's colour, shown by the latest scan only (the entity's source: UntilNextScan)"));
			return 0.f;
		}));
		S.Add(Do(TEXT("7 scan 1"), []() { Lidar()->Scan(); return 0.5f; }));
		S.Add(Do(TEXT("7 scan 2"), []()
		{
			const AEntity* E = FindEntity();
			Note(FString::Printf(TEXT("7: scan 1 gathered %d wall points, %d figure points; the figure kept %d"), E->GetLastGatheredWallPoints(), E->GetLastGatheredFigurePoints(), E->GetLastKeptCheckKept()));
			Lidar()->Scan();
			return 0.5f;
		}));
		S.Add(Do(TEXT("7 no sighting; touching"), []()
		{
			AEntity* E = FindEntity();
			Check(E->GetLastGatheredWallPoints() == G.WallPoints && E->GetLastKeptCheckFigurePoints() > 0 && E->GetSightingStreak() == 0 && !E->WasSightedThisStop() && !E->GetLastKeptCheckResult(),
				FString::Printf(TEXT("7: two scans that keep the wall's points (%d) and not the figure's (kept %d of %d) start no clear sighting (streak %d)"), E->GetLastGatheredWallPoints(),
					E->GetLastKeptCheckKept(), E->GetLastKeptCheckFigurePoints(), E->GetSightingStreak()));
			// Against the wall's face.
			const FAreaPassage& P = G.Mouth.Where;
			const float Touch = UTheLastShiftSettings::Get()->EntityForms.WallBlockDepth * 0.5f + CapsuleRadius() + 3.f;
			FVector Floor;
			OnNav(P.Centre + P.Along * (G.Mouth.Side * Touch), Floor, FVector(30.f, 30.f, 250.f));
			Floor = FVector(P.Centre.X, P.Centre.Y, Floor.Z) + P.Along * (G.Mouth.Side * Touch);
			StandOn(Floor, P.Centre - P.Along * (G.Mouth.Side * 300.f) + FVector(0.f, 0.f, 150.f));
			G.WalkStart = Real();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("7 pressing into the wall for 1 s"), []()
		{
			if (APawn* P = Pawn())
			{
				P->AddMovementInput(-G.Mouth.Where.Along * G.Mouth.Side, 1.f);
			}
			return Real() - G.WalkStart > 1.0;
		}, 3.f, []()
		{
			AEntity* E = FindEntity();
			const AEntityWall* Wall = E->GetWalls().Num() > 0 ? E->GetWalls()[0] : nullptr;
			const double Signed = Wall ? Wall->GetSignedDistance(Pawn()->GetActorLocation()) * G.Mouth.Side : -1.0;
			Check(IsStopped() && !E->WasContactThisStop() && Signed > 0.0, FString::Printf(TEXT("7: walking into the wall is no contact (still Stopped, contact %d) and the player stays on its side (%.0f cm off its plane)"),
				E->WasContactThisStop() ? 1 : 0, Signed));
			StandOn(G.Mouth.NearStart, G.Mouth.Where.Centre - G.Mouth.Where.Along * (G.Mouth.Side * 300.f) + FVector(0.f, 0.f, 150.f));
			G.WalkStart = Real();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("5 walking at the wall for 1.5 s while Stopped"), []()
		{
			if (APawn* P = Pawn())
			{
				P->AddMovementInput(-G.Mouth.Where.Along * G.Mouth.Side, 1.f);
			}
			return Real() - G.WalkStart > 1.5;
		}, 3.f, []()
		{
			AEntity* E = FindEntity();
			const AEntityWall* Wall = E->GetWalls().Num() > 0 ? E->GetWalls()[0] : nullptr;
			const double Signed = Wall ? Wall->GetSignedDistance(Pawn()->GetActorLocation()) * G.Mouth.Side : -1.0;
			Check(Signed > 0.0 && Signed < 200.0 - 20.0, FString::Printf(TEXT("5: walking at it while Stopped, the player stops at the wall (%.0f cm off its plane, from 200 cm)"), Signed));
			// The entity walks through: from the near side to the player on the far side.
			StandOn(G.Mouth.FarAway, G.Mouth.Where.Centre + FVector(0.f, 0.f, 150.f));
			E->Speed = 300.f;
			E->PlaceFigure(G.Mouth.Near, false);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("5 the entity crosses the wall's opening"), []()
		{
			const AEntity* E = FindEntity();
			const AEntityWall* Wall = E->GetWalls().Num() > 0 ? E->GetWalls()[0] : nullptr;
			return Wall && Wall->GetSignedDistance(E->GetFigureLocation()) * G.Mouth.Side < -30.0;
		}, 6.f, []()
		{
			AEntity* E = FindEntity();
			Check(E->GetWalls().Num() == 1 && IsStopped(), FString::Printf(TEXT("5: the figure walked through the standing wall (figure at %s)"), *E->GetFigureLocation().ToString()));
			E->Speed = 0.f;
			StandAtSix(G.Mouth);
			PutFigureAway();
			return 0.f;
		}));
		// 6: the cap, with every residue, the figure, the trail and WallCount walls in the scan's coverage.
		S.Add(Do(TEXT("6 the cap"), []()
		{
			AEntity* E = FindEntity();
			StandAtSix(G.Mouth);
			TArray<int32> At;
			for (int32 K = 0; K < G.WallCount; ++K)
			{
				At.Add(G.Mouth.Candidate);
			}
			E->RaiseWallsAtForTest(At);
			const APawn* P = Pawn();
			const FVector Eye = EyeOf(P);
			const FVector Ahead = (G.Mouth.Where.Centre - G.Mouth.Six).GetSafeNormal2D();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
			int32 Index = 0;
			for (TActorIterator<AResidue> It(W()); It; ++It)
			{
				G.MovedResidues.Emplace(*It, It->GetActorLocation());
				It->SetActorLocation(Eye + Ahead * (250.f + 30.f * (Index % 4)) + Right * (-90.f + 60.f * (Index % 4)) + FVector(0.f, 0.f, -30.f + 20.f * (Index / 4)));
				++Index;
			}
			FVector Figure;
			OnNav(G.Mouth.Six + Ahead * 350.f, Figure, FVector(60.f, 60.f, 250.f));
			E->PlaceFigure(Figure, true);
			StandAtSix(G.Mouth);
			Lidar()->Scan();
			return 0.5f;
		}));
		S.Add(Do(TEXT("6 the cap: nothing dropped"), []()
		{
			AEntity* E = FindEntity();
			const FLidarInjectionStats Stats = Lidar()->GetLastInjectionStats();
			int32 Residues = 0;
			int32 ResiduePoints = 0;
			for (TActorIterator<AResidue> It(W()); It; ++It)
			{
				++Residues;
				ResiduePoints += It->GetLastGatheredPoints();
			}
			const int32 Entity = E->GetLastGatheredFigurePoints() + E->GetLastGatheredTrailPoints() + E->GetLastGatheredWallPoints();
			Note(FString::Printf(TEXT("6 CAP %s: figure %d + trail %d + %d wall(s) %d + %d residues %d = %d of the cap %d; gathered %d, outside coverage %d, over the cap %d, uploaded %d"), *MapShort(),
				E->GetLastGatheredFigurePoints(), E->GetLastGatheredTrailPoints(), E->GetWalls().Num(), E->GetLastGatheredWallPoints(), Residues, ResiduePoints, Entity + ResiduePoints,
				Lidar()->MaxInjectedPointsPerScan, Stats.Gathered, Stats.OutsideCoverage, Stats.OverCap, Stats.Uploaded));
			Check(Stats.OverCap == 0 && Lidar()->GetLastDroppedInjectedPoints() == 0 && Entity + ResiduePoints <= Lidar()->MaxInjectedPointsPerScan && E->GetLastGatheredWallPoints() == G.WallCount * G.WallPoints,
				FString::Printf(TEXT("6: the figure, the trail, every residue and %d walls in one scan's coverage drop no injected point (%d of %d; %d outside the coverage)"), G.WallCount,
					Entity + ResiduePoints, Lidar()->MaxInjectedPointsPerScan, Stats.OutsideCoverage));
			for (const TPair<TWeakObjectPtr<AResidue>, FVector>& Moved : G.MovedResidues)
			{
				if (AResidue* Residue = Moved.Key.Get())
				{
					Residue->SetActorLocation(Moved.Value);
				}
			}
			G.MovedResidues.Reset();
			return 0.f;
		}));
		// 9 (mouth) and 6 (the entity's colour, the latest scan only).
		S.Add(Do(TEXT("9 mouth: found, one wall"), []()
		{
			AEntity* E = FindEntity();
			Run()->SetEntityFound(true, TEXT("tls.WallCheck: the entity's own colour"));
			// The residues out of every shot from here on (their colour is not the entity's, but close enough to count): put back at the end.
			for (TActorIterator<AResidue> It(W()); It; ++It)
			{
				G.MovedResidues.Emplace(*It, It->GetActorLocation());
				It->SetActorLocation(It->GetActorLocation() - FVector(0.f, 0.f, 100000.f));
			}
			E->RaiseWallsAtForTest({ G.Mouth.Candidate });
			StandAtSix(G.Mouth);
			PutFigureAway();
			StandAtSix(G.Mouth);
			Check(E->GetLidarSourceSettings_Implementation().Color.Equals(UTheLastShiftSettings::GetScanColors().Entity), TEXT("6: found, the walls' points take the entity's own colour"));
			Lidar()->Scan();
			return 0.6f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_CorridorMouth_6m"), *MapShort(), *G.Form); }, [](const FShot& Shot)
		{
			G.EntityHueFound = CountHue(Shot, EHue::Entity);
			Check(G.EntityHueFound > 150, FString::Printf(TEXT("9: the wall across the %s from 6 m in a scan, in the entity's colour (%d entity-coloured pixels)"), LexToString(G.Mouth.Where.Kind), G.EntityHueFound));
		});
		// Brief 47: the strands' width, before (the form's WallThickness before Brief 47, half today's, for the session) and now.
		S.Add(Do(TEXT("47 width before"), []()
		{
			AEntity* E = FindEntity();
			G.LookForm = E->GetRequestedForm();
			G.SavedLook = AEntity::GetSessionWallLook(G.LookForm);
			float Thickness = 0.f;
			int32 Strands = 0;
			float Bow = 0.f;
			AEntity::GetWallLookOf(G.LookForm, Thickness, Strands, Bow);
			G.ThicknessAfter = Thickness;
			AEntity::SetSessionWallLook(G.LookForm, FVector3f(Thickness * 0.5f, static_cast<float>(Strands), Bow));
			GShotFolder = TEXT("Brief47");
			E->RaiseWallsAtForTest({ G.Mouth.Candidate });
			StandAtSix(G.Mouth);
			PutFigureAway();
			StandAtSix(G.Mouth);
			Note(FString::Printf(TEXT("47: %s's walls before Brief 47: WallThickness %.1f cm; now %.1f cm"), *G.Form, Thickness * 0.5f, Thickness));
			Lidar()->Scan();
			return 0.6f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_CorridorMouth_6m_WidthBefore"), *MapShort(), *G.Form); }, [](const FShot&)
		{
			int32 Points = 0, Outside = 0;
			G.WidthBeforePx = StrandWidthPx(Points, Outside);
		});
		S.Add(Do(TEXT("47 width now"), []()
		{
			AEntity* E = FindEntity();
			AEntity::SetSessionWallLook(G.LookForm, G.SavedLook);
			E->RaiseWallsAtForTest({ G.Mouth.Candidate });
			StandAtSix(G.Mouth);
			Lidar()->Scan();
			return 0.6f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_CorridorMouth_6m_WidthNow"), *MapShort(), *G.Form); }, [](const FShot& Shot)
		{
			int32 Points = 0, Outside = 0;
			G.WidthAfterPx = StrandWidthPx(Points, Outside);
			const float Ratio = G.WidthAfterPx / FMath::Max(G.WidthBeforePx, 0.01f);
			Check(Ratio >= 1.6f && Ratio <= 2.4f, FString::Printf(TEXT("47: from 6 m a strand is twice as wide on screen as before, within a fifth (%.1f px against %.1f px: x%.2f; %d entity-coloured pixels)"),
				G.WidthAfterPx, G.WidthBeforePx, Ratio, CountHue(Shot, EHue::Entity)));
			Check(Points == G.WallPoints && Outside == 0, FString::Printf(TEXT("47: with the wider strands every point of the wall lies inside its opening (%d points, %d outside; WallThickness %.1f cm), no point added"),
				Points, Outside, G.ThicknessAfter));
			GShotFolder = TEXT("Brief30");
		});
		S.Add(Do(TEXT("6 latest scan only: a scan looking away"), []()
		{
			// Turned away first: the capture follows the camera, which takes the new view on the pawn's next tick.
			StandOn(G.Mouth.Six, G.Mouth.Six - (G.Mouth.Where.Centre - G.Mouth.Six).GetSafeNormal2D() * 500.f + FVector(0.f, 0.f, 150.f));
			return 0.3f;
		}));
		S.Add(Do(TEXT("6 latest scan only: the scan looking away"), []()
		{
			Lidar()->Scan();
			return 0.8f;
		}));
		S.Add(Do(TEXT("6 latest scan only: look back"), []()
		{
			const FLidarInjectionStats Stats = Lidar()->GetLastInjectionStats();
			Note(FString::Printf(TEXT("6: the scan looking away: %d injected points gathered, %d outside its coverage, %d uploaded"), Stats.Gathered, Stats.OutsideCoverage, Stats.Uploaded));
			StandAtSix(G.Mouth);
			return 0.3f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_CorridorMouth_AfterNextScan"), *MapShort(), *G.Form); }, [](const FShot& Shot)
		{
			const int32 Hue = CountHue(Shot, EHue::Entity);
			Check(Hue * 10 < FMath::Max(G.EntityHueFound, 1), FString::Printf(TEXT("6: after the next scan, which does not see it, the wall's points are gone: shown by the latest scan only (%d entity-coloured pixels, %d before)"),
				Hue, G.EntityHueFound));
		});
		S.Add(Do(TEXT("5 Normal time: walk through"), []()
		{
			ResumeNow();
			AEntity* E = FindEntity();
			Check(!IsStopped() && E->GetWalls().Num() == 0 && CountWallActors() == 0, TEXT("5: the Stop ended, no wall stands"));
			StandOn(G.Mouth.NearStart, G.Mouth.Where.Centre - G.Mouth.Where.Along * (G.Mouth.Side * 300.f) + FVector(0.f, 0.f, 150.f));
			G.WalkStart = Real();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("5 walking for 2 s in Normal time"), []()
		{
			if (APawn* P = Pawn())
			{
				P->AddMovementInput(-G.Mouth.Where.Along * G.Mouth.Side, 1.f);
			}
			return Real() - G.WalkStart > 2.0;
		}, 4.f, []()
		{
			const double Signed = FVector::DotProduct(Pawn()->GetActorLocation() - G.Mouth.Where.Centre, G.Mouth.Where.Along) * G.Mouth.Side;
			Check(Signed < -CapsuleRadius(), FString::Printf(TEXT("5: in Normal time the player walks through the same passage (%.0f cm past its plane)"), -Signed));
			return 0.f;
		}));

		// --- 9: the walkway ---
		AddWallStop(S, TEXT("9 walkway"), []() -> const FWallPose& { return G.Walk.bValid ? G.Walk : G.Mouth; }, 1);
		S.Add(Do(TEXT("9 walkway: scan"), []() { StandAtSix(G.Walk.bValid ? G.Walk : G.Mouth); Lidar()->Scan(); return 0.6f; }));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_Walkway_6m"), *MapShort(), *G.Form); }, [](const FShot& Shot)
		{
			const int32 Hue = CountHue(Shot, EHue::Entity);
			Check(G.Walk.bValid && Hue > 150, FString::Printf(TEXT("9: the wall across a walkway from 6 m in a scan, in the entity's colour (%d entity-coloured pixels)"), Hue));
		});

		// --- Criterion 8: the switches ---
		S.Add(Do(TEXT("8 bEntityWalls off"), []()
		{
			ResumeNow();
			GetMutableDefault<UTheLastShiftSettings>()->EntityForms.bEntityWalls = false;
			AEntity* E = FindEntity();
			StandOn(G.TimingPose, G.TimingPose + FVector(500.f, 0.f, 150.f));
			StopNow();
			Check(!E->AreWallsOn() && E->GetWalls().Num() == 0 && CountWallActors() == 0 && E->GetLastWallReport().Raised == 0 && E->GetLastWallReport().Passages.Num() == 0,
				FString::Printf(TEXT("8: bEntityWalls off: no candidate chosen, no wall (report '%s')"), *E->GetLastWallReport().StrategicWhy));
			Lidar()->Scan();
			return 0.4f;
		}));
		S.Add(Do(TEXT("8 bEntityWalls off: scan"), []()
		{
			AEntity* E = FindEntity();
			Check(E->GetLastGatheredWallPoints() == 0, FString::Printf(TEXT("8: bEntityWalls off: no wall point in a scan (%d)"), E->GetLastGatheredWallPoints()));
			ResumeNow();
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			Settings->EntityForms.bEntityWalls = true;
			if (Settings->ActTuning.IsValidIndex(G.Act - 1))
			{
				Settings->ActTuning[G.Act - 1].WallCount = 0;
			}
			else
			{
				Settings->TestActTuning.WallCount = 0;
			}
			StandOn(G.TimingPose, G.TimingPose + FVector(500.f, 0.f, 150.f));
			StopNow();
			Check(!E->AreWallsOn() && E->GetWallCountNow() == 0 && E->GetWalls().Num() == 0 && CountWallActors() == 0 && E->GetLastWallReport().Raised == 0,
				FString::Printf(TEXT("8: the act's WallCount 0: no candidate chosen, no wall (report '%s')"), *E->GetLastWallReport().StrategicWhy));
			Lidar()->Scan();
			return 0.4f;
		}));
		S.Add(Do(TEXT("8 WallCount 0: scan"), []()
		{
			AEntity* E = FindEntity();
			Check(E->GetLastGatheredWallPoints() == 0, FString::Printf(TEXT("8: WallCount 0: no wall point in a scan (%d)"), E->GetLastGatheredWallPoints()));
			ResumeNow();
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			Settings->ActTuning = G.SavedActs;
			Settings->TestActTuning = G.SavedTestAct;
			StandOn(G.TimingPose, G.TimingPose + FVector(500.f, 0.f, 150.f));
			StopNow();
			Check(E->AreWallsOn() && E->GetWalls().Num() > 0, FString::Printf(TEXT("8: both back on: the same pose raises walls again (%d)"), E->GetWalls().Num()));
			ResumeNow();
			return 0.f;
		}));

		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun), 0.f);
		UE_LOG(LogTheLastShift, Log, TEXT("%s: started (%d steps)."), *GRun.Name, GRun.Steps.Num());
	}

	// =====================================================================================================
	// tls.Entity.Walls
	// =====================================================================================================

	static void LogWalls()
	{
		AEntity* E = FindEntity();
		if (!E)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Entity.Walls: no entity in this map."));
			return;
		}
		const TOptional<bool> Session = AEntity::GetSessionWalls();
		const TOptional<int32> Count = AEntity::GetSessionWallCount();
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.Walls: walls %s (session %s, settings %s), count %d (session %s), %d points each; %d standing."), E->AreWallsOn() ? TEXT("on") : TEXT("off"),
			Session.IsSet() ? (Session.GetValue() ? TEXT("on") : TEXT("off")) : TEXT("unset"), UTheLastShiftSettings::Get()->EntityForms.bEntityWalls ? TEXT("on") : TEXT("off"), E->GetWallCountNow(),
			Count.IsSet() ? *FString::FromInt(Count.GetValue()) : TEXT("unset"), E->GetWallPointsNow(), E->GetWalls().Num());
		for (const AEntityWall* Wall : E->GetWalls())
		{
			const FAreaPassage& P = Wall->GetPassage();
			UE_LOG(LogTheLastShift, Log, TEXT("  wall at #%d %s%s: centre %s, %.0f cm wide, %.0f cm high, %d strands, %d points"), Wall->GetPassageIndex(), LexToString(P.Kind),
				Wall->IsStrategic() ? TEXT(" (strategic)") : TEXT(""), *P.Centre.ToString(), P.HalfWidth * 2.f, Wall->GetHeight(), Wall->GetStrands().Num(), Wall->GetPointCount());
		}
		const FEntityWallReport& R = E->GetLastWallReport();
		UE_LOG(LogTheLastShift, Log, TEXT("  last choice (Stop %d, %s): %d of %d; strategic target %s, passage %d%s; usable %d, protected %d, in range %d, too near %d, left out for the way out %d; player segment %d at route %.0f m (%d ways out)"),
			R.StopSerial, *R.Why, R.Raised, R.Wanted, R.StrategicTarget.IsEmpty() ? TEXT("none") : *R.StrategicTarget, R.StrategicPassage, R.StrategicWhy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *R.StrategicWhy),
			R.Usable, R.Protected, R.InRange, R.TooNear, R.RejectedWayOut, R.PlayerSegment, R.PlayerRouteDistance / 100.f, R.WaysOut);
		const TArray<FEntityWallCandidate>& Candidates = E->GetWallCandidates();
		for (int32 I = 0; I < Candidates.Num(); ++I)
		{
			const FEntityWallCandidate& C = Candidates[I];
			UE_LOG(LogTheLastShift, Log, TEXT("  candidate #%d %s: segment %d/%d, route %.0f m, centre %s, %.0f cm wide%s"), I, LexToString(C.Where.Kind), C.Where.Segment, C.Where.OtherSegment,
				C.Where.RouteDistance / 100.f, *C.Where.Centre.ToString(), C.Where.HalfWidth * 2.f, C.bExcluded ? *FString::Printf(TEXT(" - excluded: %s"), *C.Why) : TEXT(""));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsWallCheckCommand(
	TEXT("tls.WallCheck"),
	TEXT("tls.WallCheck [Quit]: Brief 30, the entity's walls (an area map, PIE): candidates, timing, the strategic wall, the limits over 200 seeded Stops, blocking, points, not the figure, switches; shots to Saved/Screenshots/Brief30."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		WallDebug::StartCheck(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityWallsCommand(
	TEXT("tls.Entity.Walls"),
	TEXT("tls.Entity.Walls [0|1] [Count]: Brief 30, the entity's walls off or on and their count for the session (no argument: logs the Stop's walls and the area's candidates)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		if (Args.Num() > 0)
		{
			AEntity::SetSessionWalls(FCString::Atoi(*Args[0]) != 0);
			AEntity::SetSessionWallCount(Args.Num() > 1 ? TOptional<int32>(FMath::Clamp(FCString::Atoi(*Args[1]), 0, 4)) : TOptional<int32>());
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.Walls: walls %s for the session%s (from the next Stop)."), FCString::Atoi(*Args[0]) != 0 ? TEXT("on") : TEXT("off"),
				Args.Num() > 1 ? *FString::Printf(TEXT(", %d a Stop"), FMath::Clamp(FCString::Atoi(*Args[1]), 0, 4)) : TEXT(", the act's count"));
		}
		WallDebug::LogWalls();
	}));

#endif // !UE_BUILD_SHIPPING

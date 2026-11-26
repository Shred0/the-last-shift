// SPDX-License-Identifier: MIT

// Brief 29, the entity's four forms. tls.Entity.Form [Four|Vast|Wheels|Echo|Procedural|Act] [YawOffsetDeg] sets the figure's
// form for the session (Act: each act's own again; no argument: logs it) and, given, the form's yaw offset for the session. tls.Entity.Show awakens the entity and stands its figure 8 m in
// front of the player, still, for the next scans of the Stop (it stops time when needed). tls.EntityFormCheck [Quit] (an area
// map, PIE): the act's form with its configured mesh and count, every point on the mesh as it stands at the figure's place,
// front along the direction of travel turned by the form's YawOffsetDeg, and a case at an offset of 90 for the session (the
// points turn about the pivot, the figure's place and travel do not); no injected point dropped with the figure, the trail and every residue of the area in
// a scan's coverage; the scan's colour before the entity is found and its own after, shown by the latest scan only; a contact
// at ContactRadius and a graze at GrazeRadius with the form and with the procedural figure; the fallback (modelled figures
// off, a form with no mesh, the procedural form); logs the share a scan keeps from 8 m in a closed corridor and in the
// area's largest open space and the traces of the found check, next to the procedural figure's (FORM NUMBERS line); shots
// from 6 and 15 m in the open space and from 8 m in the corridor, in the entity's own colour, to Saved/Screenshots/Brief29.
// It puts back the run's state, the residues, the entity's tuning, the settings and the session's form. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Debug/EntityFormTestScope.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Area/AreaTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Entity/EntityFigureShape.h"
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

namespace EntityFormDebug
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
			FPlatformMisc::RequestExit(false, TEXT("tls Brief 29 test"));
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
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }

	static AEntity* FindEntity(UWorld* World = nullptr)
	{
		World = World ? World : W();
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static FVector EyeOf(const APawn* P)
	{
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() : (P ? P->GetActorLocation() + FVector(0.f, 0.f, 62.f) : FVector::ZeroVector);
	}

	/** Height of the eye above the navmesh point the pawn stands on. */
	static float EyeAboveFloor()
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		return Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f + static_cast<float>(EyeOf(Character).Z - Character->GetActorLocation().Z) : 160.f;
	}

	static void StandOn(const FVector& Floor, const FVector& LookAt)
	{
		ACharacter* Character = Cast<ACharacter>(Pawn());
		if (!Character)
		{
			return;
		}
		const float Half = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->SetActorLocation(Floor + FVector(0.f, 0.f, Half + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->StopMovementImmediately();
		if (APlayerController* C = PC())
		{
			C->SetControlRotation((LookAt - EyeOf(Character)).Rotation());
		}
	}

	static bool OnNav(const FVector& Point, FVector& Out, const FVector& Extent = FVector(60.f, 60.f, 300.f))
	{
		UWorld* World = W();
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		FNavLocation Location;
		if (!Nav || !Nav->ProjectPointToNavigation(Point, Location, Extent))
		{
			return false;
		}
		Out = Location.Location;
		return true;
	}

	/** Every height of a figure of about the procedural one's size is in sight from Eye (the player and the entity ignored). */
	static bool AllHeightsInSight(const FVector& Eye, const FVector& Figure)
	{
		UWorld* World = W();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsEntityFormSight), false, FindEntity());
		Params.AddIgnoredActor(Pawn());
		for (const float Height : { 20.f, 60.f, 100.f, 135.f, 170.f })
		{
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Eye, Figure + FVector(0.f, 0.f, Height), ECC_Visibility, Params))
			{
				return false;
			}
		}
		return true;
	}

	/** A navmesh point Target (within Tolerance) from Figure horizontally, level with it, from which all of it is in sight. */
	static bool FindFloorAt(const AAreaInfo* Info, const FVector& Figure, float Target, float Tolerance, FVector& OutFloor)
	{
		const float EyeAbove = EyeAboveFloor();
		auto Accept = [&](const FVector& Candidate)
		{
			FVector Floor;
			if (!OnNav(Candidate, Floor, FVector(40.f, 40.f, 300.f)))
			{
				return false;
			}
			if (FMath::Abs(FVector::Dist2D(Floor, Figure) - Target) > Tolerance || FMath::Abs(Floor.Z - Figure.Z) > 150.f)
			{
				return false;
			}
			if (!AllHeightsInSight(Floor + FVector(0.f, 0.f, EyeAbove), Figure))
			{
				return false;
			}
			OutFloor = Floor;
			return true;
		};
		// Along the route first (walkways, corridors), then on rings round the figure.
		if (Info)
		{
			int32 Segment = 0;
			float Offset = 0.f;
			const float Here = Info->ProjectToRoute(Figure, Segment, Offset);
			for (float Step = Target - Tolerance; Step <= Target * 1.6f; Step += 25.f)
			{
				for (const float Sign : { 1.f, -1.f })
				{
					int32 Unused = 0;
					if (Accept(Info->GetRoutePointAtDistance(Here + Sign * Step, Unused)))
					{
						return true;
					}
				}
			}
		}
		for (int32 Step = 0; Step < 72; ++Step)
		{
			const float Angle = Step * (2.f * UE_PI / 72.f);
			if (Accept(Figure + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Target))
			{
				return true;
			}
		}
		return false;
	}

	/** The newest alive scan's context (INDEX_NONE scan index when none). */
	static FLidarScanContext LatestScan()
	{
		FLidarScanContext Latest;
		if (const ULidarScanComponent* L = Lidar())
		{
			for (const FLidarScanContext& Context : L->GetAliveScans())
			{
				if (Latest.ScanIndex == INDEX_NONE || Context.ScanIndex > Latest.ScanIndex)
				{
					Latest = Context;
				}
			}
		}
		return Latest;
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
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief29"), GShot.Label + TEXT(".png")));
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
	// tls.EntityFormCheck
	// =====================================================================================================

	struct FPose
	{
		bool bValid = false;
		FVector Figure = FVector::ZeroVector;
		FVector Player = FVector::ZeroVector;
		FString Where;
	};

	struct FNumbers
	{
		int32 Points = 0;
		int32 Kept = 0;
		int32 Traces = 0;
		int32 ProceduralPoints = 0;
		int32 ProceduralKept = 0;
		int32 ProceduralTraces = 0;
		bool bMeasured = false;
	};

	struct FCheck
	{
		int32 Act = 0;
		EEntityForm Form = EEntityForm::Procedural;
		FEntityFormTuning Tuning;
		bool bSavedFound = false;
		bool bSavedContact = false;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		float SavedSpeed = 66.6667f;
		TOptional<EEntityForm> SavedSessionForm;
		FEntityForms SavedForms;
		TArray<TPair<TWeakObjectPtr<AResidue>, FVector>> MovedResidues;
		FPose Corridor;
		FPose Open;
		FVector OpenSix = FVector::ZeroVector;
		FVector OpenFifteen = FVector::ZeroVector;
		float LookHeight = 90.f;
		FNumbers CorridorNumbers;
		FNumbers OpenNumbers;
		int32 FoundEntityHue = 0;
		int32 NotFoundEntityHue = -1;
		int32 ContactPhase = 0;
	};
	static FCheck G;

	static FString FormName() { return LexToString(G.Form); }
	static FString MapShort()
	{
		const UWorld* World = W();
		return World ? FPaths::GetBaseFilename(World->GetOutermost()->GetName()).Replace(TEXT("UEDPIE_0_"), TEXT("")) : FString();
	}

	static void SetForm(TOptional<EEntityForm> Form)
	{
		AEntity::SetSessionForm(Form);
		if (AEntity* E = FindEntity())
		{
			E->RefreshFigureShape();
		}
	}

	/** Corridor: a straight 8 m stretch of a closed corridor (not the exit corridor), the figure at one end, the player at the other. */
	static FPose FindCorridorPose(const AAreaInfo* Info)
	{
		FPose Pose;
		if (!Info)
		{
			return Pose;
		}
		for (const EAreaSegmentType Type : { EAreaSegmentType::Corridor, EAreaSegmentType::Gate, EAreaSegmentType::Room })
		{
			for (const FAreaSegmentRecord& Segment : Info->Segments)
			{
				if (Segment.Type != Type || !Segment.bClosed || Segment.bExitCorridor || Segment.bTwinAIRoom)
				{
					continue;
				}
				for (float D = Segment.RouteStart + 200.f; D + 1000.f <= Segment.RouteEnd; D += 100.f)
				{
					const FVector Dir = Info->GetRouteDirectionAtDistance(D);
					bool bStraight = true;
					for (float Along = 200.f; Along <= 1000.f && bStraight; Along += 200.f)
					{
						bStraight = FVector::DotProduct(Dir, Info->GetRouteDirectionAtDistance(D + Along)) > 0.9995f;
					}
					int32 Unused = 0;
					FVector Figure, Player;
					if (!bStraight || !OnNav(Info->GetRoutePointAtDistance(D, Unused), Figure) || !OnNav(Info->GetRoutePointAtDistance(D + 800.f, Unused), Player))
					{
						continue;
					}
					if (FMath::Abs(FVector::Dist2D(Figure, Player) - 800.f) > 40.f || !AllHeightsInSight(Player + FVector(0.f, 0.f, EyeAboveFloor()), Figure))
					{
						continue;
					}
					Pose.bValid = true;
					Pose.Figure = Figure;
					Pose.Player = Player;
					Pose.Where = FString::Printf(TEXT("%s at route %.0f..%.0f m (segment %.0f..%.0f m)"), LexToString(Type), D / 100.f, (D + 800.f) / 100.f,
						Segment.RouteStart / 100.f, Segment.RouteEnd / 100.f);
					return Pose;
				}
			}
		}
		return Pose;
	}

	/** Open space: the largest open segment, a figure point from which floors at 6, 8 and 15 m see all of it. */
	static FPose FindOpenPose(const AAreaInfo* Info, FVector& OutSix, FVector& OutFifteen)
	{
		FPose Pose;
		if (!Info)
		{
			return Pose;
		}
		int32 Best = INDEX_NONE;
		double BestArea = 0.0;
		for (int32 I = 0; I < Info->Segments.Num(); ++I)
		{
			const FAreaSegmentRecord& Segment = Info->Segments[I];
			const FVector Size = Segment.Bounds.GetSize();
			if (!Segment.bClosed && Size.X * Size.Y > BestArea)
			{
				BestArea = Size.X * Size.Y;
				Best = I;
			}
		}
		if (Best == INDEX_NONE)
		{
			return Pose;
		}
		const FAreaSegmentRecord& Segment = Info->Segments[Best];
		const float Mid = (Segment.RouteStart + Segment.RouteEnd) * 0.5f;
		for (float Step = 0.f; Step <= (Segment.RouteEnd - Segment.RouteStart) * 0.5f; Step += 200.f)
		{
			for (const float Sign : { 1.f, -1.f })
			{
				int32 Unused = 0;
				FVector Figure, Eight;
				if (!OnNav(Info->GetRoutePointAtDistance(Mid + Sign * Step, Unused), Figure))
				{
					continue;
				}
				if (FindFloorAt(Info, Figure, 800.f, 60.f, Eight) && FindFloorAt(Info, Figure, 600.f, 60.f, OutSix) && FindFloorAt(Info, Figure, 1500.f, 100.f, OutFifteen))
				{
					Pose.bValid = true;
					Pose.Figure = Figure;
					Pose.Player = Eight;
					Pose.Where = FString::Printf(TEXT("%s %d (%.0f x %.0f m), figure at route %.0f m"), LexToString(Segment.Type), Best, Segment.Bounds.GetSize().X / 100.f,
						Segment.Bounds.GetSize().Y / 100.f, (Mid + Sign * Step) / 100.f);
					return Pose;
				}
				if (Step == 0.f)
				{
					break;
				}
			}
		}
		return Pose;
	}

	/** A Stop with the entity manifested, held at Figure, the player standing on Player looking at it. */
	static void AddStopAt(TArray<FStep>& S, const FString& Label, TFunction<FVector()> Figure, TFunction<FVector()> Player)
	{
		S.Add(Do(Label + TEXT(": Stop"), []()
		{
			if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped())
			{
				T->Stop();
			}
			AEntity* E = FindEntity();
			if (E && !E->IsManifested())
			{
				E->SpawnNow(800.f);
			}
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": the entity is manifested"), []() { const AEntity* E = FindEntity(); return E && E->IsManifested() && IsStopped(); }, 5.f));
		S.Add(WaitFor(Label + TEXT(": the automatic scan has fired"), []() { const UScanDirectorComponent* D = Director(); return D && D->GetScansThisStop() >= 1; }, 4.f, [Label, Figure, Player]()
		{
			const FVector At = Figure();
			StandOn(Player(), At + FVector(0.f, 0.f, G.LookHeight));
			AEntity* E = FindEntity();
			Check(E->PlaceFigure(At, true), FString::Printf(TEXT("%s: the figure placed at %s, held"), *Label, *At.ToString()));
			StandOn(Player(), E->GetFigureLocation() + FVector(0.f, 0.f, G.LookHeight));
			return 0.2f;
		}));
	}

	static void AddScan(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": scan"), []() { Lidar()->Scan(); return 0.4f; }));
	}

	/** Measures the share kept and the traces on the newest scan, with the act's form and with the procedural figure. */
	static void AddMeasure(TArray<FStep>& S, const FString& Label, FNumbers* Out)
	{
		S.Add(Do(Label + TEXT(": measure"), [Label, Out]()
		{
			AEntity* E = FindEntity();
			const FLidarScanContext Context = LatestScan();
			Check(Context.ScanIndex != INDEX_NONE, FString::Printf(TEXT("%s: a scan to measure (scan %d)"), *Label, Context.ScanIndex));
			int32 Unused = 0;
			E->MeasureFigureKept(Context, true, Out->Kept, Unused, Out->Points);
			E->MeasureFigureKept(Context, false, Unused, Out->Traces, Unused);
			SetForm(EEntityForm::Procedural);
			E->MeasureFigureKept(Context, true, Out->ProceduralKept, Unused, Out->ProceduralPoints);
			E->MeasureFigureKept(Context, false, Unused, Out->ProceduralTraces, Unused);
			SetForm(TOptional<EEntityForm>());
			Out->bMeasured = true;
			Note(FString::Printf(TEXT("%s: %s keeps %d of %d (%.1f%%), %d traces; procedural keeps %d of %d (%.1f%%), %d traces"), *Label, *FormName(),
				Out->Kept, Out->Points, Out->Points ? 100.0 * Out->Kept / Out->Points : 0.0, Out->Traces, Out->ProceduralKept, Out->ProceduralPoints,
				Out->ProceduralPoints ? 100.0 * Out->ProceduralKept / Out->ProceduralPoints : 0.0, Out->ProceduralTraces));
			return 0.f;
		}));
	}

	static void RestoreResidues()
	{
		for (const TPair<TWeakObjectPtr<AResidue>, FVector>& Moved : G.MovedResidues)
		{
			if (AResidue* Residue = Moved.Key.Get())
			{
				Residue->SetActorLocation(Moved.Value);
			}
		}
		G.MovedResidues.Reset();
	}

	static void Cleanup()
	{
		EntityWallsTest::Restore(TEXT("tls.EntityFormCheck"));
		RestoreResidues();
		GetMutableDefault<UTheLastShiftSettings>()->EntityForms = G.SavedForms;
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->Resume();
		}
		AEntity::SetSessionForm(G.SavedSessionForm);
		if (AEntity* E = FindEntity())
		{
			E->Speed = G.SavedSpeed;
			E->RefreshFigureShape();
		}
		if (URunSubsystem* R = Run())
		{
			R->SetEntityFound(G.bSavedFound, TEXT("tls.EntityFormCheck: end"));
			R->SetFirstContact(G.bSavedContact, TEXT("tls.EntityFormCheck: end"));
		}
		if (UHomeStateSubsystem* H = Home())
		{
			H->RestoreState(G.SavedBattery, G.SavedHome);
		}
		if (GShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(GShotHandle);
			GShotHandle.Reset();
		}
	}

	/** Contact and graze with the current form at the corridor pose: Distance from the figure, then a Resume (graze) or the contact. */
	static void AddContactCase(TArray<FStep>& S, const FString& Label, float Distance, int32 Kind)
	{
		// Kind 0: no contact, Resume, graze expected; 1: no contact, Resume, no graze; 2: contact expected.
		AddStopAt(S, Label, []() { return G.Corridor.Figure; }, [Distance]()
		{
			const FVector Dir = (G.Corridor.Player - G.Corridor.Figure).GetSafeNormal2D();
			FVector Floor = G.Corridor.Figure + Dir * Distance;
			OnNav(Floor, Floor, FVector(40.f, 40.f, 300.f));
			return Floor;
		});
		if (Kind == 2)
		{
			S.Add(WaitFor(Label + TEXT(": the contact ends the Stop"), []() { return !IsStopped(); }, 2.f, [Label, Distance]()
			{
				const AEntity* E = FindEntity();
				Check(E->WasContactThisStop(), FString::Printf(TEXT("%s: a contact at %.0f cm from the figure (ContactRadius %.0f cm), response %s"), *Label, Distance, E->ContactRadius,
					LexToString(E->ContactResponse)));
				return 0.3f;
			}));
			return;
		}
		S.Add(Do(Label + TEXT(": no contact"), [Label, Distance]()
		{
			const AEntity* E = FindEntity();
			const APawn* P = Pawn();
			Check(IsStopped() && !E->WasContactThisStop(), FString::Printf(TEXT("%s: no contact at %.0f cm from the figure (ContactRadius %.0f cm; measured %.0f cm)"), *Label, Distance,
				E->ContactRadius, P ? FVector::Dist(E->GetFigureLocation(), P->GetNavAgentLocation()) : -1.0));
			if (UTimeStateSubsystem* T = TS())
			{
				T->Resume();
			}
			return 0.3f;
		}));
		S.Add(Do(Label + TEXT(": graze"), [Label, Distance, Kind]()
		{
			const AEntity* E = FindEntity();
			const bool bExpected = Kind == 0;
			Check(!IsStopped() && E->WasLastExitGraze() == bExpected, FString::Printf(TEXT("%s: a Resume at path distance %.0f cm (GrazeRadius %.0f cm) is %sa graze (%s)"), *Label,
				E->GetLastExitPathDistance(), E->GrazeRadius, bExpected ? TEXT("") : TEXT("not "), E->WasLastExitGraze() ? TEXT("graze") : TEXT("no graze")));
			return 0.2f;
		}));
	}

	static void AddResetRunState(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("run state reset"), []()
		{
			Run()->SetEntityFound(false, TEXT("tls.EntityFormCheck: next case"));
			Run()->SetFirstContact(false, TEXT("tls.EntityFormCheck: next case"));
			Home()->RestoreState(G.SavedBattery, G.SavedHome);
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
		GRun.Name = TEXT("tls.EntityFormCheck");
		GRun.bQuit = bQuit;
		G = FCheck();
		TArray<FStep>& S = GRun.Steps;

		// --- Setup and criterion 1 (the form) ---
		S.Add(WaitFor(TEXT("setup: an area map, ready, with the player's lidar and an entity"), []()
		{
			const UWorld* World = W();
			const AAreaGenerator* Generator = World ? AAreaGenerator::Find(World) : nullptr;
			const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			return Generator && Generator->IsReady() && (!Flow || !Flow->IsTransitioning()) && Lidar() && Pawn() && FindEntity() && Run() && Home() && AAreaInfo::Find(World);
		}, 30.f, []()
		{
			AEntity* E = FindEntity();
			const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
			G.Act = URunSubsystem::GetActIndexOf(W());
			G.bSavedFound = Run()->IsEntityFound();
			G.bSavedContact = Run()->HasFirstContact();
			G.SavedBattery = Home()->GetBatteryFraction();
			G.SavedHome = Home()->GetHomeSecondsElapsed();
			G.SavedSpeed = E->Speed;
			G.SavedSessionForm = AEntity::GetSessionForm();
			G.SavedForms = Settings->EntityForms;
			GRun.Cleanup = &Cleanup;
			// Brief 30: no entity wall for the run: it counts the injected points of the figure, the trail and the residues.
			EntityWallsTest::SwitchOff(TEXT("tls.EntityFormCheck"));
			Run()->SetEntityFound(false, TEXT("tls.EntityFormCheck: start"));
			Run()->SetFirstContact(false, TEXT("tls.EntityFormCheck: start"));
			E->Speed = 0.f;

			// The act's own form, whatever the session had set.
			SetForm(TOptional<EEntityForm>());
			G.Form = Settings->GetEntityFormOfAct(G.Act);
			const FEntityFormTuning* Tuning = Settings->FindEntityForm(G.Form);
			if (Tuning)
			{
				G.Tuning = *Tuning;
			}
			UStaticMesh* Mesh = Tuning ? Tuning->Mesh.LoadSynchronous() : nullptr;
			Note(FString::Printf(TEXT("map %s, act %d: form %s, mesh %s, %d points; modelled figures %s"), *MapShort(), G.Act, *FormName(), Mesh ? *Mesh->GetPathName() : TEXT("none"),
				G.Tuning.PointCount, Settings->EntityForms.bModelledFigure ? TEXT("on") : TEXT("off")));
			Check(G.Act >= 1 && G.Form != EEntityForm::Procedural && Tuning && Mesh, FString::Printf(TEXT("criterion 1: act %d has a modelled form (%s) with a mesh"), G.Act, *FormName()));
			Check(E->GetRequestedForm() == G.Form && E->GetForm() == G.Form && E->GetFormMesh() == Mesh,
				FString::Printf(TEXT("criterion 1: the entity's figure takes the act's form (requested %s, built %s, mesh %s)"), LexToString(E->GetRequestedForm()), LexToString(E->GetForm()),
					*GetNameSafe(E->GetFormMesh())));
			Check(E->GetFigureShapePoints().Num() == G.Tuning.PointCount && E->GetFigurePointCountNow() == G.Tuning.PointCount,
				FString::Printf(TEXT("criterion 1: %d points sampled (configured %d)"), E->GetFigureShapePoints().Num(), G.Tuning.PointCount));
			if (Mesh)
			{
				TArray<FVector> Vertices;
				TArray<uint32> Indices;
				const bool bRead = FEntityFigureShape::ReadMeshTriangles(Mesh, Vertices, Indices);
				const FBox Bounds = Mesh->GetBoundingBox();
				FBox Sampled(ForceInit);
				for (const FVector& P : E->GetFigureShapePoints())
				{
					Sampled += P;
				}
				G.LookHeight = FMath::Clamp(static_cast<float>(Bounds.GetCenter().Z), 90.f, 300.f);
				Note(FString::Printf(TEXT("mesh %s: %d triangles, %d vertices, bounds %s (size %s cm), CPU access %d; sampled points span %s cm"), *Mesh->GetName(), Indices.Num() / 3,
					Vertices.Num(), *Bounds.ToString(), *Bounds.GetSize().ToString(), Mesh->bAllowCPUAccess ? 1 : 0, *Sampled.GetSize().ToString()));
				const FVector Ratio = Sampled.GetSize() / Bounds.GetSize().ComponentMax(FVector(1.0));
				Check(bRead && Ratio.GetMin() > 0.9 && Ratio.GetMax() < 1.01, FString::Printf(TEXT("criterion 1: the points cover the mesh (span / bounds %.3f %.3f %.3f)"), Ratio.X, Ratio.Y, Ratio.Z));
				Check(Mesh->bAllowCPUAccess, TEXT("the form's mesh keeps its CPU copy (bAllowCPUAccess), so a package can sample it"));
			}

			const AAreaInfo* Info = AAreaInfo::Find(W());
			G.Corridor = FindCorridorPose(Info);
			G.Open = FindOpenPose(Info, G.OpenSix, G.OpenFifteen);
			Check(G.Corridor.bValid, FString::Printf(TEXT("a closed corridor pose: %s"), G.Corridor.bValid ? *G.Corridor.Where : TEXT("none")));
			if (!G.Open.bValid)
			{
				Note(TEXT("no open segment with a pose at 6, 8 and 15 m: the shots of the open space use the longest corridor"));
			}
			Check(true, FString::Printf(TEXT("the open space pose: %s"), G.Open.bValid ? *G.Open.Where : TEXT("none (corridor instead)")));
			return G.Corridor.bValid ? 0.f : -1.f;
		}));

		// --- The corridor at 8 m: criterion 1 (on the mesh, front), 7 (numbers), 2 (nothing dropped), 3 (colours) ---
		AddStopAt(S, TEXT("corridor"), []() { return G.Corridor.Figure; }, []() { return G.Corridor.Player; });
		S.Add(Do(TEXT("corridor: points on the mesh"), []()
		{
			const AEntity* E = FindEntity();
			TArray<FVector> Vertices;
			TArray<uint32> Indices;
			FEntityFigureShape::ReadMeshTriangles(E->GetFormMesh(), Vertices, Indices);
			TArray<FVector> Points;
			E->BuildFigurePoints(Points);
			const FTransform Transform = E->GetFigureTransform();
			float Worst = 0.f;
			for (const FVector& P : Points)
			{
				Worst = FMath::Max(Worst, FEntityFigureShape::DistanceToMesh(Transform.InverseTransformPosition(P), Vertices, Indices));
			}
			Check(Points.Num() == G.Tuning.PointCount && Worst >= 0.f && Worst <= 0.5f,
				FString::Printf(TEXT("criterion 1: all %d points lie on the mesh as it stands at the figure's place (farthest %.3f cm off)"), Points.Num(), Worst));
			const TArray<FVector> Path = E->GetCurrentPath();
			const float PathYaw = Path.Num() >= 2 ? static_cast<float>((Path[1] - Path[0]).Rotation().Yaw) : E->GetFigureYaw();
			const FRotator Rotation = Transform.Rotator();
			const float YawError = FMath::Abs(FRotator::NormalizeAxis(Rotation.Yaw - E->GetFormYawOffset() - PathYaw));
			const FVector Ground = Transform.GetLocation();
			Check(YawError < 1.f && FMath::IsNearlyZero(Rotation.Pitch, 0.01) && FMath::IsNearlyZero(Rotation.Roll, 0.01) && FVector::Dist2D(Ground, E->GetFigureLocation()) < 1.0,
				FString::Printf(TEXT("criterion 1: the mesh upright at the figure's feet (pivot %.1f cm off), its front (+X) along the direction of travel turned by the form's offset (yaw %.1f, offset %.1f, path %.1f)"),
					FVector::Dist2D(Ground, E->GetFigureLocation()), Rotation.Yaw, E->GetFormYawOffset(), PathYaw));
			return 0.f;
		}));

		// Follow-up to Brief 29: the form's YawOffsetDeg at 90 for the session turns the points only.
		S.Add(Do(TEXT("yaw offset 90"), []()
		{
			AEntity* E = FindEntity();
			TArray<FVector> Before;
			E->BuildFigurePoints(Before);
			const FVector PlaceBefore = E->GetFigureLocation();
			const float YawBefore = E->GetFigureYaw();
			const float OffsetBefore = E->GetFormYawOffset();
			const float PathBefore = E->GetPathDistanceToPlayer();
			const float Offset = 90.f;
			for (FEntityFormTuning& Entry : GetMutableDefault<UTheLastShiftSettings>()->EntityForms.Forms)
			{
				if (Entry.Form == G.Form)
				{
					Entry.YawOffsetDeg = Offset;
				}
			}
			TArray<FVector> Vertices;
			TArray<uint32> Indices;
			FEntityFigureShape::ReadMeshTriangles(E->GetFormMesh(), Vertices, Indices);
			TArray<FVector> After;
			E->BuildFigurePoints(After);
			const FTransform Transform = E->GetFigureTransform();
			float Worst = 0.f;
			for (const FVector& P : After)
			{
				Worst = FMath::Max(Worst, FEntityFigureShape::DistanceToMesh(Transform.InverseTransformPosition(P), Vertices, Indices));
			}
			// Each point turned by the offset's change about the vertical through the pivot.
			const FVector Pivot = Transform.GetLocation();
			const FQuat Turn = FRotator(0.f, Offset - OffsetBefore, 0.f).Quaternion();
			float WorstTurn = 0.f;
			for (int32 I = 0; I < FMath::Min(Before.Num(), After.Num()); ++I)
			{
				WorstTurn = FMath::Max(WorstTurn, static_cast<float>(FVector::Dist(Pivot + Turn.RotateVector(Before[I] - Pivot), After[I])));
			}
			Check(E->GetFormYawOffset() == Offset && After.Num() == G.Tuning.PointCount && Worst >= 0.f && Worst <= 0.5f && WorstTurn <= 0.5f,
				FString::Printf(TEXT("yaw offset %.0f: all %d points on the mesh turned by the offset (farthest %.3f cm off the mesh), each the point at offset %.0f turned %.0f degrees about the pivot (farthest %.3f cm)"),
					Offset, After.Num(), Worst, OffsetBefore, Offset - OffsetBefore, WorstTurn));
			const float YawError = FMath::Abs(FRotator::NormalizeAxis(Transform.Rotator().Yaw - (YawBefore + Offset)));
			Check(YawError < 0.01f && E->GetFigureLocation().Equals(PlaceBefore, 0.01) && E->GetFigureYaw() == YawBefore && FMath::IsNearlyEqual(E->GetPathDistanceToPlayer(), PathBefore, 1.f),
				FString::Printf(TEXT("yaw offset %.0f: the mesh's yaw is the travel's plus the offset (%.1f = %.1f + %.0f); the figure's place, its travel's yaw and its path distance unchanged (%.1f cm, then %.1f cm)"),
					Offset, Transform.Rotator().Yaw, YawBefore, Offset, PathBefore, E->GetPathDistanceToPlayer()));
			Lidar()->Scan();
			return 0.4f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_Corridor8m_Yaw90"), *MapShort(), *FormName()); }, nullptr);
		S.Add(Do(TEXT("yaw offset back"), []()
		{
			AEntity* E = FindEntity();
			GetMutableDefault<UTheLastShiftSettings>()->EntityForms = G.SavedForms;
			Check(E->GetLastGatheredFigurePoints() == G.Tuning.PointCount, FString::Printf(TEXT("yaw offset 90: the scan gathered %d figure points; the session's offset back to %.0f"),
				E->GetLastGatheredFigurePoints(), E->GetFormYawOffset()));
			return 0.f;
		}));
		AddScan(S, TEXT("corridor 8 m"));
		S.Add(Do(TEXT("corridor 8 m: gathered"), []()
		{
			const AEntity* E = FindEntity();
			Check(E->GetLastGatheredFigurePoints() == G.Tuning.PointCount, FString::Printf(TEXT("criterion 1: the scan gathered %d figure points (configured %d), %d trail points"),
				E->GetLastGatheredFigurePoints(), G.Tuning.PointCount, E->GetLastGatheredTrailPoints()));
			return 0.f;
		}));
		AddMeasure(S, TEXT("corridor 8 m"), &G.CorridorNumbers);

		// Criterion 2: every residue of the area in the scan's coverage, between the player and the figure.
		S.Add(Do(TEXT("criterion 2: residues into the coverage"), []()
		{
			const FVector Eye = EyeOf(Pawn());
			const FVector Figure = FindEntity()->GetFigureLocation() + FVector(0.f, 0.f, 100.f);
			const FVector Along = (Figure - Eye).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
			int32 Index = 0;
			for (TActorIterator<AResidue> It(W()); It; ++It)
			{
				G.MovedResidues.Emplace(*It, It->GetActorLocation());
				const float T = 250.f + 45.f * Index;
				It->SetActorLocation(Eye + Along * T + Side * ((Index % 3) - 1) * 70.f + FVector(0.f, 0.f, (Index % 2) ? -40.f : 20.f));
				++Index;
			}
			Note(FString::Printf(TEXT("criterion 2: %d residues moved between the player and the figure"), Index));
			Lidar()->Scan();
			return 0.4f;
		}));
		S.Add(Do(TEXT("criterion 2: nothing dropped"), []()
		{
			const ULidarScanComponent* L = Lidar();
			const AEntity* E = FindEntity();
			const FLidarInjectionStats Stats = L->GetLastInjectionStats();
			const FLidarScanContext Context = LatestScan();
			int32 ResiduePoints = 0;
			int32 ResidueMax = 0;
			int32 Residues = 0;
			int32 InCoverage = 0;
			for (const TPair<TWeakObjectPtr<AResidue>, FVector>& Moved : G.MovedResidues)
			{
				if (const AResidue* Residue = Moved.Key.Get())
				{
					++Residues;
					ResiduePoints += Residue->GetLastGatheredPoints();
					ResidueMax += FMath::Clamp(Residue->PointCount, 1, 2000);
					double Depth = 0.0;
					InCoverage += Context.IsInCoverage(Residue->GetActorLocation(), Depth) ? 1 : 0;
				}
			}
			const int32 TrailMax = FMath::FloorToInt32(FMath::Clamp(E->TrailReach, 0.f, 5000.f) / FMath::Clamp(E->TrailPointSpacing, 5.f, 500.f));
			const int32 Cap = FMath::Clamp(L->MaxInjectedPointsPerScan, 0, 65536);
			Note(FString::Printf(TEXT("criterion 2: scan %d: %d sources, %d gathered (figure %d, trail %d, %d residues %d), %d outside coverage, %d over cap, %d uploaded; cap %d"),
				Stats.ScanIndex, Stats.Sources, Stats.Gathered, E->GetLastGatheredFigurePoints(), E->GetLastGatheredTrailPoints(), Residues, ResiduePoints, Stats.OutsideCoverage,
				Stats.OverCap, Stats.Uploaded, Cap));
			Check(Residues > 0 && InCoverage == Residues && E->GetLastGatheredFigurePoints() == G.Tuning.PointCount,
				FString::Printf(TEXT("criterion 2: the figure (%d points) and all %d residues (%d in the coverage) in the scan"), E->GetLastGatheredFigurePoints(), Residues, InCoverage));
			Check(Stats.OverCap == 0 && Stats.Uploaded == Stats.Gathered - Stats.OutsideCoverage,
				FString::Printf(TEXT("criterion 2: no injected point dropped (%d over the cap, %d uploaded of %d gathered, %d outside coverage)"), Stats.OverCap, Stats.Uploaded,
					Stats.Gathered, Stats.OutsideCoverage));
			const int32 Worst = G.Tuning.PointCount + TrailMax + ResidueMax;
			Check(Worst < Cap, FString::Printf(TEXT("criterion 2: the most the area can inject (figure %d + trail %d + residues %d = %d) is under the cap of %d"), G.Tuning.PointCount,
				TrailMax, ResidueMax, Worst, Cap));
			RestoreResidues();
			return 0.f;
		}));

		// Criterion 3: the scan's colour before it is found, its own after, the latest scan only.
		S.Add(Do(TEXT("criterion 3: not found"), []()
		{
			Run()->SetEntityFound(false, TEXT("tls.EntityFormCheck: colours"));
			const AEntity* E = FindEntity();
			const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(const_cast<AEntity*>(E));
			Check(Settings.Color.Equals(UTheLastShiftSettings::GetScanColors().Scan) && Settings.FadeMode == ELidarFadeMode::UntilNextScan && Settings.bOccludeByCapture,
				TEXT("criterion 3: not found, the figure's points in the scan's colour, shown until the next scan (UntilNextScan), hidden by what the capture cannot see"));
			StandOn(G.Corridor.Player, E->GetFigureLocation() + FVector(0.f, 0.f, G.LookHeight));
			Lidar()->Scan();
			return 0.5f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_Corridor8m_NotFound"), *MapShort(), *FormName()); }, [](const FShot& Shot)
		{
			G.NotFoundEntityHue = CountHue(Shot, EHue::Entity);
			Check(G.NotFoundEntityHue <= 20 && CountHue(Shot, EHue::Scan) > 1000, FString::Printf(TEXT("criterion 3: before it is found no pixel in the entity's colour (%d) and the scan's colour on screen (%d)"),
				G.NotFoundEntityHue, CountHue(Shot, EHue::Scan)));
		});
		S.Add(Do(TEXT("criterion 3: found"), []()
		{
			Run()->SetEntityFound(true, TEXT("tls.EntityFormCheck: colours"));
			const AEntity* E = FindEntity();
			const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(const_cast<AEntity*>(E));
			Check(Settings.Color.Equals(UTheLastShiftSettings::GetScanColors().Entity), TEXT("criterion 3: found, the figure's points in the entity's colour"));
			Lidar()->Scan();
			return 0.5f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_Corridor8m"), *MapShort(), *FormName()); }, [](const FShot& Shot)
		{
			G.FoundEntityHue = CountHue(Shot, EHue::Entity);
			Check(G.FoundEntityHue > 300, FString::Printf(TEXT("criterion 3/6: found, the figure on screen in the entity's colour from 8 m in the corridor (%d pixels)"), G.FoundEntityHue));
		});
		S.Add(Do(TEXT("criterion 3: a scan facing away"), []()
		{
			PC()->SetControlRotation(PC()->GetControlRotation() + FRotator(0.f, 180.f, 0.f));
			return 0.15f;
		}));
		S.Add(Do(TEXT("criterion 3: scan away"), []() { Lidar()->Scan(); return 0.9f; }));
		S.Add(Do(TEXT("criterion 3: look back"), []()
		{
			PC()->SetControlRotation(PC()->GetControlRotation() + FRotator(0.f, 180.f, 0.f));
			return 0.3f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_Corridor_AfterNextScan"), *MapShort(), *FormName()); }, [](const FShot& Shot)
		{
			const int32 Left = CountHue(Shot, EHue::Entity);
			Check(Left * 50 <= G.FoundEntityHue, FString::Printf(TEXT("criterion 3: the next scan starts and the figure is gone (%d pixels in the entity's colour, %d before)"), Left, G.FoundEntityHue));
		});
		S.Add(Do(TEXT("corridor: Resume"), []()
		{
			TS()->Resume();
			return 0.3f;
		}));
		AddResetRunState(S);

		// --- The open space: 8 m numbers (criterion 7), shots from 6 and 15 m (criterion 6) ---
		auto OpenFigure = []() { return G.Open.bValid ? G.Open.Figure : G.Corridor.Figure; };
		AddStopAt(S, TEXT("open 8 m"), OpenFigure, []() { return G.Open.bValid ? G.Open.Player : G.Corridor.Player; });
		AddScan(S, TEXT("open 8 m"));
		AddMeasure(S, TEXT("open 8 m"), &G.OpenNumbers);
		S.Add(Do(TEXT("open: found, 6 m"), []()
		{
			Run()->SetEntityFound(true, TEXT("tls.EntityFormCheck: shots"));
			FVector Six = G.OpenSix;
			if (!G.Open.bValid)
			{
				const FVector Dir = (G.Corridor.Player - G.Corridor.Figure).GetSafeNormal2D();
				OnNav(G.Corridor.Figure + Dir * 600.f, Six);
			}
			StandOn(Six, FindEntity()->GetFigureLocation() + FVector(0.f, 0.f, G.LookHeight));
			Lidar()->Scan();
			return 0.5f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_%s6m"), *MapShort(), *FormName(), G.Open.bValid ? TEXT("Open") : TEXT("Corridor")); }, [](const FShot& Shot)
		{
			Check(CountHue(Shot, EHue::Entity) > 300, FString::Printf(TEXT("criterion 6: the figure in the entity's colour from 6 m (%d pixels)"), CountHue(Shot, EHue::Entity)));
		});
		S.Add(Do(TEXT("open: 15 m"), []()
		{
			FVector Fifteen = G.OpenFifteen;
			if (!G.Open.bValid)
			{
				const FVector Dir = (G.Corridor.Player - G.Corridor.Figure).GetSafeNormal2D();
				OnNav(G.Corridor.Figure + Dir * 1500.f, Fifteen);
			}
			StandOn(Fifteen, FindEntity()->GetFigureLocation() + FVector(0.f, 0.f, G.LookHeight));
			Lidar()->Scan();
			return 0.5f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_%s_%s15m"), *MapShort(), *FormName(), G.Open.bValid ? TEXT("Open") : TEXT("Corridor")); }, [](const FShot& Shot)
		{
			Check(CountHue(Shot, EHue::Entity) > 50, FString::Printf(TEXT("criterion 6: the figure in the entity's colour from 15 m (%d pixels)"), CountHue(Shot, EHue::Entity)));
		});
		S.Add(Do(TEXT("open: Resume"), []()
		{
			TS()->Resume();
			return 0.3f;
		}));
		AddResetRunState(S);

		// --- Criterion 4: contact and graze, with the form and with the procedural figure ---
		for (const bool bProcedural : { false, true })
		{
			const FString Which = bProcedural ? TEXT("procedural") : TEXT("form");
			S.Add(Do(Which + TEXT(": the figure's form"), [bProcedural]()
			{
				SetForm(bProcedural ? TOptional<EEntityForm>(EEntityForm::Procedural) : TOptional<EEntityForm>());
				Note(FString::Printf(TEXT("criterion 4 with the %s figure (%s)"), bProcedural ? TEXT("procedural") : TEXT("modelled"), LexToString(FindEntity()->GetForm())));
				return 0.f;
			}));
			const AEntity* E = FindEntity(W());
			const float Contact = E ? FMath::Clamp(E->ContactRadius, 10.f, 1000.f) : 150.f;
			const float Graze = E ? FMath::Clamp(E->GrazeRadius, 0.f, 5000.f) : 400.f;
			AddContactCase(S, Which + TEXT(" outside ContactRadius"), Contact + 40.f, Graze >= Contact + 40.f ? 0 : 1);
			AddResetRunState(S);
			AddContactCase(S, Which + TEXT(" outside GrazeRadius"), Graze + 80.f, 1);
			AddResetRunState(S);
			AddContactCase(S, Which + TEXT(" inside ContactRadius"), Contact - 30.f, 2);
			AddResetRunState(S);
		}
		S.Add(Do(TEXT("criterion 4: the act's form again"), []() { SetForm(TOptional<EEntityForm>()); return 0.f; }));

		// --- Criterion 5: the fallback ---
		AddStopAt(S, TEXT("fallback"), []() { return G.Corridor.Figure; }, []() { return G.Corridor.Player; });
		for (int32 Case = 0; Case < 3; ++Case)
		{
			static const TCHAR* Names[] = { TEXT("modelled figures off"), TEXT("the form's mesh missing"), TEXT("the session's form Procedural") };
			S.Add(Do(FString::Printf(TEXT("criterion 5: %s"), Names[Case]), [Case]()
			{
				UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
				Settings->EntityForms = G.SavedForms;
				if (Case == 0)
				{
					Settings->EntityForms.bModelledFigure = false;
				}
				else if (Case == 1)
				{
					for (FEntityFormTuning& Entry : Settings->EntityForms.Forms)
					{
						if (Entry.Form == G.Form)
						{
							Entry.Mesh.Reset();
						}
					}
				}
				SetForm(Case == 2 ? TOptional<EEntityForm>(EEntityForm::Procedural) : TOptional<EEntityForm>());
				Lidar()->Scan();
				return 0.4f;
			}));
			S.Add(Do(FString::Printf(TEXT("criterion 5: %s, checked"), Names[Case]), [Case]()
			{
				const AEntity* E = FindEntity();
				const int32 Procedural = FMath::Clamp(E->FigurePointCount, 10, 2000) * FMath::Clamp(E->RefractionCount, 1, 8);
				Check(E->GetForm() == EEntityForm::Procedural && !E->GetFormMesh() && E->GetFigurePointCountNow() == Procedural && E->GetLastGatheredFigurePoints() == Procedural,
					FString::Printf(TEXT("criterion 5: %s: the procedural figure (%s), %d points gathered (today's %d)"), Names[Case], LexToString(E->GetForm()),
						E->GetLastGatheredFigurePoints(), Procedural));
				return 0.f;
			}));
		}
		S.Add(Do(TEXT("criterion 5: the settings back"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->EntityForms = G.SavedForms;
			SetForm(TOptional<EEntityForm>());
			const AEntity* E = FindEntity();
			Check(E->GetForm() == G.Form && E->GetFigurePointCountNow() == G.Tuning.PointCount,
				FString::Printf(TEXT("criterion 5: with the settings back, the act's form again (%s, %d points)"), LexToString(E->GetForm()), E->GetFigurePointCountNow()));
			TS()->Resume();
			return 0.3f;
		}));

		// --- Criterion 7: the numbers ---
		S.Add(Do(TEXT("criterion 7"), []()
		{
			auto Share = [](int32 Kept, int32 Points) { return Points > 0 ? 100.0 * Kept / Points : 0.0; };
			const FNumbers& C = G.CorridorNumbers;
			const FNumbers& O = G.OpenNumbers;
			UE_LOG(LogTheLastShift, Display, TEXT("tls.EntityFormCheck FORM NUMBERS %s %s (%d points): corridor 8 m kept %.1f%% (%d of %d), %d traces; open 8 m kept %.1f%% (%d of %d), %d traces | procedural (%d points): corridor %.1f%%, %d traces; open %.1f%%, %d traces | open space: %s"),
				*MapShort(), *FormName(), C.Points, Share(C.Kept, C.Points), C.Kept, C.Points, C.Traces, Share(O.Kept, O.Points), O.Kept, O.Points, O.Traces, C.ProceduralPoints,
				Share(C.ProceduralKept, C.ProceduralPoints), C.ProceduralTraces, Share(O.ProceduralKept, O.ProceduralPoints), O.ProceduralTraces,
				G.Open.bValid ? *G.Open.Where : TEXT("none (the corridor)"));
			Check(C.bMeasured && O.bMeasured, TEXT("criterion 7: the share kept and the traces measured in the corridor and the open space"));
			return 0.f;
		}));

		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started (%d steps)."), *GRun.Name, GRun.Steps.Num());
	}

	// =====================================================================================================
	// tls.Entity.Show
	// =====================================================================================================

	static void Show(UWorld* World)
	{
		AEntity* Entity = FindEntity(World);
		UTimeStateSubsystem* Time = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* P = Controller ? Controller->GetPawn() : nullptr;
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!Entity || !Time || !P || !Nav)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Entity.Show: no entity, time state, player or navigation in this world."));
			return;
		}
		if (!Time->IsStopped())
		{
			Time->Stop();
		}
		if (!Entity->IsManifested() && !Entity->SpawnNow(800.f))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Entity.Show: the entity could not appear."));
			return;
		}
		FVector Eye;
		FRotator View;
		Controller->GetPlayerViewPoint(Eye, View);
		const FVector Forward = FVector(View.Vector().X, View.Vector().Y, 0.f).GetSafeNormal();
		const FVector Side(-Forward.Y, Forward.X, 0.f);
		const FVector Feet = P->GetNavAgentLocation();
		for (float Distance = 800.f; Distance >= 300.f; Distance -= 50.f)
		{
			for (const float Lateral : { 0.f, 100.f, -100.f, 200.f, -200.f })
			{
				FNavLocation Location;
				if (!Nav->ProjectPointToNavigation(Feet + Forward * Distance + Side * Lateral, Location, FVector(60.f, 60.f, 300.f)))
				{
					continue;
				}
				if (FMath::Abs(FVector::Dist2D(Location.Location, Feet) - Distance) > 80.f || !Entity->CanBeSeenFrom(Eye, Location.Location))
				{
					continue;
				}
				Entity->PlaceFigure(Location.Location, true);
				UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.Show: the figure (%s, %d points) stands %.0f cm in front of the player at %s, still until the Stop ends; scan to see it."),
					LexToString(Entity->GetForm()), Entity->GetFigurePointCountNow(), FVector::Dist2D(Location.Location, Feet), *Location.Location.ToString());
				return;
			}
		}
		UE_LOG(LogTheLastShift, Warning, TEXT("tls.Entity.Show: no navmesh point in sight 3 to 8 m in front of the player: face open floor and try again."));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityFormCheckCommand(
	TEXT("tls.EntityFormCheck"),
	TEXT("tls.EntityFormCheck [Quit=0]: Brief 29 (an area map, PIE; fresh session): the act's form of the entity's figure with its mesh and point count, every point on the mesh at the figure's place with its front along the direction of travel; no injected point dropped with the figure, the trail and every residue in a scan; the scan's colour before it is found and its own after, the latest scan only; contact at ContactRadius and graze at GrazeRadius with the form and the procedural figure; the fallback; the share kept from 8 m in a closed corridor and in the largest open space and the traces (FORM NUMBERS); shots from 6 and 15 m in the open space and 8 m in the corridor to Saved/Screenshots/Brief29."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		EntityFormDebug::StartCheck(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityFormCommand(
	TEXT("tls.Entity.Form"),
	TEXT("tls.Entity.Form [Four|Vast|Wheels|Echo|Procedural|Act] [YawOffsetDeg]: Brief 29, sets the entity's figure form for the session (Act: each act's own again) and, given, the form's YawOffsetDeg for the session (-180 to 180: only the sampled points turn about the pivot); without an argument, logs it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (Args.Num() > 0)
		{
			EEntityForm Form = EEntityForm::Procedural;
			if (Args[0].Equals(TEXT("Act"), ESearchCase::IgnoreCase))
			{
				AEntity::SetSessionForm(TOptional<EEntityForm>());
			}
			else if (LexTryParseEntityForm(Args[0], Form))
			{
				AEntity::SetSessionForm(Form);
				if (Args.Num() > 1)
				{
					FEntityFormTuning* Entry = GetMutableDefault<UTheLastShiftSettings>()->EntityForms.Forms.FindByPredicate([Form](const FEntityFormTuning& E) { return E.Form == Form; });
					if (Entry)
					{
						Entry->YawOffsetDeg = FMath::Clamp(FCString::Atof(*Args[1]), -180.f, 180.f);
						UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.Form: %s's YawOffsetDeg set to %.1f for the session (ini line: (Form=%s,...,YawOffsetDeg=%.1f))."), LexToString(Form),
							Entry->YawOffsetDeg, LexToString(Form), Entry->YawOffsetDeg);
					}
					else
					{
						UE_LOG(LogTheLastShift, Warning, TEXT("tls.Entity.Form: %s has no entry in EntityForms: no offset to set."), LexToString(Form));
					}
				}
			}
			else
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("tls.Entity.Form: unknown form '%s' (Four, Vast, Wheels, Echo, Procedural or Act)."), *Args[0]);
				return;
			}
		}
		const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
		const TOptional<EEntityForm> Session = AEntity::GetSessionForm();
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Entity.Form: the session's form %s; modelled figures %s; acts 1-4: %s %s %s %s."), Session.IsSet() ? LexToString(Session.GetValue()) : TEXT("unset (each act's own)"),
			Settings->EntityForms.bModelledFigure ? TEXT("on") : TEXT("off"), LexToString(Settings->GetEntityFormOfAct(1)), LexToString(Settings->GetEntityFormOfAct(2)),
			LexToString(Settings->GetEntityFormOfAct(3)), LexToString(Settings->GetEntityFormOfAct(4)));
		for (const FEntityFormTuning& Entry : Settings->EntityForms.Forms)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("  form %s: mesh %s, %d points, YawOffsetDeg %.1f"), LexToString(Entry.Form), *Entry.Mesh.ToString(), Entry.PointCount, Entry.YawOffsetDeg);
		}
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			It->RefreshFigureShape();
			UE_LOG(LogTheLastShift, Log, TEXT("  %s (act %d): requested %s, built %s, mesh %s, %d points, yaw offset %.1f."), *It->GetName(), URunSubsystem::GetActIndexOf(World),
				LexToString(It->GetRequestedForm()), LexToString(It->GetForm()), *GetNameSafe(It->GetFormMesh()), It->GetFigurePointCountNow(), It->GetFormYawOffset());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsEntityShowCommand(
	TEXT("tls.Entity.Show"),
	TEXT("tls.Entity.Show: Brief 29, stops time if needed, awakens the entity and stands its figure 8 m in front of the player (or nearer, where the floor allows), still for the rest of the Stop: scan to see the form."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		EntityFormDebug::Show(TimeStateDebug::ResolveGameWorld(InWorld));
	}));

#endif // !UE_BUILD_SHIPPING

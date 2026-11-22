// SPDX-License-Identifier: MIT

// Brief 17, the entity in the scan's colour until it is found; Brief 20, found by a clear sighting or by contact, and the
// rows said. tls.Run.EntityFound [0|1] and tls.Run.FirstContact [0|1] set, clear or log the run's state.
// tls.EntityFoundTest [Quit] [Shots] (an area map, Area2_ServerHalls in the reports: its contact is a jump) counts the
// exact share of the figure every scan keeps (bMeasureFullShare): the entity dormant, outside the scan's coverage, behind
// geometry (the game's figure count against the lidar's own depth test, bVerifyInjectedOcclusion, with the residues out of
// the scans so that the entity is the only injected source), with only points of its trail kept in three scans, with half
// of the figure or less kept in every scan, with more than half in one scan only, in two scans not in a row, and in the
// last scan of one Stop and the first of the next: not found, nothing said, the scan's colour at the next Stop. Two scans
// in a row with more than half: still the scan's colour for the rest of the Stop (shot), found on the Resume, the
// engineer's EntitySeen row then the EntityFound row; from the next Stop the figure and the trail in the entity's colour
// (shot). The first contact after that: the return message and the EntityContact row; a second contact: the return
// message alone. A graze finds nothing. Found by contact with each response (Rewind, Jump, RewindAndJump): the return
// message when there is one, EntityContact, EntityFound, and the entity's colour at the next Stop. A clear sighting and the
// first contact in one Stop: the contact's sequence. tls.StillCheck [Quit] (Area2_ServerHalls): the {still} number of a
// contact jump and of a graze jump against the Stop's length on the run's clock and in real time. tls.FoundRunTest [Quit]
// (MainMenu): both states across travel, in the snapshots and the save (version 4), with Continue, restarts and New game;
// a version 3 file, a version 2 file and a file with no Version load with the first contact not happened. The messages are
// the test's fixture rows. Shots go to Saved/Screenshots/Brief20. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Debug/HomeTestListener.h"
#include "Debug/NarrativeFixture.h"
#include "Debug/TextTestListener.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "LidarPointSource.h"
#include "LidarScanComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Residue/Residue.h"
#include "Run/RunSaveGame.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TextChannel/TextChannelComponent.h"
#include "TextChannel/TextTokens.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"

#if !UE_BUILD_SHIPPING

namespace EntityFoundDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner on the core ticker (it survives map travel) ---

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
		double LastTickReal = -1.0;
		bool bRunning = false;
		bool bQuit = false;
		bool bSaveShots = true;
		TFunction<void()> Cleanup;
		TFunction<void(double)> PerTick;
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
		GRun.PerTick = nullptr;
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls Brief 20 test"));
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
		if (GRun.PerTick && W())
		{
			GRun.PerTick(GRun.LastTickReal >= 0.0 ? Now - GRun.LastTickReal : 0.0);
		}
		GRun.LastTickReal = Now;
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
			// A travel between maps leaves no world for a few frames: a condition is not asked then.
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

	static void StartRun(const FString& Name, bool bQuit, bool bShots)
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.Name = Name;
		GRun.bQuit = bQuit;
		GRun.bSaveShots = bShots;
	}

	static void Launch()
	{
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started (%d steps)."), *GRun.Name, GRun.Steps.Num());
	}

	// --- World access ---

	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static UTextChannelComponent* Channel() { return Pawn() ? Pawn()->FindComponentByClass<UTextChannelComponent>() : nullptr; }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static FString MapName() { const UWorld* World = W(); return World ? World->GetOutermost()->GetName() : FString(); }

	static AEntity* FindEntity()
	{
		UWorld* World = W();
		for (TActorIterator<AEntity> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static bool Transitioning()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		return Flow && Flow->IsTransitioning();
	}

	static bool AreaReady(int32 Act)
	{
		const UWorld* World = W();
		const AAreaGenerator* Generator = World ? AAreaGenerator::Find(World) : nullptr;
		return World && URunSubsystem::GetActIndexOf(World) == Act && Generator && Generator->IsReady() && !Transitioning() && Pawn();
	}

	static FVector EyeOf(const APawn* P)
	{
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() : (P ? P->GetActorLocation() + FVector(0.f, 0.f, 62.f) : FVector::ZeroVector);
	}

	static void StopTime()
	{
		if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped())
		{
			T->Stop();
		}
	}

	static void ResumeTime()
	{
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->Resume();
		}
	}

	static void Look(const FVector& At)
	{
		if (APlayerController* C = PC())
		{
			C->SetControlRotation((At - EyeOf(Pawn())).Rotation());
		}
	}

	/** The pawn standing on the navmesh point Floor, looking at LookAt. */
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
		Look(LookAt);
	}

	/** A navmesh point MinDist..MaxDist from the figure from which an eye at the pawn's height sees it (the spawn's own test). */
	static bool FindSeeingFloor(const AEntity* Entity, float MinDist, float MaxDist, FVector& OutFloor)
	{
		UWorld* World = W();
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		if (!Entity || !Nav || !Character)
		{
			return false;
		}
		const FVector Figure = Entity->GetFigureLocation();
		const float EyeAbove = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f + (EyeOf(Character).Z - Character->GetActorLocation().Z);
		for (float Radius = MinDist; Radius <= MaxDist; Radius += 50.f)
		{
			for (int32 Step = 0; Step < 24; ++Step)
			{
				const float Angle = Step * (2.f * UE_PI / 24.f);
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Figure + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius, OnNav, FVector(40.f, 40.f, 300.f)))
				{
					continue;
				}
				if (FVector::Dist2D(OnNav.Location, Figure) < MinDist - 1.f || FMath::Abs(OnNav.Location.Z - Figure.Z) > 150.f)
				{
					continue;
				}
				if (Entity->CanBeSeenFrom(OnNav.Location + FVector(0.f, 0.f, EyeAbove), Figure))
				{
					OutFloor = OnNav.Location;
					return true;
				}
			}
		}
		return false;
	}

	/** A navmesh point MinDist..MaxDist from the figure from which the figure is NOT in sight (geometry between). */
	static bool FindHiddenFloor(const AEntity* Entity, float MinDist, float MaxDist, FVector& OutFloor)
	{
		UWorld* World = W();
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		if (!Entity || !Nav || !Character)
		{
			return false;
		}
		const FVector Figure = Entity->GetFigureLocation();
		const float EyeAbove = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f + (EyeOf(Character).Z - Character->GetActorLocation().Z);
		for (float Radius = MinDist; Radius <= MaxDist; Radius += 100.f)
		{
			for (int32 Step = 0; Step < 24; ++Step)
			{
				const float Angle = Step * (2.f * UE_PI / 24.f);
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Figure + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius, OnNav, FVector(40.f, 40.f, 300.f)))
				{
					continue;
				}
				if (FVector::Dist2D(OnNav.Location, Figure) < MinDist - 1.f || FMath::Abs(OnNav.Location.Z - Figure.Z) > 150.f)
				{
					continue;
				}
				if (!Entity->CanBeSeenFrom(OnNav.Location + FVector(0.f, 0.f, EyeAbove), Figure))
				{
					OutFloor = OnNav.Location;
					return true;
				}
			}
		}
		return false;
	}

	// --- Shots, measured on their pixels ---

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

	/** The hue classes of OcclusionDebugCommands (8-bit sRGB after tonemapping): the scan's cyan, the entity's red. */
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

	static int32 CountHue(const FShot& Shot, EHue Hue, const FBox2D& Rect)
	{
		if (Shot.Pixels.Num() < Shot.Width * Shot.Height)
		{
			return -1;
		}
		const int32 X0 = FMath::Clamp(FMath::FloorToInt32(Rect.Min.X * Shot.Width), 0, Shot.Width);
		const int32 X1 = FMath::Clamp(FMath::CeilToInt32(Rect.Max.X * Shot.Width), 0, Shot.Width);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(Rect.Min.Y * Shot.Height), 0, Shot.Height);
		const int32 Y1 = FMath::Clamp(FMath::CeilToInt32(Rect.Max.Y * Shot.Height), 0, Shot.Height);
		int32 Count = 0;
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				Count += IsHue(Shot.Pixels[Y * Shot.Width + X], Hue) ? 1 : 0;
			}
		}
		return Count;
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		GShot.Width = Width;
		GShot.Height = Height;
		GShot.Pixels = Pixels;
		if (GRun.bSaveShots)
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief20"), GShot.Label + TEXT(".png")));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Png, *Path);
			Note(FString::Printf(TEXT("shot %s saved (%d x %d): %s"), *GShot.Label, Width, Height, *Path));
		}
		bShotReady = true;
	}

	static void AddShot(TArray<FStep>& S, const FString& Label, TFunction<void(const FShot&)> Then)
	{
		S.Add(Do(FString::Printf(TEXT("shot %s"), *Label), [Label]()
		{
			if (!GShotHandle.IsValid())
			{
				GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
			}
			GShot = FShot();
			GShot.Label = Label;
			bShotReady = false;
			FScreenshotRequest::RequestScreenshot(FString(), false, false);
			return 0.f;
		}));
		S.Add(WaitFor(FString::Printf(TEXT("shot %s taken"), *Label), []() { return bShotReady; }, 5.f, [Then]()
		{
			if (Then)
			{
				Then(GShot);
			}
			return 0.f;
		}));
	}

	static void ReleaseShots()
	{
		if (GShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(GShotHandle);
			GShotHandle.Reset();
		}
	}

	/** The figure's box on screen (fractions of the viewport): its feet to its head, padded. */
	static FBox2D FigureRect(const AEntity* Entity)
	{
		const APlayerController* C = PC();
		FVector2D Viewport(1.f, 1.f);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(Viewport);
		}
		FVector2D Feet, Head;
		if (!C || !Entity || !C->ProjectWorldLocationToScreen(Entity->GetFigureLocation(), Feet, false)
			|| !C->ProjectWorldLocationToScreen(Entity->GetFigureLocation() + FVector(0.f, 0.f, 180.f), Head, false))
		{
			return FBox2D(FVector2D(0.3, 0.2), FVector2D(0.7, 0.9));
		}
		const double Height = FMath::Max(FMath::Abs(Feet.Y - Head.Y), 40.0);
		const FVector2D Centre = (Feet + Head) * 0.5;
		const FVector2D Half(Height * 0.9, Height * 0.75);
		return FBox2D((Centre - Half) / Viewport, (Centre + Half) / Viewport);
	}

	// =====================================================================================================
	// tls.Run.EntityFound
	// =====================================================================================================

	static FAutoConsoleCommandWithWorldAndArgs GEntityFoundCommand(
		TEXT("tls.Run.EntityFound"),
		TEXT("tls.Run.EntityFound [0|1]: clears or sets the run's entity-found state (without its message); without an argument, logs it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* InWorld)
		{
			UWorld* World = ResolveGameWorld(InWorld);
			URunSubsystem* RunSub = URunSubsystem::Get(World);
			if (!RunSub)
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("tls.Run.EntityFound: no run subsystem."));
				return;
			}
			if (Args.Num() > 0)
			{
				RunSub->SetEntityFound(FCString::Atoi(*Args[0]) != 0, TEXT("tls.Run.EntityFound"));
			}
			const AEntity* Entity = nullptr;
			for (TActorIterator<AEntity> It(World); World && It; ++It)
			{
				Entity = *It;
				break;
			}
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Run.EntityFound: %s; entity %s, points in %s."), RunSub->IsEntityFound() ? TEXT("found") : TEXT("not found"),
				*GetNameSafe(Entity), Entity ? *Entity->GetPointColor().ToString() : TEXT("-"));
		}));

	// =====================================================================================================
	// tls.EntityFoundTest
	// =====================================================================================================

	struct FFoundTest
	{
		TStrongObjectPtr<UTlsTextTestListener> Text;
		TStrongObjectPtr<UTlsHomeTestListener> HomeListener;
		TArray<TWeakObjectPtr<AResidue>> MovedResidues;
		bool bSavedFound = false;
		bool bSavedContact = false;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		float SavedSpeed = 66.6667f;
		float SavedGraze = 400.f;
		EEntityContactResponse SavedResponse = EEntityContactResponse::Rewind;
		bool bSavedVerify = false;
		int32 StartedMark = 0;
		int32 JumpsMark = 0;
		/** From the pose: looking at the figure, and a view that keeps half of the figure or less. */
		FRotator FullRot = FRotator::ZeroRotator;
		FRotator PartialRot = FRotator::ZeroRotator;
		bool bPartial = false;
		int32 ProbeState = 0;
		float ProbeOffset = 0.f;
		double ProbeAt = 0.0;
		int32 ProbeScans = 0;
		double ProbeShare = 0.0;
		FLidarSourceSettings NotFoundSettings;
		int32 NotFoundFigurePoints = -1;
		int32 NotFoundScanHue = -1;
		/** An attempted case (behind geometry, trail only) was met; the scans of the attempt. */
		bool bCaseDone = false;
		int32 AttemptFigureKept = 0;
		int32 AttemptLidarKept = 0;
		int32 AttemptScans = 0;
	};
	static FFoundTest GFound;

	struct FBehind
	{
		bool bDone = false;
		int32 AutoKept = -1;
		int32 ManualKept = -1;
		int32 AutoGame = -1;
		bool bAutoChecked = false;
		int32 AutoTraces = 0;
		int32 AutoScan = -1;
	};
	static FBehind GBehind;

	static int32 StartedSince(int32 Mark, const TCHAR* Contains)
	{
		int32 Count = 0;
		const UTlsTextTestListener* L = GFound.Text.Get();
		for (int32 I = Mark; L && I < L->Started.Num(); ++I)
		{
			Count += L->Started[I].ToString().Contains(Contains) ? 1 : 0;
		}
		return Count;
	}

	static FString StartedRows(int32 Mark)
	{
		FString Out;
		const UTlsTextTestListener* L = GFound.Text.Get();
		for (int32 I = Mark; L && I < L->Started.Num(); ++I)
		{
			const ETextVoice Voice = L->StartedVoices.IsValidIndex(I) ? L->StartedVoices[I] : ETextVoice::Home;
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + L->Started[I].ToString() + (Voice == ETextVoice::Engineer ? TEXT("(engineer)") : Voice == ETextVoice::Twin ? TEXT("(twin)") : TEXT(""));
		}
		return Out.IsEmpty() ? TEXT("none") : Out;
	}

	/** Injected points the lidar kept in its last scan: uploaded less those its depth test removes (only the entity injects). */
	static int32 LidarKept()
	{
		const ULidarScanComponent* L = Lidar();
		if (!L)
		{
			return -1;
		}
		const FLidarInjectionStats Stats = L->GetLastInjectionStats();
		// Nothing uploaded: the lidar skips its depth readback, and nothing is kept.
		return Stats.Uploaded == 0 ? 0 : (Stats.BehindCapturedDepth < 0 ? -1 : Stats.Uploaded - Stats.BehindCapturedDepth);
	}

	/** The game's check ran on the lidar's last scan. */
	static bool CheckedLastScan()
	{
		const AEntity* E = FindEntity();
		const ULidarScanComponent* L = Lidar();
		return E && L && E->GetLastKeptCheckScan() != INDEX_NONE && E->GetLastKeptCheckScan() == L->GetLastInjectionStats().ScanIndex;
	}

	/** Share of the figure's points the last check kept (exact: the test sets bMeasureFullShare). */
	static double LastShare()
	{
		const AEntity* E = FindEntity();
		return E && E->GetLastKeptCheckFigurePoints() > 0 ? static_cast<double>(E->GetLastKeptCheckKept()) / E->GetLastKeptCheckFigurePoints() : 0.0;
	}

	static FString LastScanText()
	{
		const ULidarScanComponent* L = Lidar();
		const AEntity* E = FindEntity();
		const FLidarInjectionStats Stats = L ? L->GetLastInjectionStats() : FLidarInjectionStats();
		return FString::Printf(TEXT("scan %d: lidar gathered %d, outside coverage %d, uploaded %d, behind captured depth %d; game check scan %d: figure kept %d of %d (%.1f%%), clear %d, %d traces, %.3f ms; %d in a row, sighted %d"),
			Stats.ScanIndex, Stats.Gathered, Stats.OutsideCoverage, Stats.Uploaded, Stats.BehindCapturedDepth, E ? E->GetLastKeptCheckScan() : -1,
			E ? E->GetLastKeptCheckKept() : -1, E ? E->GetLastKeptCheckFigurePoints() : -1, LastShare() * 100.0, E && E->GetLastKeptCheckResult() ? 1 : 0,
			E ? E->GetLastKeptCheckTraces() : -1, E ? E->GetLastKeptCheckMs() : -1.0, E ? E->GetSightingStreak() : -1, E && E->WasSightedThisStop() ? 1 : 0);
	}

	/** All of the figure's sample heights are in sight from Eye (traces on the visibility channel, the player and the entity ignored). */
	static bool AllHeightsInSight(const FVector& Eye, const FVector& Figure)
	{
		UWorld* World = W();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsEntityFoundSight), false, FindEntity());
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

	/** A navmesh point MinDist..MaxDist from the figure from which every height of the figure is in sight. */
	static bool FindClearFloor(const AEntity* Entity, float MinDist, float MaxDist, FVector& OutFloor)
	{
		UWorld* World = W();
		const UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		if (!Entity || !Nav || !Character)
		{
			return false;
		}
		const FVector Figure = Entity->GetFigureLocation();
		const float EyeAbove = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f + (EyeOf(Character).Z - Character->GetActorLocation().Z);
		for (float Radius = MinDist; Radius <= MaxDist; Radius += 50.f)
		{
			for (int32 Step = 0; Step < 24; ++Step)
			{
				const float Angle = Step * (2.f * UE_PI / 24.f);
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Figure + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius, OnNav, FVector(40.f, 40.f, 300.f)))
				{
					continue;
				}
				if (FVector::Dist2D(OnNav.Location, Figure) < MinDist - 1.f || FMath::Abs(OnNav.Location.Z - Figure.Z) > 150.f)
				{
					continue;
				}
				if (AllHeightsInSight(OnNav.Location + FVector(0.f, 0.f, EyeAbove), Figure))
				{
					OutFloor = OnNav.Location;
					return true;
				}
			}
		}
		return false;
	}

	static void MarkCase()
	{
		GFound.StartedMark = GFound.Text->Started.Num();
		GFound.JumpsMark = GFound.HomeListener->Jumps;
		Home()->RestoreState(GFound.SavedBattery, GFound.SavedHome);
	}

	/** Waits for the channel to be idle, then marks the rows and jumps from here. */
	static void AddCaseStart(TArray<FStep>& S, const FString& Label, TFunction<void()> Then = nullptr)
	{
		S.Add(WaitFor(Label + TEXT(": the channel is idle"), []() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }, 60.f, [Then]()
		{
			MarkCase();
			if (Then)
			{
				Then();
			}
			return 0.2f;
		}));
	}

	/** Stop with the entity awake and manifested (SpawnNow the first time), held still. */
	static void AddStopManifested(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": Stop"), [Label]()
		{
			Home()->RestoreState(GFound.SavedBattery, GFound.SavedHome);
			StopTime();
			AEntity* E = FindEntity();
			if (E && !E->IsManifested())
			{
				E->SpawnNow(800.f);
			}
			if (E)
			{
				E->Speed = 0.f;
			}
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": the entity is manifested"), []() { const AEntity* E = FindEntity(); return E && E->IsManifested() && IsStopped(); }, 5.f));
	}

	/** Stands where every height of the figure is in sight MinDist..MaxDist away, looking at it (FullRot). */
	static float StandClear(const FString& Label, float MinDist, float MaxDist)
	{
		const AEntity* E = FindEntity();
		FVector Floor;
		bool bFound = FindClearFloor(E, MinDist, MaxDist, Floor);
		if (!bFound)
		{
			bFound = FindClearFloor(E, MinDist, MaxDist * 2.f, Floor);
		}
		Check(bFound, FString::Printf(TEXT("%s: a pose %.0f..%.0f cm from the figure that sees all of it"), *Label, MinDist, MaxDist));
		if (!bFound)
		{
			return -1.f;
		}
		StandOn(Floor, E->GetFigureLocation() + FVector(0.f, 0.f, 90.f));
		GFound.FullRot = (E->GetFigureLocation() + FVector(0.f, 0.f, 90.f) - EyeOf(Pawn())).Rotation();
		PC()->SetControlRotation(GFound.FullRot);
		return 0.2f;
	}

	/** After the automatic scan, stands where all of the figure is in sight. */
	static void AddClearPose(TArray<FStep>& S, const FString& Label, float MinDist, float MaxDist)
	{
		S.Add(WaitFor(Label + TEXT(": the automatic scan has fired"), []() { const UScanDirectorComponent* D = Director(); return D && D->GetScansThisStop() >= 1; }, 4.f, [Label, MinDist, MaxDist]()
		{
			const AEntity* E = FindEntity();
			Note(FString::Printf(TEXT("%s: the automatic scan, from where the figure appeared: %s"), *Label, *LastScanText()));
			Check(!E->WasSightedThisStop(), FString::Printf(TEXT("%s: one scan is no clear sighting (%d in a row)"), *Label, E->GetSightingStreak()));
			return StandClear(Label, MinDist, MaxDist);
		}));
		// The automatic scan may itself have kept more than half of the figure: a scan facing away starts the row again,
		// so that the case's own scans are counted from 0.
		S.Add(Do(Label + TEXT(": a row that starts at 0"), []()
		{
			const AEntity* E = FindEntity();
			if (E->GetSightingStreak() == 0)
			{
				return 0.f;
			}
			PC()->SetControlRotation(GFound.FullRot + FRotator(0.f, 180.f, 0.f));
			return 0.12f;
		}));
		S.Add(Do(Label + TEXT(": a scan facing away"), []()
		{
			const AEntity* E = FindEntity();
			if (E->GetSightingStreak() == 0)
			{
				return 0.f;
			}
			Note(FString::Printf(TEXT("the automatic scan was a clear sighting (%d in a row): one scan facing away"), E->GetSightingStreak()));
			Lidar()->Scan();
			return 0.35f;
		}));
		S.Add(Do(Label + TEXT(": the row"), [Label]()
		{
			const AEntity* E = FindEntity();
			Check(E->GetSightingStreak() == 0 && !E->WasSightedThisStop(), FString::Printf(TEXT("%s: the case's scans start with 0 in a row (%s)"), *Label, *LastScanText()));
			PC()->SetControlRotation(GFound.FullRot);
			return 0.12f;
		}));
	}

	/**
	 * One scan looking at the figure ('F') or with the view of the probe ('P'); checks that the game's check ran, its share
	 * (more than ClearSightingShare for F, at most it for P), the count of scans in a row and the sighting.
	 */
	static void AddLookScan(TArray<FStep>& S, const FString& Label, TCHAR Kind, int32 ExpectStreak, bool bExpectSighted)
	{
		S.Add(Do(Label + TEXT(": look"), [Kind]() { PC()->SetControlRotation(Kind == TEXT('F') ? GFound.FullRot : GFound.PartialRot); return 0.12f; }));
		S.Add(Do(Label + TEXT(": scan"), []() { Lidar()->Scan(); return 0.35f; }));
		S.Add(Do(Label, [Label, Kind, ExpectStreak, bExpectSighted]()
		{
			const AEntity* E = FindEntity();
			const double Share = LastShare();
			const bool bClear = Share > E->ClearSightingShare;
			const bool bKind = Kind == TEXT('F') ? bClear : !bClear;
			Check(CheckedLastScan() && bKind && E->GetLastKeptCheckResult() == bClear && E->GetSightingStreak() == ExpectStreak && E->WasSightedThisStop() == bExpectSighted,
				FString::Printf(TEXT("%s: %s, %.1f%% of the figure kept (more than %.0f%%: %d); %d in a row (expected %d), sighted %d (expected %d) [%s]"), *Label,
					Kind == TEXT('F') ? TEXT("looking at the figure") : TEXT("half of it or less in the coverage"), Share * 100.0, E->ClearSightingShare * 100.f, bClear ? 1 : 0,
					E->GetSightingStreak(), ExpectStreak, E->WasSightedThisStop() ? 1 : 0, bExpectSighted ? 1 : 0, *LastScanText()));
			return 0.f;
		}));
	}

	/** Turns away from the figure and back in steps until a scan keeps some of the figure but half of it or less (PartialRot). */
	static void AddPartialProbe(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": probe"), []()
		{
			GFound.ProbeState = 0;
			GFound.ProbeOffset = 70.f;
			GFound.ProbeScans = 0;
			GFound.bPartial = false;
			GFound.ProbeShare = 0.0;
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": a view that keeps some of the figure but half of it or less"), []()
		{
			const double Now = Real();
			if (GFound.ProbeState == 0)
			{
				PC()->SetControlRotation(GFound.FullRot + FRotator(0.f, GFound.ProbeOffset, 0.f));
				GFound.ProbeAt = Now + 0.12;
				GFound.ProbeState = 1;
				return false;
			}
			if (Now < GFound.ProbeAt)
			{
				return false;
			}
			if (GFound.ProbeState == 1)
			{
				Lidar()->Scan();
				++GFound.ProbeScans;
				GFound.ProbeAt = Now + 0.35;
				GFound.ProbeState = 2;
				return false;
			}
			const AEntity* E = FindEntity();
			GFound.ProbeShare = LastShare();
			if (CheckedLastScan() && GFound.ProbeShare > 0.0 && GFound.ProbeShare <= E->ClearSightingShare)
			{
				GFound.PartialRot = GFound.FullRot + FRotator(0.f, GFound.ProbeOffset, 0.f);
				GFound.bPartial = true;
				return true;
			}
			if (GFound.ProbeShare > E->ClearSightingShare || GFound.ProbeOffset <= 0.f)
			{
				return true;
			}
			GFound.ProbeOffset -= 1.5f;
			GFound.ProbeState = 0;
			return false;
		}, 60.f, [Label]()
		{
			const AEntity* E = FindEntity();
			Check(GFound.bPartial && E->GetSightingStreak() == 0 && !E->WasSightedThisStop(), FString::Printf(TEXT("%s: %.1f deg off the figure, %.1f%% of it kept, after %d scans that kept half of it or less (%d in a row) [%s]"),
				*Label, GFound.ProbeOffset, GFound.ProbeShare * 100.0, GFound.ProbeScans, E->GetSightingStreak(), *LastScanText()));
			return GFound.bPartial ? 0.f : -1.f;
		}));
	}

	/** Resumes: not found, no first contact, nothing said; at the next Stop its points are still in the scan's colour. */
	static void AddNotFoundEnd(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": Resume"), [Label]()
		{
			ResumeTime();
			Check(!Run()->IsEntityFound() && !Run()->HasFirstContact(), FString::Printf(TEXT("%s: the Stop ended, the entity is not found"), *Label));
			return 0.5f;
		}));
		S.Add(Do(Label + TEXT(": the next Stop"), [Label]()
		{
			StopTime();
			AEntity* E = FindEntity();
			const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(E);
			Check(E->IsManifested() && Settings.Color.Equals(UTheLastShiftSettings::GetScanColors().Scan, 1.e-4f),
				FString::Printf(TEXT("%s: at the next Stop its points are still in the scan's colour %s"), *Label, *Settings.Color.ToString()));
			ResumeTime();
			return 2.5f;
		}));
		S.Add(Do(Label + TEXT(": nothing said"), [Label]()
		{
			Check(StartedSince(GFound.StartedMark, TEXT("Entity")) == 0 && GFound.HomeListener->Jumps == GFound.JumpsMark,
				FString::Printf(TEXT("%s: nothing said about it (rows since the case began: %s)"), *Label, *StartedRows(GFound.StartedMark)));
			Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: next case"));
			Run()->SetFirstContact(false, TEXT("tls.EntityFoundTest: next case"));
			return 0.f;
		}));
	}

	/**
	 * Waits until the rows of the case have started and the channel is idle, then checks them in order: each entry of Want
	 * is a row name's start (A<act>_<Trigger>_), "(engineer)" after it for the engineer's voice.
	 */
	static void AddExpectRows(TArray<FStep>& S, const FString& Label, const TArray<FString>& Want)
	{
		S.Add(WaitFor(Label + TEXT(": the rows"), [Want]()
		{
			const UTextChannelComponent* C = Channel();
			return C && !C->IsBusy() && GFound.Text->Started.Num() - GFound.StartedMark >= Want.Num();
		}, 60.f, [Label, Want]()
		{
			const UTlsTextTestListener* L = GFound.Text.Get();
			const int32 Act = URunSubsystem::GetActIndexOf(W());
			bool bOk = L->Started.Num() - GFound.StartedMark == Want.Num();
			FString Expected;
			for (int32 I = 0; I < Want.Num(); ++I)
			{
				FString Prefix = Want[I];
				const bool bEngineer = Prefix.RemoveFromEnd(TEXT("(engineer)"));
				Prefix = FString::Printf(TEXT("A%d_%s_"), Act, *Prefix);
				Expected += (I ? TEXT(" ") : TEXT("")) + Prefix + TEXT("*") + (bEngineer ? TEXT("(engineer)") : TEXT(""));
				const int32 At = GFound.StartedMark + I;
				bOk &= L->Started.IsValidIndex(At) && L->Started[At].ToString().StartsWith(Prefix)
					&& L->StartedVoices.IsValidIndex(At) && (L->StartedVoices[At] == ETextVoice::Engineer) == bEngineer;
			}
			Check(bOk, FString::Printf(TEXT("%s: on the return to Normal time, in order and once each: %s (expected %s)"), *Label, *StartedRows(GFound.StartedMark), *Expected));
			return 0.f;
		}));
	}

	/** Stop, the figure at the minimum distance and the pawn on it, with the response Response: the contact ends the Stop. */
	static void AddContact(TArray<FStep>& S, const FString& Label, EEntityContactResponse Response, bool bStopFirst)
	{
		S.Add(Do(Label + TEXT(": contact"), [Label, Response, bStopFirst]()
		{
			AEntity* E = FindEntity();
			APawn* P = Pawn();
			E->ContactResponse = Response;
			if (bStopFirst)
			{
				Home()->RestoreState(GFound.SavedBattery, GFound.SavedHome);
				StopTime();
				E->SpawnNow(E->MinSpawnDistance);
				E->Speed = 0.f;
			}
			const float FeetToActor = P->GetActorLocation().Z - P->GetNavAgentLocation().Z;
			P->SetActorLocation(E->GetFigureLocation() + FVector(0.f, 0.f, FeetToActor + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			Note(FString::Printf(TEXT("%s: the player on the figure, response %s"), *Label, LexToString(Response)));
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": the contact ends the Stop"), []() { return !IsStopped(); }, 10.f, [Label]()
		{
			const AEntity* E = FindEntity();
			Check(E->WasContactThisStop() && Run()->HasFirstContact() && Run()->IsEntityFound(),
				FString::Printf(TEXT("%s: the Stop ended with a contact: first contact happened, the entity is found"), *Label));
			return 0.f;
		}));
	}

	/** A short Stop: the entity's points are in its own colour. */
	static void AddOwnColourNextStop(TArray<FStep>& S, const FString& Label)
	{
		S.Add(WaitFor(Label + TEXT(": the channel is idle"), []() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }, 60.f, [Label]()
		{
			Home()->RestoreState(GFound.SavedBattery, GFound.SavedHome);
			StopTime();
			AEntity* E = FindEntity();
			const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(E);
			Check(E->IsManifested() && Settings.Color.Equals(UTheLastShiftSettings::GetScanColors().Entity, 1.e-4f),
				FString::Printf(TEXT("%s: from the next Stop its points are in its own colour %s"), *Label, *Settings.Color.ToString()));
			ResumeTime();
			return 0.5f;
		}));
	}

	/** Red pixels of the whole shot outside the figure's box: the trail. */
	static int32 RedOutsideFigure(const FShot& Shot)
	{
		const FBox2D Rect = FigureRect(FindEntity());
		return CountHue(Shot, EHue::Entity, FBox2D(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0))) - CountHue(Shot, EHue::Entity, Rect);
	}

	static void StartFoundTest(bool bQuit, bool bShots)
	{
		StartRun(TEXT("tls.EntityFoundTest"), bQuit, bShots);
		TArray<FStep>& S = GRun.Steps;

		S.Add(WaitFor(TEXT("setup: a game world with the player's lidar and an entity"), []() { return Lidar() && Pawn() && FindEntity() && Run() && Home() && Channel(); }, 10.f, []()
		{
			UWorld* World = W();
			Note(FString::Printf(TEXT("map %s, act %d"), *MapName(), URunSubsystem::GetActIndexOf(World)));
			NarrativeFixture::Install(World);
			GFound = FFoundTest();
			GBehind = FBehind();
			GFound.Text.Reset(NewObject<UTlsTextTestListener>());
			GFound.Text->Bind(Channel());
			GFound.HomeListener.Reset(NewObject<UTlsHomeTestListener>());
			GFound.HomeListener->Bind(Home());
			GFound.bSavedFound = Run()->IsEntityFound();
			GFound.bSavedContact = Run()->HasFirstContact();
			GFound.SavedBattery = Home()->GetBatteryFraction();
			GFound.SavedHome = Home()->GetHomeSecondsElapsed();
			AEntity* E = FindEntity();
			GFound.SavedSpeed = E->Speed;
			GFound.SavedGraze = E->GrazeRadius;
			GFound.SavedResponse = E->ContactResponse;
			// The exact share of every scan (the game stops counting once the outcome is certain), and no graze but the graze case's.
			E->bMeasureFullShare = true;
			E->GrazeRadius = 0.f;
			ULidarScanComponent* L = Lidar();
			GFound.bSavedVerify = L->bVerifyInjectedOcclusion;
			L->bVerifyInjectedOcclusion = true;
			// The entity alone injects: the residues leave the director's sources for the test (registered again at the end).
			for (TActorIterator<AResidue> It(World); It; ++It)
			{
				GFound.MovedResidues.Emplace(*It);
				Director()->UnregisterPointSource(TScriptInterface<ILidarPointSource>(*It));
			}
			Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: start"));
			Run()->SetFirstContact(false, TEXT("tls.EntityFoundTest: start"));
			ResumeTime();
			TS()->ResetCumulativeStoppedSeconds();
			Note(FString::Printf(TEXT("%d residues taken out of the scans; colours: scan %s, entity %s; clear sighting: more than %.0f%% of the figure in %d scans in a row"),
				GFound.MovedResidues.Num(), *UTheLastShiftSettings::GetScanColors().Scan.ToString(), *UTheLastShiftSettings::GetScanColors().Entity.ToString(),
				E->ClearSightingShare * 100.f, E->ClearSightingScans));
			GRun.Cleanup = []()
			{
				ResumeTime();
				if (AEntity* E = FindEntity())
				{
					E->Speed = GFound.SavedSpeed;
					E->GrazeRadius = GFound.SavedGraze;
					E->ContactResponse = GFound.SavedResponse;
					E->bMeasureFullShare = false;
				}
				if (ULidarScanComponent* L = Lidar())
				{
					L->bVerifyInjectedOcclusion = GFound.bSavedVerify;
				}
				for (const TWeakObjectPtr<AResidue>& Moved : GFound.MovedResidues)
				{
					if (UScanDirectorComponent* D = Director(); Moved.Get() && D)
					{
						D->RegisterPointSource(TScriptInterface<ILidarPointSource>(Moved.Get()));
					}
				}
				if (URunSubsystem* RunSub = Run())
				{
					RunSub->SetEntityFound(GFound.bSavedFound, TEXT("tls.EntityFoundTest: end"));
					RunSub->SetFirstContact(GFound.bSavedContact, TEXT("tls.EntityFoundTest: end"));
				}
				if (UHomeStateSubsystem* H = Home())
				{
					H->RestoreState(GFound.SavedBattery, GFound.SavedHome);
				}
				if (GFound.Text)
				{
					GFound.Text->Unbind();
				}
				if (GFound.HomeListener)
				{
					GFound.HomeListener->Unbind();
				}
				NarrativeFixture::Restore(W());
				ReleaseShots();
			};
			MarkCase();
			return 0.3f;
		}));

		// --- Not found: dormant, outside the coverage, behind geometry ---
		S.Add(Do(TEXT("dormant: Stop"), []()
		{
			const AEntity* E = FindEntity();
			Check(E && E->GetState() == EEntityState::Dormant, TEXT("dormant: the entity is dormant at the start"));
			StopTime();
			return 1.6f;
		}));
		S.Add(Do(TEXT("dormant: Resume"), []()
		{
			const AEntity* E = FindEntity();
			Check(Director()->GetScansThisStop() >= 1 && E && !E->IsManifested(), TEXT("dormant: a scan fired, the entity did not appear"));
			ResumeTime();
			Check(!Run()->IsEntityFound() && !E->WasSightedThisStop(), TEXT("dormant: a Stop without the entity does not make it found"));
			return 0.3f;
		}));

		S.Add(Do(TEXT("away: Stop, the entity appears, the player faces away from it"), []()
		{
			StopTime();
			AEntity* E = FindEntity();
			E->SpawnNow(800.f);
			E->Speed = 0.f;
			const FVector Away = Pawn()->GetActorLocation() * 2.f - E->GetFigureLocation();
			Look(FVector(Away.X, Away.Y, EyeOf(Pawn()).Z));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("away: the automatic scan"), []() { return Director()->GetScansThisStop() >= 1; }, 4.f, []()
		{
			Lidar()->Scan();
			return 0.3f;
		}));
		S.Add(Do(TEXT("away: the scans"), []()
		{
			const AEntity* E = FindEntity();
			const FLidarInjectionStats Stats = Lidar()->GetLastInjectionStats();
			Check(E->IsManifested() && Stats.Gathered > 0 && Stats.Uploaded == 0 && !E->GetLastKeptCheckResult() && E->GetLastKeptCheckTraces() == 0 && E->GetLastKeptCheckKept() == 0,
				FString::Printf(TEXT("away: the figure is outside the coverage: nothing uploaded, nothing traced (%s)"), *LastScanText()));
			return 0.f;
		}));
		AddNotFoundEnd(S, TEXT("away"));

		// Behind geometry: stood where geometry is between the player and the figure, facing it, before the automatic scan.
		// "Out of sight" is the spawn's three-height test; an arm can still show past an edge, and then both the lidar and the
		// game see it: that pose is not the hidden case, and up to three poses are tried.
		for (int32 Attempt = 0; Attempt < 3; ++Attempt)
		{
			const FString L = FString::Printf(TEXT("behind %d"), Attempt + 1);
			S.Add(Do(L + TEXT(": Stop, the player behind geometry from the figure, facing it"), [L, Attempt]()
			{
				if (GBehind.bDone)
				{
					return 0.f;
				}
				MarkCase();
				StopTime();
				AEntity* E = FindEntity();
				E->Speed = 0.f;
				FVector Floor;
				const bool bHidden = E->IsManifested() && FindHiddenFloor(E, 400.f + 300.f * Attempt, 1800.f, Floor);
				Note(FString::Printf(TEXT("%s: %s"), *L, bHidden ? TEXT("a pose with geometry between") : TEXT("no hidden pose")));
				if (bHidden)
				{
					StandOn(Floor, E->GetFigureLocation() + FVector(0.f, 0.f, 90.f));
				}
				GBehind.AutoKept = GBehind.ManualKept = -1;
				return 0.f;
			}));
			S.Add(WaitFor(L + TEXT(": the automatic scan"), []() { return GBehind.bDone || Director()->GetScansThisStop() >= 1; }, 4.f, []()
			{
				if (GBehind.bDone)
				{
					return 0.f;
				}
				const AEntity* E = FindEntity();
				GBehind.AutoKept = LidarKept();
				GBehind.AutoGame = E->GetLastKeptCheckKept();
				GBehind.AutoTraces = E->GetLastKeptCheckTraces();
				GBehind.AutoScan = Lidar()->GetLastInjectionStats().ScanIndex;
				GBehind.bAutoChecked = CheckedLastScan();
				Look(E->GetFigureLocation() + FVector(0.f, 0.f, 90.f));
				return 0.12f;
			}));
			S.Add(Do(L + TEXT(": scan"), []() { if (!GBehind.bDone) { Lidar()->Scan(); } return 0.35f; }));
			S.Add(Do(L + TEXT(": the two scans"), [L]()
			{
				if (GBehind.bDone)
				{
					return 0.f;
				}
				const AEntity* E = FindEntity();
				const FLidarInjectionStats Stats = Lidar()->GetLastInjectionStats();
				GBehind.ManualKept = LidarKept();
				const bool bManualChecked = CheckedLastScan();
				const bool bSeen = E->CanBeSeenFrom(EyeOf(Pawn()), E->GetFigureLocation());
				Note(FString::Printf(TEXT("%s: figure %.0f cm away, in sight %d; automatic scan: the lidar keeps %d, the game %d of the figure (%s, %d traces); %s; the lidar keeps %d"), *L,
					FVector::Dist2D(Pawn()->GetActorLocation(), E->GetFigureLocation()), bSeen ? 1 : 0, GBehind.AutoKept, GBehind.AutoGame,
					GBehind.bAutoChecked ? TEXT("checked") : TEXT("did not check"), GBehind.AutoTraces, *LastScanText(), GBehind.ManualKept));
				Check(Stats.Sources == 1, FString::Printf(TEXT("%s: the entity is the only source of the scans (%d)"), *L, Stats.Sources));
				// The lidar's count holds the trail too: the game's figure count is never above it, and 0 when the lidar keeps none.
				bool bAgree = true;
				if (GBehind.bAutoChecked)
				{
					bAgree &= GBehind.AutoGame <= GBehind.AutoKept && (GBehind.AutoKept > 0 || GBehind.AutoGame == 0);
				}
				if (bManualChecked)
				{
					bAgree &= E->GetLastKeptCheckKept() <= GBehind.ManualKept && (GBehind.ManualKept > 0 || E->GetLastKeptCheckKept() == 0);
				}
				Check(bAgree, FString::Printf(TEXT("%s: for each scan it checked, the game keeps no more of the figure than the lidar's depth test keeps of figure and trail"), *L));
				const bool bSighted = E->WasSightedThisStop();
				ResumeTime();
				if (GBehind.AutoKept == 0 && GBehind.ManualKept == 0 && Stats.Uploaded > 0)
				{
					GBehind.bDone = true;
					Check(!bSighted && E->GetSightingStreak() == 0, FString::Printf(TEXT("%s: in the coverage (%d points uploaded), every point removed by the lidar, none kept by the game"), *L, Stats.Uploaded));
					Check(!Run()->IsEntityFound(), FString::Printf(TEXT("%s: the Stop does not make it found"), *L));
				}
				else
				{
					Note(FString::Printf(TEXT("%s: part of it was in view of a scan (lidar %d and %d, %d uploaded): not the hidden case; found %d"), *L, GBehind.AutoKept,
						GBehind.ManualKept, Stats.Uploaded, Run()->IsEntityFound() ? 1 : 0));
					Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: behind case in view"));
				}
				return 0.3f;
			}));
			S.Add(WaitFor(L + TEXT(": the channel is idle"), []() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }, 40.f));
		}
		S.Add(Do(TEXT("behind: done"), []()
		{
			Check(GBehind.bDone, TEXT("behind: a Stop with the figure in the coverage behind geometry, within three poses"));
			Check(StartedSince(GFound.StartedMark, TEXT("Entity")) == 0, FString::Printf(TEXT("behind: nothing said (%s)"), *StartedRows(GFound.StartedMark)));
			return 0.f;
		}));

		// --- Not found: only points of its trail kept, in three scans of a Stop (up to six Stops tried) ---
		S.Add(Do(TEXT("trail: begin"), []() { GFound.bCaseDone = false; return 0.f; }));
		for (int32 Attempt = 0; Attempt < 6; ++Attempt)
		{
			const FString L = FString::Printf(TEXT("trail %d"), Attempt + 1);
			S.Add(Do(L + TEXT(": Stop, the figure out of sight, the player looking along its trail"), [L, Attempt]()
			{
				if (GFound.bCaseDone)
				{
					return 0.f;
				}
				MarkCase();
				StopTime();
				AEntity* E = FindEntity();
				E->Speed = 0.f;
				FVector Floor;
				const bool bHidden = E->IsManifested() && FindHiddenFloor(E, 300.f + 150.f * Attempt, 1500.f, Floor);
				if (bHidden)
				{
					StandOn(Floor, Floor);
					// A point of the path 3 or 4.5 m ahead of the figure, along its trail.
					const TArray<FVector> Path = E->GetCurrentPath();
					FVector Target = E->GetFigureLocation();
					float Left = Attempt % 2 == 0 ? 300.f : 450.f;
					for (int32 I = 1; I < Path.Num(); ++I)
					{
						const float Leg = FVector::Dist(Path[I - 1], Path[I]);
						if (Leg >= Left)
						{
							Target = FMath::Lerp(Path[I - 1], Path[I], Leg > 0.f ? Left / Leg : 0.f);
							break;
						}
						Left -= Leg;
						Target = Path[I];
					}
					Look(Target + FVector(0.f, 0.f, 10.f));
				}
				Note(FString::Printf(TEXT("%s: %s"), *L, bHidden ? TEXT("a pose with the figure out of sight") : TEXT("no hidden pose")));
				GFound.AttemptFigureKept = 0;
				GFound.AttemptLidarKept = 0;
				GFound.AttemptScans = 0;
				return 0.f;
			}));
			for (int32 Scan = 0; Scan < 3; ++Scan)
			{
				const FString SL = FString::Printf(TEXT("%s: scan %d"), *L, Scan + 1);
				if (Scan == 0)
				{
					S.Add(WaitFor(SL + TEXT(" (the automatic one)"), []() { return GFound.bCaseDone || Director()->GetScansThisStop() >= 1; }, 4.f));
				}
				else
				{
					S.Add(Do(SL, []() { if (!GFound.bCaseDone) { Lidar()->Scan(); } return 0.35f; }));
				}
				S.Add(Do(SL + TEXT(": what it kept"), [SL]()
				{
					if (GFound.bCaseDone)
					{
						return 0.f;
					}
					const AEntity* E = FindEntity();
					GFound.AttemptFigureKept += CheckedLastScan() ? E->GetLastKeptCheckKept() : 1000000;
					GFound.AttemptLidarKept += FMath::Max(LidarKept(), 0);
					++GFound.AttemptScans;
					Note(FString::Printf(TEXT("%s: %s; trail points %d"), *SL, *LastScanText(), E->GetLastGatheredTrailPoints()));
					return 0.f;
				}));
			}
			S.Add(Do(L + TEXT(": the Stop"), [L]()
			{
				if (GFound.bCaseDone)
				{
					return 0.f;
				}
				const AEntity* E = FindEntity();
				const bool bTrailOnly = GFound.AttemptScans == 3 && GFound.AttemptFigureKept == 0 && GFound.AttemptLidarKept > 0;
				if (bTrailOnly)
				{
					GFound.bCaseDone = true;
					Check(E->GetSightingStreak() == 0 && !E->WasSightedThisStop(), FString::Printf(TEXT("%s: three scans kept %d points of its trail and none of its figure: no clear sighting"), *L,
						GFound.AttemptLidarKept));
					return 0.f;
				}
				Note(FString::Printf(TEXT("%s: not the trail-only case (figure kept %d, lidar kept %d in %d scans)"), *L, GFound.AttemptFigureKept, GFound.AttemptLidarKept, GFound.AttemptScans));
				ResumeTime();
				Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: trail case not met"));
				Run()->SetFirstContact(false, TEXT("tls.EntityFoundTest: trail case not met"));
				return 0.3f;
			}));
			S.Add(WaitFor(L + TEXT(": ready"), []() { const UTextChannelComponent* C = Channel(); return GFound.bCaseDone || (C && !C->IsBusy()); }, 40.f));
		}
		S.Add(Do(TEXT("trail: done"), []()
		{
			Check(GFound.bCaseDone, TEXT("trail: a Stop in which the scans kept only points of the trail, within six Stops"));
			return GFound.bCaseDone ? 0.f : -1.f;
		}));
		AddNotFoundEnd(S, TEXT("trail"));

		// --- Not found: half of the figure or less in every scan ---
		AddCaseStart(S, TEXT("half or less"));
		AddStopManifested(S, TEXT("half or less"));
		AddClearPose(S, TEXT("half or less"), 350.f, 700.f);
		AddPartialProbe(S, TEXT("half or less"));
		AddLookScan(S, TEXT("half or less: scan P1"), TEXT('P'), 0, false);
		AddLookScan(S, TEXT("half or less: scan P2"), TEXT('P'), 0, false);
		AddLookScan(S, TEXT("half or less: scan P3"), TEXT('P'), 0, false);
		AddNotFoundEnd(S, TEXT("half or less"));

		// --- Not found: more than half in one scan only ---
		AddCaseStart(S, TEXT("one scan"));
		AddStopManifested(S, TEXT("one scan"));
		AddClearPose(S, TEXT("one scan"), 350.f, 700.f);
		AddPartialProbe(S, TEXT("one scan"));
		AddLookScan(S, TEXT("one scan: scan F"), TEXT('F'), 1, false);
		AddLookScan(S, TEXT("one scan: scan P1"), TEXT('P'), 0, false);
		AddLookScan(S, TEXT("one scan: scan P2"), TEXT('P'), 0, false);
		AddNotFoundEnd(S, TEXT("one scan"));

		// --- Not found: more than half in two scans that are not in a row ---
		AddCaseStart(S, TEXT("not in a row"));
		AddStopManifested(S, TEXT("not in a row"));
		AddClearPose(S, TEXT("not in a row"), 350.f, 700.f);
		AddPartialProbe(S, TEXT("not in a row"));
		AddLookScan(S, TEXT("not in a row: scan F1"), TEXT('F'), 1, false);
		AddLookScan(S, TEXT("not in a row: scan P"), TEXT('P'), 0, false);
		AddLookScan(S, TEXT("not in a row: scan F2"), TEXT('F'), 1, false);
		AddNotFoundEnd(S, TEXT("not in a row"));

		// --- Not found: more than half in the last scan of one Stop and in the first of the next ---
		AddCaseStart(S, TEXT("two Stops"));
		AddStopManifested(S, TEXT("two Stops: first"));
		AddClearPose(S, TEXT("two Stops: first"), 350.f, 700.f);
		AddLookScan(S, TEXT("two Stops: the last scan of the first Stop"), TEXT('F'), 1, false);
		S.Add(Do(TEXT("two Stops: Resume, Stop again, stand where all of it is seen before the automatic scan"), []()
		{
			ResumeTime();
			Check(!Run()->IsEntityFound(), TEXT("two Stops: the first Stop ends, not found"));
			Home()->RestoreState(GFound.SavedBattery, GFound.SavedHome);
			StopTime();
			AEntity* E = FindEntity();
			E->Speed = 0.f;
			Check(E->IsManifested() && E->GetSightingStreak() == 0, FString::Printf(TEXT("two Stops: the next Stop starts the count again (%d in a row)"), E->GetSightingStreak()));
			return E->IsManifested() ? StandClear(TEXT("two Stops: second"), 350.f, 700.f) : -1.f;
		}));
		S.Add(WaitFor(TEXT("two Stops: the automatic scan of the second Stop"), []() { return Director()->GetScansThisStop() >= 1; }, 4.f, []()
		{
			const AEntity* E = FindEntity();
			Check(CheckedLastScan() && E->GetLastKeptCheckResult() && E->GetSightingStreak() == 1 && !E->WasSightedThisStop(),
				FString::Printf(TEXT("two Stops: the first scan of the second Stop keeps more than half of it, and it is 1 in a row (%s)"), *LastScanText()));
			return 0.f;
		}));
		AddNotFoundEnd(S, TEXT("two Stops"));

		// --- Found by a clear sighting ---
		AddCaseStart(S, TEXT("sighting"));
		AddStopManifested(S, TEXT("sighting"));
		AddClearPose(S, TEXT("sighting"), 250.f, 380.f);
		AddLookScan(S, TEXT("sighting: scan 1"), TEXT('F'), 1, false);
		AddLookScan(S, TEXT("sighting: scan 2"), TEXT('F'), 2, true);
		S.Add(Do(TEXT("sighting: still in the scan's colour"), []()
		{
			const AEntity* E = FindEntity();
			const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(const_cast<AEntity*>(E));
			Check(Settings.Color.Equals(UTheLastShiftSettings::GetScanColors().Scan, 1.e-4f) && !Run()->IsEntityFound(),
				FString::Printf(TEXT("sighting: for the rest of the Stop its points are in the scan's colour %s, not found yet"), *Settings.Color.ToString()));
			GFound.NotFoundSettings = Settings;
			GFound.NotFoundFigurePoints = E->GetLastGatheredFigurePoints();
			Note(FString::Printf(TEXT("sighting: figure %d points, trail %d points, intensity %.2f"), E->GetLastGatheredFigurePoints(), E->GetLastGatheredTrailPoints(), Settings.IntensityMultiplier));
			return 0.f;
		}));
		S.Add(Do(TEXT("sighting: a third scan"), []() { Lidar()->Scan(); return 0.35f; }));
		AddShot(S, TEXT("Entity_Sighted_ScanColour"), [](const FShot& Shot)
		{
			const FBox2D Rect = FigureRect(FindEntity());
			const int32 Red = CountHue(Shot, EHue::Entity, Rect);
			const int32 Cyan = CountHue(Shot, EHue::Scan, Rect);
			const int32 RedOut = RedOutsideFigure(Shot);
			GFound.NotFoundScanHue = Cyan;
			Check(Red >= 0 && Red <= 5 && RedOut <= 5 && Cyan > 40, FString::Printf(TEXT("sighting: after the sighting, around the figure %d pixels of the entity's red, %d of the scan's colour; %d red pixels elsewhere (rect %s)"),
				Red, Cyan, RedOut, *Rect.ToString()));
		});
		S.Add(Do(TEXT("sighting: the same settings found, but the colour"), []()
		{
			AEntity* E = FindEntity();
			Run()->SetEntityFound(true, TEXT("tls.EntityFoundTest: compare"));
			const FLidarSourceSettings Found = ILidarPointSource::Execute_GetLidarSourceSettings(E);
			Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: compare"));
			const FLidarSourceSettings& Not = GFound.NotFoundSettings;
			Check(Found.Color.Equals(UTheLastShiftSettings::GetScanColors().Entity, 1.e-4f) && Found.IntensityMultiplier == Not.IntensityMultiplier && Found.FadeMode == Not.FadeMode
				&& Found.VanishSeconds == Not.VanishSeconds && Found.HoldSeconds == Not.HoldSeconds && Found.bOccludeByCapture == Not.bOccludeByCapture
				&& Found.OcclusionTolerance == Not.OcclusionTolerance && Found.bShowBehindGeometry == Not.bShowBehindGeometry,
				TEXT("not found and found: the same intensity, fade, vanish, occlusion and behind-geometry settings; only the colour differs"));
			return 0.f;
		}));
		S.Add(Do(TEXT("sighting: Resume"), []()
		{
			ResumeTime();
			Check(Run()->IsEntityFound() && !Run()->HasFirstContact(), TEXT("sighting: a voluntary Resume after a clear sighting makes it found, no first contact"));
			return 0.f;
		}));
		AddExpectRows(S, TEXT("sighting"), { TEXT("EntitySeen(engineer)"), TEXT("EntityFound") });

		// The entity's colour from the next Stop: the figure and the trail.
		AddCaseStart(S, TEXT("found"));
		AddStopManifested(S, TEXT("found"));
		AddClearPose(S, TEXT("found"), 600.f, 1000.f);
		// The figure's path, and its trail, turn to the player on the next beat (it was spawned towards the player's old place).
		S.Add(WaitFor(TEXT("found: a beat refreshes its path towards the player"), []() { return Director()->GetBeatCount() >= 1; }, 8.f, []() { return 0.3f; }));
		S.Add(Do(TEXT("found: scan"), []() { PC()->SetControlRotation(GFound.FullRot + FRotator(-15.f, 0.f, 0.f)); return 0.12f; }));
		S.Add(Do(TEXT("found: scan it"), []() { Lidar()->Scan(); return 0.35f; }));
		S.Add(Do(TEXT("found: its colour"), []()
		{
			AEntity* E = FindEntity();
			const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(E);
			Check(Settings.Color.Equals(UTheLastShiftSettings::GetScanColors().Entity, 1.e-4f), FString::Printf(TEXT("found: its points are in the entity's colour %s"), *Settings.Color.ToString()));
			Check(E->GetLastGatheredFigurePoints() == GFound.NotFoundFigurePoints && Settings.IntensityMultiplier == GFound.NotFoundSettings.IntensityMultiplier,
				FString::Printf(TEXT("found: the figure has the same points (%d, %d before) and intensity (%.2f)"), E->GetLastGatheredFigurePoints(), GFound.NotFoundFigurePoints, Settings.IntensityMultiplier));
			Check(E->GetLastKeptCheckScan() != Lidar()->GetLastInjectionStats().ScanIndex, TEXT("found: once found, the game checks its points no more"));
			return 0.f;
		}));
		AddShot(S, TEXT("Entity_Found_EntityColour"), [](const FShot& Shot)
		{
			const FBox2D Rect = FigureRect(FindEntity());
			const int32 Red = CountHue(Shot, EHue::Entity, Rect);
			const int32 Cyan = CountHue(Shot, EHue::Scan, Rect);
			const int32 RedOut = RedOutsideFigure(Shot);
			Check(Red > 40 && RedOut > 10, FString::Printf(TEXT("found: around the figure %d pixels of the entity's red, %d of the scan's colour (%d before it was found); %d red pixels of its trail elsewhere (rect %s)"),
				Red, Cyan, GFound.NotFoundScanHue, RedOut, *Rect.ToString()));
		});
		S.Add(Do(TEXT("found: Resume"), []() { ResumeTime(); return 4.f; }));
		S.Add(Do(TEXT("found: said once in a run"), []()
		{
			Check(StartedSince(GFound.StartedMark, TEXT("Entity")) == 0, FString::Printf(TEXT("found: a later Stop that shows it says nothing more (%s)"), *StartedRows(GFound.StartedMark)));
			return 0.f;
		}));

		// --- The first contact after a clear sighting, then a second contact (area 2: a jump) ---
		AddCaseStart(S, TEXT("contact after the sighting"));
		AddContact(S, TEXT("contact after the sighting"), EEntityContactResponse::Jump, true);
		AddExpectRows(S, TEXT("contact after the sighting"), { TEXT("JumpReturn"), TEXT("EntityContact") });
		AddCaseStart(S, TEXT("second contact"));
		AddContact(S, TEXT("second contact"), EEntityContactResponse::Jump, true);
		AddExpectRows(S, TEXT("second contact"), { TEXT("JumpReturn") });

		// --- A graze finds nothing ---
		AddCaseStart(S, TEXT("graze"), []()
		{
			Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: graze"));
			Run()->SetFirstContact(false, TEXT("tls.EntityFoundTest: graze"));
		});
		S.Add(Do(TEXT("graze: Stop, the player 3 m along the figure's path, facing away"), []()
		{
			AEntity* E = FindEntity();
			E->GrazeRadius = 400.f;
			StopTime();
			E->SpawnNow(E->MinSpawnDistance);
			E->Speed = 0.f;
			const TArray<FVector> Path = E->GetCurrentPath();
			FVector Target = Path.Num() > 0 ? Path.Last() : E->GetFigureLocation();
			float Left = 300.f;
			for (int32 I = 1; I < Path.Num(); ++I)
			{
				const float Leg = FVector::Dist(Path[I - 1], Path[I]);
				if (Leg >= Left)
				{
					Target = FMath::Lerp(Path[I - 1], Path[I], Leg > 0.f ? Left / Leg : 0.f);
					break;
				}
				Left -= Leg;
			}
			APawn* P = Pawn();
			const float FeetToActor = P->GetActorLocation().Z - P->GetNavAgentLocation().Z;
			P->SetActorLocation(Target + FVector(0.f, 0.f, FeetToActor + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			const FVector Away = Target * 2.f - E->GetFigureLocation();
			Look(FVector(Away.X, Away.Y, EyeOf(P).Z));
			return 0.3f;
		}));
		S.Add(Do(TEXT("graze: Resume"), []()
		{
			AEntity* E = FindEntity();
			ResumeTime();
			E->GrazeRadius = 0.f;
			Check(E->WasLastExitGraze() && !Run()->IsEntityFound() && !Run()->HasFirstContact(), TEXT("graze: the Resume was a graze; not found, no first contact"));
			return 0.f;
		}));
		AddExpectRows(S, TEXT("graze"), { TEXT("JumpReturn") });

		// --- Found by contact, with each contact response ---
		for (const EEntityContactResponse Response : { EEntityContactResponse::Rewind, EEntityContactResponse::Jump, EEntityContactResponse::RewindAndJump })
		{
			const FString Label = FString::Printf(TEXT("found by contact (%s)"), LexToString(Response));
			AddCaseStart(S, Label, []()
			{
				Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: contact case"));
				Run()->SetFirstContact(false, TEXT("tls.EntityFoundTest: contact case"));
			});
			AddContact(S, Label, Response, true);
			if (Response == EEntityContactResponse::Rewind)
			{
				AddExpectRows(S, Label, { TEXT("EntityContact"), TEXT("EntityFound") });
			}
			else
			{
				AddExpectRows(S, Label, { TEXT("JumpReturn"), TEXT("EntityContact"), TEXT("EntityFound") });
			}
			AddOwnColourNextStop(S, Label);
		}

		// --- A clear sighting and the first contact in the same Stop: the contact's sequence ---
		AddCaseStart(S, TEXT("sighting and contact"), []()
		{
			Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: sighting and contact"));
			Run()->SetFirstContact(false, TEXT("tls.EntityFoundTest: sighting and contact"));
		});
		AddStopManifested(S, TEXT("sighting and contact"));
		AddClearPose(S, TEXT("sighting and contact"), 350.f, 700.f);
		AddLookScan(S, TEXT("sighting and contact: scan 1"), TEXT('F'), 1, false);
		AddLookScan(S, TEXT("sighting and contact: scan 2"), TEXT('F'), 2, true);
		AddContact(S, TEXT("sighting and contact"), EEntityContactResponse::Jump, false);
		AddExpectRows(S, TEXT("sighting and contact"), { TEXT("JumpReturn"), TEXT("EntityContact"), TEXT("EntityFound") });

		Launch();
	}

	static FAutoConsoleCommandWithWorldAndArgs GEntityFoundTestCommand(
		TEXT("tls.EntityFoundTest"),
		TEXT("tls.EntityFoundTest [Quit] [Shots]: Brief 20, the entity found by a clear sighting or by contact, and the rows said (an area map; Area2_ServerHalls)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			StartFoundTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0, Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0);
		}));

	// =====================================================================================================
	// tls.StillCheck
	// =====================================================================================================

	struct FStill
	{
		TStrongObjectPtr<UTlsHomeTestListener> HomeListener;
		double StopReal = -1.0;
		double StopRun = -1.0;
		int32 JumpsMark = 0;
		double EndReal = -1.0;
		double EndRun = -1.0;
		float MaxFrame = 0.f;
		int32 LongFrames = 0;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
	};
	static FStill GStill;

	static void ReportStill(const TCHAR* Label)
	{
		const FHomeJump& Jump = GStill.HomeListener->LastJump;
		const FString Shown = UTextTokensLibrary::FormatDuration(Jump.StoppedSeconds, ETextDegradation::Full);
		const double RealLen = GStill.EndReal - GStill.StopReal;
		const double RunLen = GStill.EndRun - GStill.StopRun;
		const float MaxUndilated = W() && W()->GetWorldSettings() ? W()->GetWorldSettings()->MaxUndilatedFrameTime : -1.f;
		Note(FString::Printf(TEXT("%s: {still} shows \"%s\"; the jump's StoppedSeconds %.3f s; the Stop lasted %.3f s of the run's clock and %.3f s of real time; longest frame %.3f s, %d frames over the %.2f s world clamp"),
			Label, *Shown, Jump.StoppedSeconds, RunLen, RealLen, GStill.MaxFrame, GStill.LongFrames, MaxUndilated));
		Check(GStill.HomeListener->Jumps == GStill.JumpsMark + 1, FString::Printf(TEXT("%s: one jump"), Label));
		Check(FMath::Abs(Jump.StoppedSeconds - RunLen) <= 0.05, FString::Printf(TEXT("%s: {still} is the Stop's length on the run's clock (%.3f vs %.3f s)"), Label, Jump.StoppedSeconds, RunLen));
		Check(Shown == UTextTokensLibrary::FormatDuration(FMath::FloorToDouble(Jump.StoppedSeconds), ETextDegradation::Full),
			FString::Printf(TEXT("%s: the number is the whole seconds, rounded down (%.3f s -> \"%s\")"), Label, Jump.StoppedSeconds, *Shown));
	}

	static void StartStillCheck(bool bQuit)
	{
		StartRun(TEXT("tls.StillCheck"), bQuit, false);
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("setup: an area with an entity"), []() { return Pawn() && FindEntity() && Home() && URunSubsystem::GetActIndexOf(W()) > 0; }, 10.f, []()
		{
			GStill = FStill();
			GStill.HomeListener.Reset(NewObject<UTlsHomeTestListener>());
			GStill.HomeListener->Bind(Home());
			GStill.SavedBattery = Home()->GetBatteryFraction();
			GStill.SavedHome = Home()->GetHomeSecondsElapsed();
			ResumeTime();
			Note(FString::Printf(TEXT("map %s, act %d, contact response %s, graze radius %.0f cm"), *MapName(), URunSubsystem::GetActIndexOf(W()),
				LexToString(FindEntity()->ContactResponse), FindEntity()->GrazeRadius));
			GRun.PerTick = [](double Delta)
			{
				if (GStill.StopReal >= 0.0 && GStill.EndReal < 0.0)
				{
					GStill.MaxFrame = FMath::Max(GStill.MaxFrame, static_cast<float>(Delta));
					const float Clamp = W()->GetWorldSettings() ? W()->GetWorldSettings()->MaxUndilatedFrameTime : 0.4f;
					GStill.LongFrames += Delta > Clamp ? 1 : 0;
					if (!IsStopped())
					{
						GStill.EndReal = Real();
						GStill.EndRun = TS()->GetRunSeconds();
					}
				}
			};
			GRun.Cleanup = []()
			{
				ResumeTime();
				if (UHomeStateSubsystem* H = Home())
				{
					H->RestoreState(GStill.SavedBattery, GStill.SavedHome);
				}
				if (GStill.HomeListener)
				{
					GStill.HomeListener->Unbind();
				}
			};
			return 0.5f;
		}));
		auto AddStart = [&S](const FString& Label)
		{
			S.Add(Do(Label + TEXT(": Stop, the entity at its minimum distance"), []()
			{
				GStill.JumpsMark = GStill.HomeListener->Jumps;
				GStill.EndReal = GStill.EndRun = -1.0;
				GStill.MaxFrame = 0.f;
				GStill.LongFrames = 0;
				StopTime();
				GStill.StopReal = Real();
				GStill.StopRun = TS()->GetRunSeconds();
				AEntity* E = FindEntity();
				E->SpawnNow(E->MinSpawnDistance);
				return 0.f;
			}));
		};
		AddStart(TEXT("contact"));
		S.Add(WaitFor(TEXT("contact: the figure reaches the player"), []() { return !IsStopped() && GStill.EndReal >= 0.0; }, 40.f, []()
		{
			ReportStill(TEXT("contact"));
			Home()->RestoreState(GStill.SavedBattery, GStill.SavedHome);
			return 2.f;
		}));
		AddStart(TEXT("graze"));
		S.Add(WaitFor(TEXT("graze: the figure within the graze radius"), []()
		{
			const AEntity* E = FindEntity();
			const float D = E ? E->GetPathDistanceToPlayer() : -1.f;
			return D >= 0.f && D <= FMath::Min(E->GrazeRadius, 380.f);
		}, 40.f, []()
		{
			ResumeTime();
			return 0.2f;
		}));
		S.Add(Do(TEXT("graze: the jump"), []()
		{
			Check(FindEntity()->WasLastExitGraze(), TEXT("graze: the Resume was a graze"));
			ReportStill(TEXT("graze"));
			return 0.f;
		}));
		Launch();
	}

	static FAutoConsoleCommandWithWorldAndArgs GStillCheckCommand(
		TEXT("tls.StillCheck"),
		TEXT("tls.StillCheck [Quit]: Brief 17, the {still} number of a contact jump and of a graze against the Stop's measured length (an area map; Area2_ServerHalls)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			StartStillCheck(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
		}));

	// =====================================================================================================
	// tls.FoundRunTest
	// =====================================================================================================

	static FString SlotPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), FString(URunSubsystem::SaveSlotName()) + TEXT(".sav"));
	}

	static FString DescribeSnapshots()
	{
		FString Out;
		for (const FAreaSnapshot& Snap : Run()->GetSnapshots())
		{
			Out += FString::Printf(TEXT("%s[act %d: battery %.4f, home %.0f s, banked %d, archive %d+%d, entity %s, contact %s]"), Out.IsEmpty() ? TEXT("") : TEXT(" "), Snap.ActIndex,
				Snap.BatteryFraction, Snap.HomeSecondsElapsed, Snap.BankedEnergy, Snap.Archive.LogOrders.Num(), Snap.Archive.TwinPartsPlayed, Snap.bEntityFound ? TEXT("found") : TEXT("not found"),
				Snap.bFirstContact ? TEXT("yes") : TEXT("no"));
		}
		return Out.IsEmpty() ? TEXT("none") : Out;
	}

	/** The snapshot of Act: its entity state and first contact. False when there is none. */
	static bool SnapshotState(int32 Act, bool& bOutFound, bool& bOutContact)
	{
		for (const FAreaSnapshot& Snap : Run()->GetSnapshots())
		{
			if (Snap.ActIndex == Act)
			{
				bOutFound = Snap.bEntityFound;
				bOutContact = Snap.bFirstContact;
				return true;
			}
		}
		return false;
	}

	static bool SnapshotIs(int32 Act, bool bFound, bool bContact)
	{
		bool bF = !bFound, bC = !bContact;
		return SnapshotState(Act, bF, bC) && bF == bFound && bC == bContact;
	}

	/** The slot as a URunSaveGame read straight from the disk. */
	static const URunSaveGame* ReadSlot()
	{
		return Cast<URunSaveGame>(UGameplayStatics::LoadGameFromSlot(URunSubsystem::SaveSlotName(), 0));
	}

	static bool RunIs(bool bFound, bool bContact)
	{
		return Run()->IsEntityFound() == bFound && Run()->HasFirstContact() == bContact;
	}

	static TArray<FAreaSnapshot> GOldSnapshots;

	/** Copies an old save into the slot and loads it, with the state in memory set first; Version and the snapshots must match. */
	static bool LoadOldSave(const FString& File, int32 WantVersion, const TCHAR* What)
	{
		const bool bHave = IFileManager::Get().FileExists(*File);
		Check(bHave, FString::Printf(TEXT("%s is at hand (%s)"), What, *File));
		if (!bHave)
		{
			return false;
		}
		Run()->MarkEntityFound(TEXT("tls.FoundRunTest: before an old save"));
		Run()->MarkFirstContact(TEXT("tls.FoundRunTest: before an old save"));
		const bool bCopied = IFileManager::Get().Copy(*SlotPath(), *File, true) == COPY_OK;
		const bool bLoaded = Run()->LoadSave();
		GOldSnapshots = Run()->GetSnapshots();
		bool bAnyContact = false;
		for (const FAreaSnapshot& Snap : GOldSnapshots)
		{
			bAnyContact |= Snap.bFirstContact;
		}
		Check(bCopied && bLoaded && Run()->GetLoadedSaveVersion() == WantVersion && GOldSnapshots.Num() > 0 && !bAnyContact,
			FString::Printf(TEXT("%s loads as version %d with the first contact not happened in every snapshot (version %d: %s)"), What, WantVersion, Run()->GetLoadedSaveVersion(), *DescribeSnapshots()));
		return bCopied && bLoaded;
	}

	/** Restarts from the old save's latest act (written again by this build) and checks the file on disk against GOldSnapshots. */
	static void AddOldSaveRestart(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": restart from its latest act"), [Label]()
		{
			if (GOldSnapshots.IsEmpty() || GOldSnapshots.Last().ActIndex < 2)
			{
				Note(Label + TEXT(": the old save's latest act is 1: not written again (a restart from act 1 is a new run)"));
				return 0.f;
			}
			Check(Run()->RestartFromAct(GOldSnapshots.Last().ActIndex), Label + TEXT(": restart"));
			return 0.f;
		}));
		S.Add(WaitFor(Label + TEXT(": its latest area ready"), []() { return GOldSnapshots.Num() > 0 && (GOldSnapshots.Last().ActIndex < 2 || AreaReady(GOldSnapshots.Last().ActIndex)); }, 90.f, [Label]()
		{
			if (GOldSnapshots.Last().ActIndex < 2)
			{
				return 0.f;
			}
			const FAreaSnapshot& Latest = GOldSnapshots.Last();
			Check(RunIs(Latest.bEntityFound, false), FString::Printf(TEXT("%s: Continue: the entity %s as the file says, no first contact"), *Label, Latest.bEntityFound ? TEXT("found") : TEXT("not found")));
			const URunSaveGame* Save = ReadSlot();
			bool bSame = Save && Save->Version == URunSaveGame::CurrentVersion;
			for (const FAreaSnapshot& Old : GOldSnapshots)
			{
				const FAreaSnapshot* New = Save ? Save->Snapshots.FindByPredicate([&Old](const FAreaSnapshot& X) { return X.ActIndex == Old.ActIndex; }) : nullptr;
				bSame &= New && New->BatteryFraction == Old.BatteryFraction && New->HomeSecondsElapsed == Old.HomeSecondsElapsed && New->BankedEnergy == Old.BankedEnergy
					&& New->Archive.LogOrders == Old.Archive.LogOrders && New->Archive.TwinPartsPlayed == Old.Archive.TwinPartsPlayed && New->bEntityFound == Old.bEntityFound && !New->bFirstContact;
			}
			Check(bSame, FString::Printf(TEXT("%s: written again as version %d, it keeps battery, home time, energy, archive and the entity state (%s)"), *Label, Save ? Save->Version : -1, *DescribeSnapshots()));
			return 0.f;
		}));
	}

	static void StartFoundRunTest(bool bQuit)
	{
		StartRun(TEXT("tls.FoundRunTest"), bQuit, false);
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("setup: the main menu"), []() { return Run() && MapName().EndsWith(TEXT("MainMenu")) && !Transitioning(); }, 30.f, []()
		{
			Run()->ClearSave();
			GRun.Cleanup = []() { if (URunSubsystem* R = Run()) { R->ClearSave(); } };
			Check(Run()->StartNewRun(), TEXT("a new run starts"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("area 1 ready"), []() { return AreaReady(1); }, 90.f, []()
		{
			Check(RunIs(false, false) && SnapshotIs(1, false, false), FString::Printf(TEXT("area 1: a new run starts with the entity not found and no first contact (%s)"), *DescribeSnapshots()));
			Check(Run()->TravelToNextArea(), TEXT("area 1 -> area 2"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("area 2 ready"), []() { return AreaReady(2); }, 90.f, []()
		{
			Check(RunIs(false, false) && SnapshotIs(2, false, false), FString::Printf(TEXT("area 2: entered not found, no first contact (%s)"), *DescribeSnapshots()));
			Run()->MarkEntityFound(TEXT("tls.FoundRunTest: as if sighted in area 2"));
			Check(RunIs(true, false), TEXT("area 2: found, no first contact"));
			Check(Run()->TravelToNextArea(), TEXT("area 2 -> area 3"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("area 3 ready"), []() { return AreaReady(3); }, 90.f, []()
		{
			Check(RunIs(true, false), TEXT("area 3: the entity state survived the travel"));
			Check(SnapshotIs(2, false, false) && SnapshotIs(3, true, false), FString::Printf(TEXT("area 3: its snapshot holds found, area 2's still not found (%s)"), *DescribeSnapshots()));
			Check(Run()->MarkFirstContact(TEXT("tls.FoundRunTest: as if touched in area 3")) && !Run()->MarkFirstContact(TEXT("tls.FoundRunTest: again")) && RunIs(true, true),
				TEXT("area 3: the first contact, once"));
			Check(Run()->TravelToNextArea(), TEXT("area 3 -> area 4"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("area 4 ready"), []() { return AreaReady(4); }, 90.f, []()
		{
			Check(RunIs(true, true), TEXT("area 4: the first contact survived the travel"));
			Check(SnapshotIs(3, true, false) && SnapshotIs(4, true, true), FString::Printf(TEXT("area 4: its snapshot holds the first contact, area 3's not (%s)"), *DescribeSnapshots()));
			const URunSaveGame* Save = ReadSlot();
			bool bDisk3 = false, bDisk4 = false;
			for (const FAreaSnapshot& Snap : Save ? Save->Snapshots : TArray<FAreaSnapshot>())
			{
				bDisk3 |= Snap.ActIndex == 3 && Snap.bEntityFound && !Snap.bFirstContact;
				bDisk4 |= Snap.ActIndex == 4 && Snap.bEntityFound && Snap.bFirstContact;
			}
			Check(Save && Save->Version == 4 && URunSaveGame::CurrentVersion == 4 && GetDefault<URunSaveGame>()->Version == 1 && bDisk3 && bDisk4,
				FString::Printf(TEXT("the save on disk is version %d with area 3's snapshot found and area 4's found with the first contact (class default %d)"), Save ? Save->Version : -1,
					GetDefault<URunSaveGame>()->Version));
			// Continue reads the save: forget the state in memory first.
			Check(Run()->LoadSave() && Run()->GetLoadedSaveVersion() == 4, FString::Printf(TEXT("the save reads back (version %d)"), Run()->GetLoadedSaveVersion()));
			Run()->SetEntityFound(false, TEXT("tls.FoundRunTest: before Continue"));
			Run()->SetFirstContact(false, TEXT("tls.FoundRunTest: before Continue"));
			Check(Run()->ContinueRun(), TEXT("Continue"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("Continue: area 4 ready"), []() { return AreaReady(4); }, 90.f, []()
		{
			Check(RunIs(true, true), TEXT("Continue: the entity found and the first contact happened, as on entering act 4"));
			Check(Run()->RestartFromAct(3), TEXT("restart from act 3"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("restart: area 3 ready"), []() { return AreaReady(3); }, 90.f, []()
		{
			bool bF = false, bC = false;
			Check(RunIs(true, false) && !SnapshotState(4, bF, bC), FString::Printf(TEXT("restart from act 3: found, no first contact, as on entering act 3; act 4's snapshot discarded (%s)"), *DescribeSnapshots()));
			Check(Run()->RestartFromAct(2), TEXT("restart from act 2"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("restart: area 2 ready"), []() { return AreaReady(2); }, 90.f, []()
		{
			bool bF = false, bC = false;
			Check(RunIs(false, false) && !SnapshotState(3, bF, bC), FString::Printf(TEXT("restart from act 2: not found, no first contact, as on entering act 2; act 3's snapshot discarded (%s)"), *DescribeSnapshots()));
			Run()->MarkEntityFound(TEXT("tls.FoundRunTest: before restart from act 1"));
			Run()->MarkFirstContact(TEXT("tls.FoundRunTest: before restart from act 1"));
			Check(Run()->RestartFromAct(1), TEXT("restart from act 1"));
			Check(RunIs(false, false), TEXT("restart from act 1 (a new run): not found, no first contact"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("restart: area 1 ready"), []() { return AreaReady(1); }, 90.f, []()
		{
			Check(RunIs(false, false), TEXT("area 1 after the restart: not found, no first contact"));
			// A save written before this brief (version 3: the entity state, no first contact).
			LoadOldSave(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief20"), TEXT("Run_preBrief20.sav")), 3, TEXT("a version 3 save written before Brief 20"));
			bool bAnyFound = false;
			for (const FAreaSnapshot& Snap : GOldSnapshots)
			{
				bAnyFound |= Snap.bEntityFound;
			}
			Check(bAnyFound, FString::Printf(TEXT("the version 3 save keeps the entity found where it says so (%s)"), *DescribeSnapshots()));
			return 0.f;
		}));
		AddOldSaveRestart(S, TEXT("version 3 save"));
		S.Add(Do(TEXT("version 2 save"), []()
		{
			// A save written before Brief 17 (version 2: no entity state in its snapshots).
			LoadOldSave(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief17"), TEXT("Run_preBrief17.sav")), 2, TEXT("a version 2 save written before Brief 17"));
			bool bAnyFound = false;
			for (const FAreaSnapshot& Snap : GOldSnapshots)
			{
				bAnyFound |= Snap.bEntityFound;
			}
			Check(!bAnyFound, FString::Printf(TEXT("the version 2 save loads with the entity not found (%s)"), *DescribeSnapshots()));
			return 0.f;
		}));
		AddOldSaveRestart(S, TEXT("version 2 save"));
		S.Add(Do(TEXT("version 1 save and New game"), []()
		{
			// A save with no Version (Brief 8's fixture, written before version 2) still loads as 1.
			LoadOldSave(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief8"), TEXT("Run_preBrief8.sav")), 1, TEXT("a save with no Version"));
			bool bAnyFound = false;
			for (const FAreaSnapshot& Snap : Run()->GetSnapshots())
			{
				bAnyFound |= Snap.bEntityFound;
			}
			Check(!bAnyFound, FString::Printf(TEXT("a save with no Version loads as 1, the entity not found (%s)"), *DescribeSnapshots()));
			// New game: the state in memory is a new run's.
			Run()->MarkEntityFound(TEXT("tls.FoundRunTest: before New game"));
			Run()->MarkFirstContact(TEXT("tls.FoundRunTest: before New game"));
			Check(Run()->StartNewGame() && RunIs(false, false), TEXT("New game starts with the entity not found and no first contact"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("New game: the travel"), []() { return !Transitioning() && Pawn() != nullptr; }, 90.f, []()
		{
			Check(RunIs(false, false), FString::Printf(TEXT("New game: still not found, no first contact in %s"), *MapName()));
			return 0.f;
		}));
		Launch();
	}

	static FAutoConsoleCommandWithWorldAndArgs GFoundRunTestCommand(
		TEXT("tls.FoundRunTest"),
		TEXT("tls.FoundRunTest [Quit]: Brief 20, the entity-found state and the first contact across travel, snapshots, the save (version 4), Continue, restarts, old saves (3, 2, none) and New game (MainMenu). Clears the save slot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			StartFoundRunTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
		}));

	static FAutoConsoleCommandWithWorldAndArgs GFirstContactCommand(
		TEXT("tls.Run.FirstContact"),
		TEXT("tls.Run.FirstContact [0|1]: clears or sets the run's first contact with the entity (without its rows); without an argument, logs it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* InWorld)
		{
			URunSubsystem* RunSub = URunSubsystem::Get(ResolveGameWorld(InWorld));
			if (!RunSub)
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("tls.Run.FirstContact: no run subsystem."));
				return;
			}
			if (Args.Num() > 0)
			{
				RunSub->SetFirstContact(FCString::Atoi(*Args[0]) != 0, TEXT("tls.Run.FirstContact"));
			}
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Run.FirstContact: %s; entity %s."), RunSub->HasFirstContact() ? TEXT("happened") : TEXT("not happened"),
				RunSub->IsEntityFound() ? TEXT("found") : TEXT("not found"));
		}));
}

#endif // !UE_BUILD_SHIPPING

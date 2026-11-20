// SPDX-License-Identifier: MIT

// Brief 17, the entity in the scan's colour until it is found. tls.Run.EntityFound [0|1] sets, clears or logs the run's
// state. tls.EntityFoundTest [Quit] [Shots] (an area map, Area2_ServerHalls in the reports: its contact is a jump):
// the entity dormant, manifested outside the scan's coverage and behind geometry (the game's kept-point check against the
// lidar's own depth test, bVerifyInjectedOcclusion, with the residues moved out of range so that the entity is the only
// injected source) does not make it found; seen, its points are in the scan's colour (measured on a shot), its point
// counts and source settings are those of the found entity but for the colour; a voluntary Resume makes it found and the
// home AI says the EntityFound row once, after the return message when there is one; from the next Stop its points are
// in the entity's colour (shot); a later Stop says nothing; each exit (Rewound, Jumped, RewoundAndJumped) of a Stop
// during which a scan kept its points makes it found. tls.StillCheck [Quit] (Area2_ServerHalls): the {still} number of
// a contact jump and of a graze jump against the Stop's length on the run's clock and in real time. tls.FoundRunTest
// [Quit] (MainMenu): the state across travel, in the snapshots and the save (version 3), with Continue, a restart from
// an act and New game; a version 2 file and a file with no Version load with the entity not found. The messages are
// the test's fixture rows. Shots go to Saved/Screenshots/Brief17. Development builds only.

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
			FPlatformMisc::RequestExit(false, TEXT("tls Brief 17 test"));
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
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief17"), GShot.Label + TEXT(".png")));
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
		TArray<TPair<TWeakObjectPtr<AResidue>, FVector>> MovedResidues;
		bool bSavedFound = false;
		double SavedBattery = 1.0;
		double SavedHome = 0.0;
		float SavedSpeed = 66.6667f;
		bool bSavedVerify = false;
		int32 StartedMark = 0;
		int32 JumpsMark = 0;
		bool bGrazed = false;
		FVector Floor = FVector::ZeroVector;
		int32 NotFoundFigurePoints = -1;
		FLidarSourceSettings NotFoundSettings;
		int32 NotFoundScanHue = -1;
	};
	static FFoundTest GFound;

	struct FBehind
	{
		bool bDone = false;
		int32 AutoKept = -1;
		int32 ManualKept = -1;
		bool bAutoGame = false;
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
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(" ")) + L->Started[I].ToString();
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

	static FString LastScanText()
	{
		const ULidarScanComponent* L = Lidar();
		const AEntity* E = FindEntity();
		const FLidarInjectionStats Stats = L ? L->GetLastInjectionStats() : FLidarInjectionStats();
		return FString::Printf(TEXT("scan %d: lidar gathered %d, outside coverage %d, uploaded %d, behind captured depth %d; game check scan %d: %d traces, %s, %.3f ms"),
			Stats.ScanIndex, Stats.Gathered, Stats.OutsideCoverage, Stats.Uploaded, Stats.BehindCapturedDepth, E ? E->GetLastKeptCheckScan() : -1,
			E ? E->GetLastKeptCheckTraces() : -1, E && E->GetLastKeptCheckResult() ? TEXT("kept") : TEXT("none kept"), E ? E->GetLastKeptCheckMs() : -1.0);
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

	/** Stands where the figure is seen MinDist..MaxDist away, then one scan from there (after the automatic one). */
	static void AddSeenScan(TArray<FStep>& S, const FString& Label, float MinDist, float MaxDist)
	{
		S.Add(WaitFor(Label + TEXT(": the automatic scan has fired"), []() { const UScanDirectorComponent* D = Director(); return D && D->GetScansThisStop() >= 1; }, 4.f, [Label, MinDist, MaxDist]()
		{
			const AEntity* E = FindEntity();
			FVector Floor;
			bool bFound = FindSeeingFloor(E, MinDist, MaxDist, Floor);
			if (!bFound)
			{
				bFound = FindSeeingFloor(E, MinDist, MaxDist * 2.f, Floor);
			}
			Check(bFound, FString::Printf(TEXT("%s: a pose %.0f..%.0f cm from the figure that sees it"), *Label, MinDist, MaxDist));
			if (!bFound)
			{
				return -1.f;
			}
			GFound.Floor = Floor;
			StandOn(Floor, E->GetFigureLocation() + FVector(0.f, 0.f, 90.f));
			return 0.4f;
		}));
		S.Add(Do(Label + TEXT(": scan where it is seen"), [Label]()
		{
			const AEntity* E = FindEntity();
			Check(E && E->CanBeSeenFrom(EyeOf(Pawn()), E->GetFigureLocation()), FString::Printf(TEXT("%s: the figure is in sight, %.0f cm away"), *Label,
				E ? FVector::Dist2D(Pawn()->GetActorLocation(), E->GetFigureLocation()) : -1.f));
			Look(E->GetFigureLocation() + FVector(0.f, 0.f, 90.f));
			Lidar()->Scan();
			return 0.35f;
		}));
	}

	static void AddRestoreBattery(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("battery back"), []() { Home()->RestoreState(GFound.SavedBattery, GFound.SavedHome); return 0.f; }));
	}

	static void AddExitCase(TArray<FStep>& S, EStopExitReason Reason)
	{
		const FString Label = FString::Printf(TEXT("exit %s"), LexToString(Reason));
		S.Add(WaitFor(Label + TEXT(": the channel is idle"), []() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }, 40.f, []()
		{
			Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: next exit case"));
			GFound.StartedMark = GFound.Text->Started.Num();
			GFound.JumpsMark = GFound.HomeListener->Jumps;
			return 0.2f;
		}));
		AddStopManifested(S, Label);
		AddSeenScan(S, Label, 350.f, 700.f);
		S.Add(Do(Label + TEXT(": the exit"), [Label, Reason]()
		{
			const AEntity* E = FindEntity();
			Check(E && E->WasShownThisStop() && !Run()->IsEntityFound(), FString::Printf(TEXT("%s: a scan kept its points, not found yet (%s)"), *Label, *LastScanText()));
			Director()->ForceExit(Reason);
			Check(!IsStopped() && Run()->IsEntityFound(), FString::Printf(TEXT("%s: the Stop ended %s, the entity is found"), *Label, LexToString(Reason)));
			return 0.f;
		}));
		const bool bJump = Reason == EStopExitReason::Jumped || Reason == EStopExitReason::RewoundAndJumped;
		S.Add(WaitFor(Label + TEXT(": the message"), []() { return StartedSince(GFound.StartedMark, TEXT("EntityFound")) > 0; }, 30.f, [Label, bJump]()
		{
			const int32 Jumps = GFound.HomeListener->Jumps - GFound.JumpsMark;
			const FString Rows = StartedRows(GFound.StartedMark);
			const FString Want = bJump ? FString::Printf(TEXT("A%d_JumpReturn_"), URunSubsystem::GetActIndexOf(W())) : FString();
			const bool bOrder = bJump ? (Jumps == 1 && Rows.StartsWith(Want) && Rows.Contains(TEXT(" A")) && Rows.EndsWith(TEXT("EntityFound_1"))) : (Jumps == 0 && Rows.EndsWith(TEXT("EntityFound_1")) && !Rows.Contains(TEXT("JumpReturn")));
			Check(bOrder && StartedSince(GFound.StartedMark, TEXT("EntityFound")) == 1,
				FString::Printf(TEXT("%s: %d jump(s); the rows started: %s (%s)"), *Label, Jumps, *Rows, bJump ? TEXT("the return message, then the EntityFound row") : TEXT("the EntityFound row alone")));
			return 0.f;
		}));
		AddRestoreBattery(S);
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
			GFound.SavedBattery = Home()->GetBatteryFraction();
			GFound.SavedHome = Home()->GetHomeSecondsElapsed();
			AEntity* E = FindEntity();
			GFound.SavedSpeed = E->Speed;
			ULidarScanComponent* L = Lidar();
			GFound.bSavedVerify = L->bVerifyInjectedOcclusion;
			L->bVerifyInjectedOcclusion = true;
			// The entity alone injects: the residues leave the director's sources for the test (registered again at the end).
			// Moving them does not work in a package (they do not move there).
			for (TActorIterator<AResidue> It(World); It; ++It)
			{
				GFound.MovedResidues.Emplace(*It, It->GetActorLocation());
				Director()->UnregisterPointSource(TScriptInterface<ILidarPointSource>(*It));
			}
			Run()->SetEntityFound(false, TEXT("tls.EntityFoundTest: start"));
			ResumeTime();
			TS()->ResetCumulativeStoppedSeconds();
			Note(FString::Printf(TEXT("%d residues taken out of the scans; colours: scan %s, entity %s"), GFound.MovedResidues.Num(),
				*UTheLastShiftSettings::GetScanColors().Scan.ToString(), *UTheLastShiftSettings::GetScanColors().Entity.ToString()));
			GRun.Cleanup = []()
			{
				ResumeTime();
				if (AEntity* E = FindEntity())
				{
					E->Speed = GFound.SavedSpeed;
				}
				if (ULidarScanComponent* L = Lidar())
				{
					L->bVerifyInjectedOcclusion = GFound.bSavedVerify;
				}
				for (const TPair<TWeakObjectPtr<AResidue>, FVector>& Moved : GFound.MovedResidues)
				{
					AResidue* R = Moved.Key.Get();
					if (UScanDirectorComponent* D = Director(); R && D)
					{
						D->RegisterPointSource(TScriptInterface<ILidarPointSource>(R));
					}
				}
				if (URunSubsystem* RunSub = Run())
				{
					RunSub->SetEntityFound(GFound.bSavedFound, TEXT("tls.EntityFoundTest: end"));
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
			Check(!Run()->IsEntityFound() && !E->WasShownThisStop(), TEXT("dormant: a Stop without the entity does not make it found"));
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
		S.Add(Do(TEXT("away: Resume"), []()
		{
			const AEntity* E = FindEntity();
			const FLidarInjectionStats Stats = Lidar()->GetLastInjectionStats();
			Check(E->IsManifested() && Stats.Gathered > 0 && Stats.Uploaded == 0 && !E->GetLastKeptCheckResult() && E->GetLastKeptCheckTraces() == 0,
				FString::Printf(TEXT("away: the figure is outside the coverage: nothing uploaded, nothing traced (%s)"), *LastScanText()));
			ResumeTime();
			Check(!Run()->IsEntityFound(), TEXT("away: a Stop in which no scan kept a point does not make it found"));
			return 0.3f;
		}));

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
				GBehind.bAutoGame = E->GetLastKeptCheckResult();
				GBehind.AutoTraces = E->GetLastKeptCheckTraces();
				GBehind.AutoScan = Lidar()->GetLastInjectionStats().ScanIndex;
				GBehind.bAutoChecked = E->GetLastKeptCheckScan() == GBehind.AutoScan;
				Look(E->GetFigureLocation() + FVector(0.f, 0.f, 90.f));
				Lidar()->Scan();
				return 0.3f;
			}));
			S.Add(Do(L + TEXT(": the two scans"), [L]()
			{
				if (GBehind.bDone)
				{
					return 0.f;
				}
				const AEntity* E = FindEntity();
				const FLidarInjectionStats Stats = Lidar()->GetLastInjectionStats();
				GBehind.ManualKept = LidarKept();
				const bool bManualChecked = E->GetLastKeptCheckScan() == Stats.ScanIndex;
				const bool bSeen = E->CanBeSeenFrom(EyeOf(Pawn()), E->GetFigureLocation());
				Note(FString::Printf(TEXT("%s: figure %.0f cm away, in sight %d; automatic scan: the lidar keeps %d, the game %s (%d traces); %s; the lidar keeps %d"), *L,
					FVector::Dist2D(Pawn()->GetActorLocation(), E->GetFigureLocation()), bSeen ? 1 : 0, GBehind.AutoKept,
					GBehind.bAutoChecked ? (GBehind.bAutoGame ? TEXT("kept") : TEXT("none")) : TEXT("did not check"), GBehind.AutoTraces, *LastScanText(), GBehind.ManualKept));
				Check(Stats.Sources == 1, FString::Printf(TEXT("%s: the entity is the only source of the scans (%d)"), *L, Stats.Sources));
				bool bAgree = true;
				if (GBehind.bAutoChecked)
				{
					bAgree &= GBehind.bAutoGame == (GBehind.AutoKept > 0);
				}
				if (bManualChecked)
				{
					bAgree &= E->GetLastKeptCheckResult() == (GBehind.ManualKept > 0);
				}
				Check(bAgree, FString::Printf(TEXT("%s: for each scan it checked, the game's check agrees with the lidar's depth test"), *L));
				const bool bShown = E->WasShownThisStop();
				ResumeTime();
				if (GBehind.AutoKept == 0 && GBehind.ManualKept == 0 && Stats.Uploaded > 0)
				{
					GBehind.bDone = true;
					Check(!bShown, FString::Printf(TEXT("%s: in the coverage (%d points uploaded), every point removed by the lidar, none kept by the game"), *L, Stats.Uploaded));
					Check(!Run()->IsEntityFound(), FString::Printf(TEXT("%s: a Stop in which no scan kept a point does not make it found"), *L));
				}
				else
				{
					Note(FString::Printf(TEXT("%s: part of the figure was in view of a scan (lidar %d and %d, %d uploaded): not the hidden case; found %d"), *L, GBehind.AutoKept,
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
			return 0.f;
		}));

		// --- Seen, not found: the scan's colour ---
		S.Add(Do(TEXT("not found: marks"), []()
		{
			GFound.StartedMark = GFound.Text->Started.Num();
			GFound.JumpsMark = GFound.HomeListener->Jumps;
			return 0.f;
		}));
		AddStopManifested(S, TEXT("not found"));
		AddSeenScan(S, TEXT("not found"), 250.f, 380.f);
		S.Add(Do(TEXT("not found: the scan kept its points"), []()
		{
			const AEntity* E = FindEntity();
			const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(const_cast<AEntity*>(E));
			const int32 Kept = LidarKept();
			Check(E->GetLastKeptCheckResult() && Kept > 0 && E->WasShownThisStop(), FString::Printf(TEXT("not found: seen, the scan keeps its points (%s; lidar %d)"), *LastScanText(), Kept));
			Check(Settings.Color.Equals(UTheLastShiftSettings::GetScanColors().Scan, 1.e-4f) && !Run()->IsEntityFound(),
				FString::Printf(TEXT("not found: its points are in the scan's colour %s"), *Settings.Color.ToString()));
			GFound.NotFoundSettings = Settings;
			GFound.NotFoundFigurePoints = E->GetLastGatheredFigurePoints();
			Note(FString::Printf(TEXT("not found: figure %d points, trail %d points, intensity %.2f"), E->GetLastGatheredFigurePoints(), E->GetLastGatheredTrailPoints(), Settings.IntensityMultiplier));
			return 0.f;
		}));
		AddShot(S, TEXT("Entity_NotFound_ScanColour"), [](const FShot& Shot)
		{
			const FBox2D Rect = FigureRect(FindEntity());
			const int32 Red = CountHue(Shot, EHue::Entity, Rect);
			const int32 Cyan = CountHue(Shot, EHue::Scan, Rect);
			GFound.NotFoundScanHue = Cyan;
			Check(Red >= 0 && Red <= 5 && Cyan > 40, FString::Printf(TEXT("not found: around the figure %d pixels of the entity's red, %d of the scan's colour (rect %s)"), Red, Cyan, *Rect.ToString()));
		});
		S.Add(Do(TEXT("not found: the same settings found, but the colour"), []()
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
		S.Add(Do(TEXT("not found: Resume"), []()
		{
			const AEntity* E = FindEntity();
			const float Path = E->GetPathDistanceToPlayer();
			ResumeTime();
			GFound.bGrazed = E->WasLastExitGraze();
			Check(Run()->IsEntityFound(), FString::Printf(TEXT("a voluntary Resume after a scan that kept its points makes it found (figure at path distance %.0f cm, graze %d)"), Path, GFound.bGrazed ? 1 : 0));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("found: the message"), []() { return StartedSince(GFound.StartedMark, TEXT("EntityFound")) > 0; }, 30.f, []()
		{
			const int32 Jumps = GFound.HomeListener->Jumps - GFound.JumpsMark;
			const FString Rows = StartedRows(GFound.StartedMark);
			const bool bOrder = Jumps > 0 ? (Rows.Contains(TEXT("JumpReturn")) && Rows.Find(TEXT("JumpReturn")) < Rows.Find(TEXT("EntityFound"))) : !Rows.Contains(TEXT("JumpReturn"));
			Check(bOrder && Rows.EndsWith(TEXT("EntityFound_1")) && StartedSince(GFound.StartedMark, TEXT("EntityFound")) == 1,
				FString::Printf(TEXT("found: said once on the return to Normal time, %s: %s"), Jumps > 0 ? TEXT("after the graze's return message") : TEXT("no jump, no return message"), *Rows));
			Check(!GFound.Text->Started.IsEmpty() && GFound.Text->Started.Last().ToString().StartsWith(FString::Printf(TEXT("A%d_"), URunSubsystem::GetActIndexOf(W()))),
				TEXT("found: the act's row"));
			return 0.f;
		}));
		AddRestoreBattery(S);
		S.Add(WaitFor(TEXT("found: the channel is idle"), []() { const UTextChannelComponent* C = Channel(); return C && !C->IsBusy(); }, 40.f, []()
		{
			GFound.StartedMark = GFound.Text->Started.Num();
			return 0.f;
		}));

		// --- Found: the entity's colour from the next Stop ---
		AddStopManifested(S, TEXT("found"));
		AddSeenScan(S, TEXT("found"), 350.f, 700.f);
		S.Add(Do(TEXT("found: its colour"), []()
		{
			AEntity* E = FindEntity();
			const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(E);
			Check(Settings.Color.Equals(UTheLastShiftSettings::GetScanColors().Entity, 1.e-4f), FString::Printf(TEXT("found: its points are in the entity's colour %s"), *Settings.Color.ToString()));
			Check(E->GetLastGatheredFigurePoints() == GFound.NotFoundFigurePoints && Settings.IntensityMultiplier == GFound.NotFoundSettings.IntensityMultiplier,
				FString::Printf(TEXT("found: the figure has the same points (%d, %d before) and intensity (%.2f)"), E->GetLastGatheredFigurePoints(), GFound.NotFoundFigurePoints, Settings.IntensityMultiplier));
			Note(FString::Printf(TEXT("found: %s"), *LastScanText()));
			return 0.f;
		}));
		AddShot(S, TEXT("Entity_Found_EntityColour"), [](const FShot& Shot)
		{
			const FBox2D Rect = FigureRect(FindEntity());
			const int32 Red = CountHue(Shot, EHue::Entity, Rect);
			const int32 Cyan = CountHue(Shot, EHue::Scan, Rect);
			Check(Red > 40, FString::Printf(TEXT("found: around the figure %d pixels of the entity's red, %d of the scan's colour (%d before it was found; rect %s)"), Red, Cyan,
				GFound.NotFoundScanHue, *Rect.ToString()));
		});
		S.Add(Do(TEXT("found: Resume"), []()
		{
			ResumeTime();
			return 4.f;
		}));
		S.Add(Do(TEXT("found: said once in a run"), []()
		{
			Check(StartedSince(GFound.StartedMark, TEXT("EntityFound")) == 0, FString::Printf(TEXT("found: a later Stop that shows it says nothing more (%s)"), *StartedRows(GFound.StartedMark)));
			return 0.f;
		}));
		AddRestoreBattery(S);

		// --- Every way a Stop ends ---
		AddExitCase(S, EStopExitReason::Rewound);
		AddExitCase(S, EStopExitReason::Jumped);
		AddExitCase(S, EStopExitReason::RewoundAndJumped);

		Launch();
	}

	static FAutoConsoleCommandWithWorldAndArgs GEntityFoundTestCommand(
		TEXT("tls.EntityFoundTest"),
		TEXT("tls.EntityFoundTest [Quit] [Shots]: Brief 17, the entity in the scan's colour until found (an area map; Area2_ServerHalls)."),
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
			Out += FString::Printf(TEXT("%s[act %d: battery %.4f, home %.0f s, banked %d, archive %d+%d, entity %s]"), Out.IsEmpty() ? TEXT("") : TEXT(" "), Snap.ActIndex,
				Snap.BatteryFraction, Snap.HomeSecondsElapsed, Snap.BankedEnergy, Snap.Archive.LogOrders.Num(), Snap.Archive.TwinPartsPlayed, Snap.bEntityFound ? TEXT("found") : TEXT("not found"));
		}
		return Out.IsEmpty() ? TEXT("none") : Out;
	}

	static bool SnapshotFound(int32 Act, bool& bOut)
	{
		for (const FAreaSnapshot& Snap : Run()->GetSnapshots())
		{
			if (Snap.ActIndex == Act)
			{
				bOut = Snap.bEntityFound;
				return true;
			}
		}
		return false;
	}

	/** The slot as a URunSaveGame read straight from the disk. */
	static const URunSaveGame* ReadSlot()
	{
		return Cast<URunSaveGame>(UGameplayStatics::LoadGameFromSlot(URunSubsystem::SaveSlotName(), 0));
	}

	static TArray<FAreaSnapshot> GOldSnapshots;

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
			bool bSnap = true;
			Check(!Run()->IsEntityFound() && SnapshotFound(1, bSnap) && !bSnap, FString::Printf(TEXT("area 1: a new run starts with the entity not found (%s)"), *DescribeSnapshots()));
			Check(Run()->TravelToNextArea(), TEXT("area 1 -> area 2"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("area 2 ready"), []() { return AreaReady(2); }, 90.f, []()
		{
			bool bSnap = true;
			Check(!Run()->IsEntityFound() && SnapshotFound(2, bSnap) && !bSnap, FString::Printf(TEXT("area 2: entered not found, its snapshot not found (%s)"), *DescribeSnapshots()));
			Run()->MarkEntityFound(TEXT("tls.FoundRunTest: as if found in area 2"));
			Check(Run()->IsEntityFound(), TEXT("area 2: found"));
			Check(Run()->TravelToNextArea(), TEXT("area 2 -> area 3"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("area 3 ready"), []() { return AreaReady(3); }, 90.f, []()
		{
			bool bSnap2 = true, bSnap3 = false;
			Check(Run()->IsEntityFound(), TEXT("area 3: the state survived the travel"));
			Check(SnapshotFound(2, bSnap2) && !bSnap2 && SnapshotFound(3, bSnap3) && bSnap3, FString::Printf(TEXT("area 3: its snapshot holds found, area 2's still not found (%s)"), *DescribeSnapshots()));
			const URunSaveGame* Save = ReadSlot();
			bool bDisk3 = false;
			for (const FAreaSnapshot& Snap : Save ? Save->Snapshots : TArray<FAreaSnapshot>())
			{
				bDisk3 |= Snap.ActIndex == 3 && Snap.bEntityFound;
			}
			Check(Save && Save->Version == 3 && URunSaveGame::CurrentVersion == 3 && GetDefault<URunSaveGame>()->Version == 1 && bDisk3,
				FString::Printf(TEXT("the save on disk is version %d with area 3's snapshot found (class default %d)"), Save ? Save->Version : -1, GetDefault<URunSaveGame>()->Version));
			// Continue reads the save: forget the state in memory first.
			Check(Run()->LoadSave() && Run()->GetLoadedSaveVersion() == 3, FString::Printf(TEXT("the save reads back (version %d)"), Run()->GetLoadedSaveVersion()));
			Run()->SetEntityFound(false, TEXT("tls.FoundRunTest: before Continue"));
			Check(Run()->ContinueRun(), TEXT("Continue"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("Continue: area 3 ready"), []() { return AreaReady(3); }, 90.f, []()
		{
			Check(Run()->IsEntityFound(), TEXT("Continue: the entity found, as on entering act 3"));
			Check(Run()->RestartFromAct(2), TEXT("restart from act 2"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("restart: area 2 ready"), []() { return AreaReady(2); }, 90.f, []()
		{
			bool bSnap3 = false;
			Check(!Run()->IsEntityFound() && !SnapshotFound(3, bSnap3), FString::Printf(TEXT("restart from act 2: not found, as on entering act 2; act 3's snapshot discarded (%s)"), *DescribeSnapshots()));
			Run()->MarkEntityFound(TEXT("tls.FoundRunTest: before restart from act 1"));
			Check(Run()->RestartFromAct(1), TEXT("restart from act 1"));
			Check(!Run()->IsEntityFound(), TEXT("restart from act 1 (a new run): not found"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("restart: area 1 ready"), []() { return AreaReady(1); }, 90.f, []()
		{
			Check(!Run()->IsEntityFound(), TEXT("area 1 after the restart: not found"));
			// A save written before this brief (version 2: no entity state in its snapshots).
			const FString Old = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief17"), TEXT("Run_preBrief17.sav"));
			const bool bHave = IFileManager::Get().FileExists(*Old);
			Check(bHave, FString::Printf(TEXT("a save written before Brief 17 is at hand (%s)"), *Old));
			if (bHave)
			{
				Run()->MarkEntityFound(TEXT("tls.FoundRunTest: before the old save"));
				const bool bCopied = IFileManager::Get().Copy(*SlotPath(), *Old, true) == COPY_OK;
				const bool bLoaded = Run()->LoadSave();
				bool bAnyFound = false;
				for (const FAreaSnapshot& Snap : Run()->GetSnapshots())
				{
					bAnyFound |= Snap.bEntityFound;
				}
				GOldSnapshots = Run()->GetSnapshots();
				Check(bCopied && bLoaded && Run()->GetLoadedSaveVersion() == 2 && GOldSnapshots.Num() > 0 && !bAnyFound,
					FString::Printf(TEXT("the version 2 save loads with the entity not found in every snapshot (version %d: %s)"), Run()->GetLoadedSaveVersion(), *DescribeSnapshots()));
				// Written again by this build and read back: everything else as it was (a restart from act 1 would be a new run).
				if (GOldSnapshots.Last().ActIndex >= 2)
				{
					Run()->RestartFromAct(GOldSnapshots.Last().ActIndex);
				}
				else
				{
					Note(TEXT("the old save's latest act is 1: not written again (a restart from act 1 is a new run)"));
				}
			}
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("old save: its latest area ready"), []() { return GOldSnapshots.Num() > 0 && (GOldSnapshots.Last().ActIndex < 2 || AreaReady(GOldSnapshots.Last().ActIndex)); }, 90.f, []()
		{
			Check(!Run()->IsEntityFound(), TEXT("Continue from the version 2 save: not found"));
			const URunSaveGame* Save = ReadSlot();
			bool bSame = GOldSnapshots.Last().ActIndex < 2 || (Save && Save->Version == 3);
			for (const FAreaSnapshot& Old : GOldSnapshots.Last().ActIndex < 2 ? TArray<FAreaSnapshot>() : GOldSnapshots)
			{
				const FAreaSnapshot* New = Save ? Save->Snapshots.FindByPredicate([&Old](const FAreaSnapshot& X) { return X.ActIndex == Old.ActIndex; }) : nullptr;
				bSame &= New && New->BatteryFraction == Old.BatteryFraction && New->HomeSecondsElapsed == Old.HomeSecondsElapsed && New->BankedEnergy == Old.BankedEnergy
					&& New->Archive.LogOrders == Old.Archive.LogOrders && New->Archive.TwinPartsPlayed == Old.Archive.TwinPartsPlayed && !New->bEntityFound;
			}
			Check(bSame, FString::Printf(TEXT("the version 2 save written again as version 3 keeps battery, home time, energy and archive (%s)"), *DescribeSnapshots()));
			// A save with no Version (Brief 8's fixture, written before version 2) still loads as 1.
			const FString V1 = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Brief8"), TEXT("Run_preBrief8.sav"));
			if (IFileManager::Get().FileExists(*V1) && IFileManager::Get().Copy(*SlotPath(), *V1, true) == COPY_OK)
			{
				Run()->MarkEntityFound(TEXT("tls.FoundRunTest: before the version 1 save"));
				const bool bLoaded = Run()->LoadSave();
				bool bAnyFound = false;
				for (const FAreaSnapshot& Snap : Run()->GetSnapshots())
				{
					bAnyFound |= Snap.bEntityFound;
				}
				Check(bLoaded && Run()->GetLoadedSaveVersion() == 1 && !bAnyFound, FString::Printf(TEXT("a save with no Version loads as 1, the entity not found (%s)"), *DescribeSnapshots()));
			}
			else
			{
				Check(false, FString::Printf(TEXT("the version 1 save is at hand (%s)"), *V1));
			}
			// New game: the state in memory is a new run's.
			Run()->MarkEntityFound(TEXT("tls.FoundRunTest: before New game"));
			Check(Run()->StartNewGame() && !Run()->IsEntityFound(), TEXT("New game starts with the entity not found"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("New game: the travel"), []() { return !Transitioning() && Pawn() != nullptr; }, 90.f, []()
		{
			Check(!Run()->IsEntityFound(), FString::Printf(TEXT("New game: still not found in %s"), *MapName()));
			return 0.f;
		}));
		Launch();
	}

	static FAutoConsoleCommandWithWorldAndArgs GFoundRunTestCommand(
		TEXT("tls.FoundRunTest"),
		TEXT("tls.FoundRunTest [Quit]: Brief 17, the entity-found state across travel, snapshots, the save (version 3), Continue, restarts and New game (MainMenu). Clears the save slot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
		{
			StartFoundRunTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
		}));
}

#endif // !UE_BUILD_SHIPPING

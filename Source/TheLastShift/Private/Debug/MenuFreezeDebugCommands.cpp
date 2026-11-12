// SPDX-License-Identifier: MIT

// tls.MenuFreezeTest (Brief 7): opens StdCore's pause menu in Normal time and while Stopped and checks that the run
// stands still behind it: door timer, message, first-scan delay, residue hold, cooldown, beat, entity, cumulative
// stopped seconds, scan requests and the contact check. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Components/SpotLightComponent.h"
#include "Containers/Ticker.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Entity/Entity.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "LidarScanComponent.h"
#include "Misc/Paths.h"
#include "Residue/Residue.h"
#include "Scan/ScanDirectorComponent.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace MenuFreezeDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner on the core ticker: it keeps running while the menu pauses the world ---

	struct FStep
	{
		FString Label;
		TFunction<float()> Do;
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	struct FRun
	{
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		float MenuSeconds = 30.f;
		float ShortMenuSeconds = 10.f;
		/** Exit the game at the end (packaged runs started with -ExecCmds). */
		bool bQuit = false;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FRun GRun;

	static const TCHAR* Name = TEXT("MenuFreezeTest");

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), Name, bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), Name, *What);
	}

	static double Real() { return FPlatformTime::Seconds(); }

	// --- World access ---

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static AStdPlayerController* StdPC() { return Cast<AStdPlayerController>(PC()); }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UScanDirectorComponent* Director() { UWorld* World = W(); return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; }
	static ULidarScanComponent* Lidar() { const UScanDirectorComponent* D = Director(); return D ? D->GetLidarScan() : nullptr; }
	static UTextChannelComponent* Channel() { const APawn* P = Pawn(); return P ? P->FindComponentByClass<UTextChannelComponent>() : nullptr; }

	template <class T>
	static T* First(TFunction<bool(const T*)> Filter = nullptr)
	{
		UWorld* World = W();
		TArray<T*> Found;
		for (TActorIterator<T> It(World); World && It; ++It)
		{
			if (!Filter || Filter(*It))
			{
				Found.Add(*It);
			}
		}
		Found.Sort([](const T& A, const T& B) { return A.GetName() < B.GetName(); });
		return Found.Num() > 0 ? Found[0] : nullptr;
	}

	static AEntity* Entity() { return First<AEntity>(); }
	static AResidue* Residue() { return First<AResidue>([](const AResidue* R) { return !R->bRegenerates && R->GetState() == EResidueState::Available; }); }

	static APowerTerminal* DoorTerminal()
	{
		return First<APowerTerminal>([](const APowerTerminal* T)
		{
			for (const APoweredDevice* Target : T->Targets)
			{
				if (const ADoorDevice* D = Cast<ADoorDevice>(Target); D && D->PoweredSeconds > 0.f)
				{
					return true;
				}
			}
			return false;
		});
	}

	static ADoorDevice* TimedDoor()
	{
		if (const APowerTerminal* T = DoorTerminal())
		{
			for (APoweredDevice* Target : T->Targets)
			{
				if (ADoorDevice* D = Cast<ADoorDevice>(Target); D && D->PoweredSeconds > 0.f)
				{
					return D;
				}
			}
		}
		return nullptr;
	}

	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static bool IsMenuOpen() { const AStdPlayerController* C = StdPC(); return C && C->IsPausedByMenu(); }
	static bool IsWorldPaused() { const UWorld* World = W(); return World && World->IsPaused(); }
	static double WorldNow() { const UWorld* World = W(); return World ? World->GetTimeSeconds() : 0.0; }
	static int32 ScanCount() { const ULidarScanComponent* L = Lidar(); return L ? L->GetScanCount() : -1; }

	static bool IsFlashlightOn()
	{
		const APawn* P = Pawn();
		if (!P)
		{
			return false;
		}
		TArray<USpotLightComponent*> Lights;
		P->GetComponents(Lights);
		for (const USpotLightComponent* Light : Lights)
		{
			if (Light->IsVisible())
			{
				return true;
			}
		}
		return false;
	}

	static void Teleport(const FVector& Location)
	{
		if (APawn* P = Pawn())
		{
			P->SetActorLocation(FVector(Location.X, Location.Y, P->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			if (const ACharacter* Character = Cast<ACharacter>(P))
			{
				Character->GetCharacterMovement()->StopMovementImmediately();
			}
		}
	}

	static void Key(FName PawnAction, EInputEvent Event)
	{
		if (APlayerController* C = PC())
		{
			C->InputKey(FInputKeyEventArgs::CreateSimulated(TimeStateDebug::BoundKey(C->GetWorld(), PawnAction), Event, Event == IE_Pressed ? 1.f : 0.f));
		}
	}

	static bool OpenMenu()
	{
		AStdPlayerController* C = StdPC();
		const bool bOk = C && C->PauseGame();
		const UTimeStateSubsystem* T = TS();
		Note(FString::Printf(TEXT("pause menu opened: %d (world paused %d)"), bOk ? 1 : 0, IsWorldPaused() ? 1 : 0));
		Check(T && T->IsRunFrozen(), TEXT("the time state reports the run frozen while the menu is open"));
		return bOk;
	}

	static bool CloseMenu()
	{
		AStdPlayerController* C = StdPC();
		const bool bOk = C && C->ResumeGame();
		const UTimeStateSubsystem* T = TS();
		Note(FString::Printf(TEXT("pause menu closed: %d (world paused %d)"), bOk ? 1 : 0, IsWorldPaused() ? 1 : 0));
		Check(T && !T->IsRunFrozen(), TEXT("the time state reports the run running once the menu is closed"));
		return bOk;
	}

	static void Shot(const FString& Label)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief7"), Label + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		Note(FString::Printf(TEXT("screenshot requested: %s"), *Path));
	}

	static FStep Do(const FString& Label, TFunction<float()> Fn)
	{
		FStep Step;
		Step.Label = Label;
		Step.Do = MoveTemp(Fn);
		return Step;
	}

	static FStep Wait(float Seconds) { return Do(FString::Printf(TEXT("wait %.2f s"), Seconds), [Seconds]() { return Seconds; }); }

	static FStep WaitFor(const FString& Label, TFunction<bool()> Until, float Timeout, TFunction<float()> Then = nullptr)
	{
		FStep Step;
		Step.Label = Label;
		Step.Until = MoveTemp(Until);
		Step.Timeout = Timeout;
		Step.Do = MoveTemp(Then);
		return Step;
	}

	// --- Snapshot of the stopped run ---

	struct FSnapshot
	{
		double Real = 0.0;
		double World = 0.0;
		bool bStopped = false;
		bool bFlashlight = false;
		float CooldownFraction = 0.f;
		float CooldownRemaining = 0.f;
		int32 Beats = 0;
		float TimeToBeat = 0.f;
		float Cumulative = 0.f;
		float StopDuration = 0.f;
		int32 Scans = 0;
		float EntityPath = -1.f;
		FVector Figure = FVector::ZeroVector;
		bool bManifested = false;
	};

	static FSnapshot Snap()
	{
		FSnapshot S;
		S.Real = Real();
		S.World = WorldNow();
		S.bStopped = IsStopped();
		S.bFlashlight = IsFlashlightOn();
		if (const UScanDirectorComponent* D = Director())
		{
			S.CooldownFraction = D->GetCooldownFraction();
			S.CooldownRemaining = D->GetCooldownRemaining();
			S.Beats = D->GetBeatCount();
			S.TimeToBeat = D->GetTimeToNextBeat();
		}
		if (const UTimeStateSubsystem* T = TS())
		{
			S.Cumulative = T->GetCumulativeStoppedSeconds();
			S.StopDuration = T->GetCurrentStopDuration();
		}
		S.Scans = ScanCount();
		if (const AEntity* E = Entity())
		{
			S.bManifested = E->IsManifested();
			S.EntityPath = E->GetPathDistanceToPlayer();
			S.Figure = E->GetFigureLocation();
		}
		return S;
	}

	static FString Describe(const FSnapshot& S)
	{
		return FString::Printf(TEXT("stopped %d, flashlight %d, cooldown fraction %.4f (%.3f s left), beats %d (next in %.3f s), cumulative %.3f s, stop %.3f s, scans %d, entity %s path %.1f cm at %s, world t %.3f"),
			S.bStopped ? 1 : 0, S.bFlashlight ? 1 : 0, S.CooldownFraction, S.CooldownRemaining, S.Beats, S.TimeToBeat, S.Cumulative, S.StopDuration, S.Scans,
			S.bManifested ? TEXT("manifested") : TEXT("hidden"), S.EntityPath, *S.Figure.ToCompactString(), S.World);
	}

	static constexpr float TimeTolerance = 0.05f;
	static constexpr float DistanceTolerance = 5.f;

	static void CompareSnapshots(const TCHAR* Phase, const FSnapshot& A, const FSnapshot& B)
	{
		const float Real = static_cast<float>(B.Real - A.Real);
		Note(FString::Printf(TEXT("%s: %.2f real s between the two readings"), Phase, Real));
		Check(A.bStopped == B.bStopped, FString::Printf(TEXT("%s: time state unchanged (stopped %d -> %d)"), Phase, A.bStopped ? 1 : 0, B.bStopped ? 1 : 0));
		Check(A.bFlashlight == B.bFlashlight, FString::Printf(TEXT("%s: flashlight unchanged (%d -> %d)"), Phase, A.bFlashlight ? 1 : 0, B.bFlashlight ? 1 : 0));
		Check(FMath::Abs(B.CooldownFraction - A.CooldownFraction) <= TimeTolerance / 4.5f,
			FString::Printf(TEXT("%s: cooldown fraction %.4f -> %.4f"), Phase, A.CooldownFraction, B.CooldownFraction));
		Check(B.Beats == A.Beats && FMath::Abs(B.TimeToBeat - A.TimeToBeat) <= TimeTolerance,
			FString::Printf(TEXT("%s: beats %d -> %d, next beat in %.3f -> %.3f s"), Phase, A.Beats, B.Beats, A.TimeToBeat, B.TimeToBeat));
		Check(FMath::Abs(B.Cumulative - A.Cumulative) <= TimeTolerance,
			FString::Printf(TEXT("%s: cumulative stopped %.3f -> %.3f s"), Phase, A.Cumulative, B.Cumulative));
		Check(B.Scans == A.Scans, FString::Printf(TEXT("%s: no scan fired (scan count %d -> %d)"), Phase, A.Scans, B.Scans));
		if (A.bManifested)
		{
			Check(B.bManifested && FMath::Abs(B.EntityPath - A.EntityPath) <= DistanceTolerance && FVector::Dist(A.Figure, B.Figure) <= DistanceTolerance,
				FString::Printf(TEXT("%s: entity path distance %.1f -> %.1f cm, figure moved %.1f cm"), Phase, A.EntityPath, B.EntityPath, FVector::Dist(A.Figure, B.Figure)));
		}
	}

	// --- State shared between steps ---

	struct FState
	{
		FSnapshot Before;
		FSnapshot During;
		double PhaseStartReal = 0.0;
		double MenuOpenReal = 0.0;
		double MenuCloseReal = 0.0;
		double EventReal = -1.0;
		int32 ScansAtStart = 0;
		int32 BeatsAtStart = 0;
		double FirstScanReal = -1.0;
		double FirstBeatReal = -1.0;
		float RemainingBefore = 0.f;
		int32 CharsBefore = 0;
		float ProgressBefore = 0.f;
		TWeakObjectPtr<AResidue> Residue;
		bool bFlashlightBefore = false;
		FTSTicker::FDelegateHandle Watcher;
		bool bCloseSeen = false;
		int32 ExitsDuringMenu = 0;
		bool bStoppedAtClose = false;
	};
	static FState G;

	/** Watches the scan count and beat count every frame (core ticker), so their real times are known to a frame. */
	static bool WatchFirstScan(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		const UScanDirectorComponent* D = Director();
		const double Now = Real();
		if (G.FirstScanReal < 0.0 && ScanCount() > G.ScansAtStart)
		{
			G.FirstScanReal = Now;
		}
		if (G.FirstBeatReal < 0.0 && D && D->GetBeatCount() > G.BeatsAtStart)
		{
			G.FirstBeatReal = Now;
		}
		if (G.Residue.IsValid() && G.EventReal < 0.0 && G.Residue->GetState() == EResidueState::Collected)
		{
			G.EventReal = Now;
		}
		return true;
	}

	static void StartWatcher()
	{
		FTSTicker::GetCoreTicker().RemoveTicker(G.Watcher);
		G.Watcher = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&WatchFirstScan));
	}

	static void StopWatcher()
	{
		FTSTicker::GetCoreTicker().RemoveTicker(G.Watcher);
		G.Watcher.Reset();
	}

	/** A Stop that keeps a dormant entity dormant (cumulative count restarted). */
	static void StopDormant()
	{
		if (UTimeStateSubsystem* T = TS())
		{
			if (!T->IsStopped())
			{
				T->Stop();
			}
			T->ResetCumulativeStoppedSeconds();
		}
	}

	static void ResumeTime()
	{
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->Resume(EStopExitReason::Resumed);
		}
	}

	// --- Phases ---

	/** Criterion 4 and 5 (Normal): a powered timed door and a message being typed. */
	static void AddNormalPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("normal: pay a timed door"), []()
		{
			ResumeTime();
			APowerTerminal* Terminal = DoorTerminal();
			ADoorDevice* Door = TimedDoor();
			UEnergySubsystem* Energy = UEnergySubsystem::Get(W());
			if (!Terminal || !Door || !Energy)
			{
				Check(false, TEXT("normal: the map has a terminal powering a timed door"));
				return -1.f;
			}
			Energy->AddBankedEnergy(Terminal->GetCost());
			Teleport(Terminal->GetActorLocation() + Terminal->GetActorForwardVector() * 90.f);
			const bool bPaid = Terminal->TryActivate(Pawn());
			Check(bPaid && Door->IsPowered() && Door->IsTimed(), FString::Printf(TEXT("normal: %s pays %s (powered %d, timed %d, %.2f s left)"),
				*Terminal->GetName(), *Door->GetName(), Door->IsPowered() ? 1 : 0, Door->IsTimed() ? 1 : 0, Door->GetRemainingSeconds()));
			return 1.f;
		}));
		S.Add(Do(TEXT("normal: push a fixture message"), []()
		{
			UTextChannelComponent* Text = Channel();
			if (!Text)
			{
				Check(false, TEXT("normal: the pawn has a text channel"));
				return -1.f;
			}
			Text->ClearAll();
			FTextChannelContext Context;
			Context.BatteryFraction = 1.0;
			Text->PushText(TEXT("Fixture line for the menu freeze test, long enough to be typed while the menu opens over it."), Context);
			return 0.8f;
		}));
		S.Add(Do(TEXT("normal: open the menu"), []()
		{
			const ADoorDevice* Door = TimedDoor();
			const UTextChannelComponent* Text = Channel();
			G.RemainingBefore = Door ? Door->GetRemainingSeconds() : -1.f;
			G.CharsBefore = Text ? Text->GetVisibleText().Len() : -1;
			G.bFlashlightBefore = IsFlashlightOn();
			G.Before = Snap();
			Note(FString::Printf(TEXT("normal before: door %.3f s left, message %d chars shown, %s"), G.RemainingBefore, G.CharsBefore, *Describe(G.Before)));
			Check(G.CharsBefore > 0, FString::Printf(TEXT("normal: the message is being typed (%d chars shown)"), G.CharsBefore));
			Check(OpenMenu(), TEXT("normal: the pause menu opens"));
			G.MenuOpenReal = Real();
			return GRun.MenuSeconds * 0.5f;
		}));
		S.Add(Do(TEXT("normal: halfway"), []()
		{
			const ADoorDevice* Door = TimedDoor();
			const UTextChannelComponent* Text = Channel();
			Note(FString::Printf(TEXT("normal halfway (%.1f s of menu): door %.3f s left, powered %d; channel suspended %d visible %d"), Real() - G.MenuOpenReal,
				Door ? Door->GetRemainingSeconds() : -1.f, Door && Door->IsPowered() ? 1 : 0, Text && Text->IsSuspended() ? 1 : 0, Text && Text->IsVisible() ? 1 : 0));
			return GRun.MenuSeconds * 0.5f;
		}));
		S.Add(Do(TEXT("normal: close the menu"), []()
		{
			const double Open = Real() - G.MenuOpenReal;
			Check(CloseMenu(), TEXT("normal: the pause menu closes"));
			const ADoorDevice* Door = TimedDoor();
			const float After = Door ? Door->GetRemainingSeconds() : -1.f;
			const FSnapshot Now = Snap();
			Note(FString::Printf(TEXT("normal after %.2f s of menu: door %.3f s left, %s"), Open, After, *Describe(Now)));
			Check(Door && Door->IsPowered() && FMath::Abs(After - G.RemainingBefore) <= TimeTolerance,
				FString::Printf(TEXT("normal: door time kept (%.3f -> %.3f s left, powered %d)"), G.RemainingBefore, After, Door && Door->IsPowered() ? 1 : 0));
			Check(!Now.bStopped, TEXT("normal: time is still Normal"));
			Check(Now.bFlashlight == G.bFlashlightBefore, FString::Printf(TEXT("normal: flashlight unchanged (%d -> %d)"), G.bFlashlightBefore ? 1 : 0, Now.bFlashlight ? 1 : 0));
			return 0.f;
		}));
		S.Add(Do(TEXT("normal: message after the menu"), []()
		{
			const UTextChannelComponent* Text = Channel();
			const int32 Chars = Text ? Text->GetVisibleText().Len() : -1;
			const bool bRestarted = Chars < G.CharsBefore;
			Note(FString::Printf(TEXT("normal: message shows %d chars on the frame after the menu (%d before): %s"), Chars, G.CharsBefore,
				bRestarted ? TEXT("restarted from its first character") : TEXT("continued")));
			Check(Text && Text->IsBusy() && !Text->IsSuspended(), TEXT("normal: the message is still there and the channel runs again"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("normal: clean up"), []()
		{
			if (UTextChannelComponent* Text = Channel())
			{
				Text->ClearAll();
			}
			return 0.2f;
		}));
	}

	/** Criterion 3: the menu during the delay before the automatic first scan. */
	static void AddFirstScanPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("first scan: stop"), []()
		{
			G.ScansAtStart = ScanCount();
			G.BeatsAtStart = 0;
			G.FirstScanReal = -1.0;
			G.FirstBeatReal = -1.0;
			G.Residue.Reset();
			StartWatcher();
			StopDormant();
			G.PhaseStartReal = Real();
			return 0.3f;
		}));
		S.Add(Do(TEXT("first scan: open the menu"), []()
		{
			G.Before = Snap();
			Note(FString::Printf(TEXT("first scan before: %s"), *Describe(G.Before)));
			Check(G.FirstScanReal < 0.0, TEXT("first scan: not fired yet when the menu opens"));
			Check(OpenMenu(), TEXT("first scan: the pause menu opens"));
			G.MenuOpenReal = Real();
			return GRun.ShortMenuSeconds;
		}));
		S.Add(Do(TEXT("first scan: close the menu"), []()
		{
			const bool bFiredDuringMenu = G.FirstScanReal >= 0.0;
			Check(CloseMenu(), TEXT("first scan: the pause menu closes"));
			G.MenuCloseReal = Real();
			const FSnapshot After = Snap();
			Note(FString::Printf(TEXT("first scan after %.2f s of menu: %s"), G.MenuCloseReal - G.MenuOpenReal, *Describe(After)));
			Check(!bFiredDuringMenu, TEXT("first scan: no scan fired while the menu was open"));
			Check(After.bStopped && FMath::Abs(After.Cumulative - G.Before.Cumulative) <= TimeTolerance,
				FString::Printf(TEXT("first scan: still stopped, cumulative %.3f -> %.3f s"), G.Before.Cumulative, After.Cumulative));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("first scan: the automatic scan fires after the menu"), []() { return G.FirstScanReal >= 0.0; }, 5.f));
		S.Add(WaitFor(TEXT("first scan: the first beat arrives"), []() { return G.FirstBeatReal >= 0.0; }, 8.f, []()
		{
			const UScanDirectorComponent* D = Director();
			const float Menu = static_cast<float>(G.MenuCloseReal - G.MenuOpenReal);
			const float Delay = D ? FMath::Clamp(D->FirstScanDelay, 0.2f, 3.f) : 1.f;
			const float Beat = D ? FMath::Clamp(D->BeatInterval, 0.5f, 15.f) : 4.5f;
			const float ScanAt = static_cast<float>(G.FirstScanReal - G.PhaseStartReal);
			const float BeatAt = static_cast<float>(G.FirstBeatReal - G.PhaseStartReal);
			Note(FString::Printf(TEXT("first scan: menu %.3f s; first scan at %.3f s real after the Stop (delay %.2f), first beat at %.3f s (interval %.2f)"),
				Menu, ScanAt, Delay, BeatAt, Beat));
			Check(FMath::Abs(ScanAt - (Delay + Menu)) <= 0.1f, FString::Printf(TEXT("first scan: delayed by the menu (%.3f s, expected %.3f)"), ScanAt, Delay + Menu));
			Check(FMath::Abs(BeatAt - (Beat + Menu)) <= 0.1f, FString::Printf(TEXT("first scan: beat delayed by the menu (%.3f s, expected %.3f)"), BeatAt, Beat + Menu));
			StopWatcher();
			ResumeTime();
			return 0.5f;
		}));
	}

	/** Criterion 3: the menu during a residue hold. */
	static void AddHoldPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("hold: stop at a residue"), []()
		{
			AResidue* R = Residue();
			if (!R)
			{
				Check(false, TEXT("hold: the map has an available residue"));
				return -1.f;
			}
			G.Residue = R;
			G.EventReal = -1.0;
			StopDormant();
			Teleport(R->GetActorLocation() - FVector(70.f, 0.f, 0.f));
			return 1.3f;
		}));
		S.Add(Do(TEXT("hold: press Interact"), []()
		{
			StartWatcher();
			Key(TEXT("InteractAction"), IE_Pressed);
			G.PhaseStartReal = Real();
			return 1.f;
		}));
		S.Add(Do(TEXT("hold: open the menu"), []()
		{
			const AResidue* R = G.Residue.Get();
			G.ProgressBefore = R ? R->GetCollectProgress() : -1.f;
			Note(FString::Printf(TEXT("hold before: %s collecting %d, progress %.4f"), *GetNameSafe(R), R && R->IsCollecting() ? 1 : 0, G.ProgressBefore));
			Check(R && R->IsCollecting() && G.ProgressBefore > 0.f, TEXT("hold: the hold is in progress when the menu opens"));
			Check(OpenMenu(), TEXT("hold: the pause menu opens"));
			G.MenuOpenReal = Real();
			return GRun.ShortMenuSeconds;
		}));
		S.Add(Do(TEXT("hold: close the menu"), []()
		{
			const AResidue* R = G.Residue.Get();
			const float During = R ? R->GetCollectProgress() : -1.f;
			const bool bCollectedDuring = G.EventReal >= 0.0;
			Check(CloseMenu(), TEXT("hold: the pause menu closes"));
			G.MenuCloseReal = Real();
			const float After = R ? R->GetCollectProgress() : -1.f;
			Note(FString::Printf(TEXT("hold after %.2f s of menu: progress %.4f before, %.4f at the end of the menu, %.4f after; collecting %d, state %s"),
				G.MenuCloseReal - G.MenuOpenReal, G.ProgressBefore, During, After, R && R->IsCollecting() ? 1 : 0, R ? LexToString(R->GetState()) : TEXT("-")));
			Check(!bCollectedDuring && FMath::Abs(After - G.ProgressBefore) <= 0.03f,
				FString::Printf(TEXT("hold: progress kept behind the menu (%.4f -> %.4f), not collected meanwhile"), G.ProgressBefore, After));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("hold: the residue is collected after the menu"), []() { return G.EventReal >= 0.0; }, 6.f, []()
		{
			const AResidue* R = G.Residue.Get();
			const float Needed = R ? FMath::Clamp(R->CollectSeconds, 0.5f, 10.f) : 2.5f;
			const float Menu = static_cast<float>(G.MenuCloseReal - G.MenuOpenReal);
			const float Held = static_cast<float>(G.EventReal - G.PhaseStartReal);
			Note(FString::Printf(TEXT("hold: collected %.3f s real after the press (needs %.2f, menu %.3f)"), Held, Needed, Menu));
			Check(FMath::Abs(Held - (Needed + Menu)) <= 0.15f, FString::Printf(TEXT("hold: collection delayed by the menu (%.3f s, expected %.3f)"), Held, Needed + Menu));
			Key(TEXT("InteractAction"), IE_Released);
			StopWatcher();
			G.Residue.Reset();
			ResumeTime();
			return 0.5f;
		}));
	}

	/** Criterion 2 and 5 (Stopped): the entity awake, a scan in cooldown, the menu open for MenuSeconds. */
	static void AddStoppedPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("stopped: stop and wake the entity"), []()
		{
			AEntity* E = Entity();
			if (!E)
			{
				Check(false, TEXT("stopped: the map has an entity"));
				return -1.f;
			}
			E->SetSpawnDistanceForNextStop(E->InitialSpawnDistance);
			if (UTimeStateSubsystem* T = TS())
			{
				T->Stop();
			}
			E->Awaken();
			return 2.5f;
		}));
		S.Add(Do(TEXT("stopped: open the menu"), []()
		{
			Shot(TEXT("Stopped_BeforeMenu"));
			G.Before = Snap();
			Note(FString::Printf(TEXT("stopped before: %s"), *Describe(G.Before)));
			Check(G.Before.bStopped && G.Before.bManifested && G.Before.Scans > 0 && G.Before.CooldownFraction > 0.f && G.Before.CooldownFraction < 1.f,
				TEXT("stopped: entity manifested, a scan in cooldown"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("stopped: menu"), []()
		{
			G.Before = Snap();
			Check(OpenMenu(), TEXT("stopped: the pause menu opens"));
			G.MenuOpenReal = Real();
			return GRun.MenuSeconds * 0.5f;
		}));
		S.Add(Do(TEXT("stopped: halfway"), []()
		{
			G.During = Snap();
			Note(FString::Printf(TEXT("stopped halfway (%.1f s of menu): %s"), Real() - G.MenuOpenReal, *Describe(G.During)));
			return GRun.MenuSeconds * 0.5f;
		}));
		S.Add(Do(TEXT("stopped: close the menu"), []()
		{
			const FSnapshot End = Snap();
			Check(CloseMenu(), TEXT("stopped: the pause menu closes"));
			const FSnapshot After = Snap();
			Note(FString::Printf(TEXT("stopped at the end of the menu: %s"), *Describe(End)));
			Note(FString::Printf(TEXT("stopped after %.2f s of menu: %s"), After.Real - G.MenuOpenReal, *Describe(After)));
			CompareSnapshots(TEXT("stopped"), G.Before, After);
			return 0.5f;
		}));
		S.Add(Do(TEXT("stopped: screenshot after the menu"), []()
		{
			Shot(TEXT("Stopped_AfterMenu"));
			return 5.f;
		}));
		S.Add(Do(TEXT("stopped: screenshot 5 s later"), []()
		{
			Shot(TEXT("Stopped_AfterMenuPlus5"));
			return 0.f;
		}));
	}

	/** No scan fires because of the menu: a scan request while it is open is refused. */
	static void AddRequestPhase(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("request: a scan is ready"), []() { const UScanDirectorComponent* D = Director(); return D && D->IsScanReady(); }, 6.f, []()
		{
			G.ScansAtStart = ScanCount();
			Check(OpenMenu(), TEXT("request: the pause menu opens"));
			G.MenuOpenReal = Real();
			Key(TEXT("ScanAction"), IE_Pressed);
			return 0.2f;
		}));
		S.Add(Do(TEXT("request: scan through the director"), []()
		{
			Key(TEXT("ScanAction"), IE_Released);
			const int32 AfterKey = ScanCount();
			UScanDirectorComponent* D = Director();
			const bool bAccepted = D && D->RequestScan();
			Note(FString::Printf(TEXT("request: scan key while the menu is open -> scans %d -> %d; director request %s"), G.ScansAtStart, AfterKey,
				bAccepted ? TEXT("ACCEPTED") : TEXT("refused")));
			Check(AfterKey == G.ScansAtStart, TEXT("request: the scan key does nothing while the menu is open"));
			Check(!bAccepted, TEXT("request: the director refuses a scan while the menu is open"));
			if (UTimeStateSubsystem* T = TS())
			{
				T->Toggle();
			}
			Check(IsStopped(), TEXT("request: Stop/Resume (Toggle) is refused while the menu is open"));
			return 1.f;
		}));
		S.Add(Do(TEXT("request: close the menu"), []()
		{
			const int32 Before = ScanCount();
			Check(CloseMenu(), TEXT("request: the pause menu closes"));
			Check(Before == G.ScansAtStart, FString::Printf(TEXT("request: no scan fired while the menu was open (%d -> %d)"), G.ScansAtStart, Before));
			return 0.3f;
		}));
		S.Add(Do(TEXT("request: scan after the menu"), []()
		{
			UScanDirectorComponent* D = Director();
			const bool bAccepted = D && D->RequestScan();
			Check(bAccepted, TEXT("request: a scan is accepted once the menu is closed"));
			return 0.5f;
		}));
	}

	/** Criterion 5: no contact while the menu is open, even with the entity inside the contact radius. */
	static void AddContactPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("contact: open the menu"), []()
		{
			const AEntity* E = Entity();
			if (!E || !E->IsManifested() || !IsStopped())
			{
				Check(false, TEXT("contact: stopped with the entity manifested"));
				return -1.f;
			}
			Check(OpenMenu(), TEXT("contact: the pause menu opens"));
			G.MenuOpenReal = Real();
			// Onto the figure: inside the contact radius while the world is paused.
			Teleport(E->GetFigureLocation());
			const APawn* P = Pawn();
			Note(FString::Printf(TEXT("contact: pawn moved onto the figure (%.1f cm, radius %.1f)"),
				P ? FVector::Dist(E->GetFigureLocation(), P->GetNavAgentLocation()) : -1.f, E->ContactRadius));
			return 3.f;
		}));
		S.Add(Do(TEXT("contact: close the menu"), []()
		{
			G.bStoppedAtClose = IsStopped();
			Check(G.bStoppedAtClose, TEXT("contact: no contact while the menu is open (still stopped after 3 s inside the radius)"));
			Check(CloseMenu(), TEXT("contact: the pause menu closes"));
			G.MenuCloseReal = Real();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("contact: contact once the menu is closed"), []() { return !IsStopped(); }, 2.f, []()
		{
			const UTimeStateSubsystem* T = TS();
			Note(FString::Printf(TEXT("contact: exit %s %.3f s after the menu closed"), T ? LexToString(T->GetLastExitReason()) : TEXT("-"), Real() - G.MenuCloseReal));
			return 0.5f;
		}));
	}

	// --- Run ---

	static void Finish(const TCHAR* Why)
	{
		StopWatcher();
		if (IsMenuOpen())
		{
			CloseMenu();
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.MenuFreezeTest"));
		}
	}

	/** The map is playable: StdCore's controller and the player's director exist, no transition covers the screen, a generated area is ready. */
	static bool IsReady()
	{
		UWorld* World = W();
		if (!World || !StdPC() || !Pawn() || !Director() || !TS())
		{
			return false;
		}
		const UGameInstance* GI = World->GetGameInstance();
		const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		const AAreaGenerator* Generator = AAreaGenerator::Find(World);
		return (!Flow || !Flow->IsTransitioning()) && (!Generator || Generator->IsReady());
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		if (!W() && GRun.Next > 0)
		{
			Finish(TEXT(", the game world is gone"));
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
		FStep& Step = GRun.Steps[GRun.Next];
		if (Step.Until)
		{
			if (GRun.ConditionStartReal < 0.0)
			{
				GRun.ConditionStartReal = Now;
			}
			const bool bMet = Step.Until();
			const bool bTimedOut = Now - GRun.ConditionStartReal > Step.Timeout;
			if (!bMet && !bTimedOut)
			{
				return true;
			}
			Check(bMet, FString::Printf(TEXT("%s (waited %.2f s%s)"), *Step.Label, Now - GRun.ConditionStartReal, bTimedOut && !bMet ? TEXT(", TIMEOUT") : TEXT("")));
			GRun.ConditionStartReal = -1.0;
		}
		++GRun.Next;
		const float Delay = Step.Do ? Step.Do() : 0.f;
		if (Delay < 0.f)
		{
			Finish(*FString::Printf(TEXT(", aborted at step '%s'"), *Step.Label));
			return false;
		}
		GRun.WaitUntilReal = Now + Delay;
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsMenuFreezeTestCommand(
	TEXT("tls.MenuFreezeTest"),
	TEXT("tls.MenuFreezeTest [MenuSeconds=30] [ShortMenuSeconds=10] [Quit=0]: (fresh PIE in TestLidar, or a package in an area with a timed door such as "
		"Area2_ServerHalls; Normal time, entity dormant; it waits until the map is playable) opens StdCore's pause menu "
		"over a powered timed door and a message being typed (Normal), during the delay before the automatic first scan, during a residue hold, "
		"while Stopped with the entity awake and a scan in cooldown, over a ready scan (scan key and director request), and with the pawn inside the "
		"contact radius; checks that nothing advances behind the menu and that everything continues from where it was."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace MenuFreezeDebug;
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.MenuFreezeTest: already running."));
			return;
		}
		GRun = FRun();
		GRun.MenuSeconds = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 2.f, 120.f) : 30.f;
		GRun.ShortMenuSeconds = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 1.f, 60.f) : 10.f;
		GRun.bQuit = Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0;
		G = FState();

		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("the map is playable (StdCore's controller, a pawn with a scan director, area ready, no transition)"), []() { return IsReady(); }, 90.f,
			[]() { return IsReady() ? 1.f : -1.f; }));
		AddNormalPhase(S);
		AddFirstScanPhase(S);
		AddHoldPhase(S);
		AddStoppedPhase(S);
		AddRequestPhase(S);
		AddContactPhase(S);

		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started: %d steps, menu %.1f s (short %.1f s)."), Name, S.Num(), GRun.MenuSeconds, GRun.ShortMenuSeconds);
	}));

#endif // !UE_BUILD_SHIPPING

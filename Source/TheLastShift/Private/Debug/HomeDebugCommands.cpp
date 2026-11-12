// SPDX-License-Identifier: MIT

// Debug console commands for home time, battery, loss, run snapshots and the save (Brief 5): tls.Home.Set,
// tls.Home.Jump, tls.Run.Snapshots, tls.Run.RestartFrom, tls.Run.ClearSave and tls.HomeTest. tls.HomeTest runs on the
// core ticker, so it survives map changes. Development builds only.

#include "Debug/HomeTestListener.h"
#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PoweredDevice.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdMenuPlayerController.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Run/RunActListWidget.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/HomeStateSubsystem.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/StdPauseMenuWidget.h"
#include "UObject/StrongObjectPtr.h"

#if !UE_BUILD_SHIPPING

namespace HomeDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner (core ticker) ---

	struct FStep
	{
		FString Label;
		/** Returns seconds to wait before the next step (< 0 aborts). */
		TFunction<float()> Do;
		/** If set, waits until it returns true (or Timeout real seconds) before Do. */
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
		TStrongObjectPtr<UTlsHomeTestListener> Listener;
		/** World of the step that started a travel: an arrival is a different world. */
		TWeakObjectPtr<UWorld> TravelFrom;
		/** Values remembered between steps. */
		double SavedBattery = 0.0;
		double SavedHome = 0.0;
		int32 SavedBanked = 0;
		int32 SavedJumps = 0;
		int32 SavedLosses = 0;
		bool bSavedPowered = false;
		EEntityContactResponse SavedResponse = EEntityContactResponse::Rewind;
		float SavedGrazeRadius = 0.f;
		TArray<FAreaSnapshot> SavedSnapshots;
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

	static void Finish(const TCHAR* Why)
	{
		if (GRun.Listener)
		{
			GRun.Listener->Unbind();
			GRun.Listener.Reset();
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		const double Now = FPlatformTime::Seconds();
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
		GRun.WaitUntilReal = Now + Delay;
		return true;
	}

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

	// --- World access ---

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static UHomeStateSubsystem* Home() { return UHomeStateSubsystem::Get(W()); }
	static URunSubsystem* Run() { return URunSubsystem::Get(W()); }
	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }
	static int32 Act() { return URunSubsystem::GetActIndexOf(W()); }

	static AEntity* Entity()
	{
		UWorld* World = W();
		if (World)
		{
			for (TActorIterator<AEntity> It(World); It; ++It) { return *It; }
		}
		return nullptr;
	}

	static APoweredDevice* Device()
	{
		UWorld* World = W();
		if (World)
		{
			for (TActorIterator<APoweredDevice> It(World); It; ++It) { return *It; }
		}
		return nullptr;
	}

	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }

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
			T->Resume(EStopExitReason::Resumed);
		}
	}

	static FActTuning Tuning(int32 InAct)
	{
		FActTuning Out;
		UTheLastShiftSettings::Get()->GetActTuning(InAct, Out);
		return Out;
	}

	static FString SnapshotsLine(const TArray<FAreaSnapshot>& List)
	{
		TArray<FString> Parts;
		for (const FAreaSnapshot& S : List)
		{
			Parts.Add(FString::Printf(TEXT("act %d: battery %.4f, home %.0f s, banked %d"), S.ActIndex, S.BatteryFraction, S.HomeSecondsElapsed, S.BankedEnergy));
		}
		return Parts.IsEmpty() ? FString(TEXT("none")) : FString::Join(Parts, TEXT("; "));
	}

	static bool SameSnapshot(const FAreaSnapshot& A, const FAreaSnapshot& B)
	{
		return A.ActIndex == B.ActIndex && FMath::IsNearlyEqual(A.BatteryFraction, B.BatteryFraction, 1.e-9)
			&& FMath::IsNearlyEqual(A.HomeSecondsElapsed, B.HomeSecondsElapsed, 1.e-3) && A.BankedEnergy == B.BankedEnergy;
	}

	static const FAreaSnapshot* FindSnapshot(int32 InAct)
	{
		const URunSubsystem* R = Run();
		return R ? R->GetSnapshots().FindByPredicate([InAct](const FAreaSnapshot& S) { return S.ActIndex == InAct; }) : nullptr;
	}

	static FString StateLine()
	{
		const UHomeStateSubsystem* H = Home();
		return FString::Printf(TEXT("battery %.4f, home %.0f s, banked %d"), H ? H->GetBatteryFraction() : -1.0, H ? H->GetHomeSecondsElapsed() : -1.0,
			Energy() ? Energy()->GetBankedEnergy() : -1);
	}

	static bool StateIs(const FAreaSnapshot& S)
	{
		const UHomeStateSubsystem* H = Home();
		return H && Energy() && FMath::IsNearlyEqual(H->GetBatteryFraction(), S.BatteryFraction, 1.e-9)
			&& FMath::IsNearlyEqual(H->GetHomeSecondsElapsed(), S.HomeSecondsElapsed, 1.e-3) && Energy()->GetBankedEnergy() == S.BankedEnergy;
	}

	static void CheckInput(bool bExpectGame, const FString& Where)
	{
		UWorld* World = W();
		TimeStateDebug::LogInputReport(World, *GRun.Name);
		const TimeStateDebug::FInputReport R = TimeStateDebug::BuildInputReport(World);
		Check(bExpectGame ? R.IsGameReady() : (R.IsMenuReady() && R.bCursorShown),
			FString::Printf(TEXT("input after %s: %s expected (mode %s, viewportFocus %d, mouseCaptured %d, cursor %s, menuFocus %s)"), *Where,
				bExpectGame ? TEXT("game") : TEXT("UI with the cursor"), *R.InputMode, R.bViewportFocused ? 1 : 0, R.bMouseCaptured ? 1 : 0,
				R.bCursorShown ? TEXT("shown") : TEXT("hidden"), R.MenuFocus.IsEmpty() ? TEXT("<none>") : *R.MenuFocus));
	}

	// --- Building blocks ---

	/** Starts listening to the home state (game instance: survives the travels). */
	static FStep Setup(const FString& What)
	{
		return Do(TEXT("setup"), [What]()
		{
			if (!Home() || !Run() || !Energy())
			{
				Check(false, TEXT("needs a game world with the home, run and energy subsystems"));
				return -1.f;
			}
			GRun.Listener.Reset(NewObject<UTlsHomeTestListener>());
			GRun.Listener->Bind(Home());
			Note(FString::Printf(TEXT("%s in %s (act %d): %s, snapshots %s"), *What, *UWorld::RemovePIEPrefix(W()->GetOutermost()->GetName()), Act(),
				*StateLine(), *SnapshotsLine(Run()->GetSnapshots())));
			return 0.f;
		});
	}

	/** Waits for a world other than the one the travel started from, with its area ready (or no area: the menu) and no transition. */
	static FStep WaitArrival(const FString& Label, int32 ExpectedAct, float Settle = 1.0f)
	{
		return WaitFor(Label, []()
		{
			UWorld* World = W();
			if (!World || World == GRun.TravelFrom.Get())
			{
				return false;
			}
			const UGameInstance* GI = World->GetGameInstance();
			const UStdFlowSubsystem* F = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			const AAreaGenerator* Gen = AAreaGenerator::Find(World);
			return (!F || !F->IsTransitioning()) && (!Gen || Gen->IsReady()) && World->GetFirstPlayerController() != nullptr;
		}, 90.f, [Label, ExpectedAct, Settle]()
		{
			Check(Act() == ExpectedAct, FString::Printf(TEXT("%s: arrived in %s, act %d (expected %d): %s"), *Label,
				*UWorld::RemovePIEPrefix(W()->GetOutermost()->GetName()), Act(), ExpectedAct, *StateLine()));
			GRun.TravelFrom.Reset();
			return Settle;
		});
	}

	/** Marks the current world as the one a travel leaves. */
	static void TravelStarted() { GRun.TravelFrom = W(); }

	/**
	 * Stop, the figure spawned at the minimum distance, the pawn put on it: the contact ends the Stop with Response
	 * (or the map's own response when bMapResponse). Then checks the jump against the act's tuning.
	 */
	static void AddContact(TArray<FStep>& S, const FString& Label, TOptional<EEntityContactResponse> Response, bool bCheckDevice)
	{
		S.Add(Do(Label + TEXT(": stop and contact"), [Response, bCheckDevice]()
		{
			AEntity* E = Entity();
			APawn* P = Pawn();
			if (!E || !P || !Home())
			{
				Check(false, TEXT("contact: needs an entity and a pawn"));
				return -1.f;
			}
			GRun.SavedResponse = E->ContactResponse;
			if (Response.IsSet())
			{
				E->ContactResponse = Response.GetValue();
			}
			APoweredDevice* D = bCheckDevice ? Device() : nullptr;
			if (D)
			{
				D->PowerOn();
			}
			GRun.bSavedPowered = D && D->IsPowered();
			GRun.SavedBattery = Home()->GetBatteryFraction();
			GRun.SavedHome = Home()->GetHomeSecondsElapsed();
			GRun.SavedJumps = GRun.Listener->Jumps;
			GRun.SavedLosses = GRun.Listener->Losses;
			StopTime();
			const bool bSpawned = E->SpawnNow(E->MinSpawnDistance);
			// Feet on the figure: the next tick's contact check fires.
			const float FeetToActor = P->GetActorLocation().Z - P->GetNavAgentLocation().Z;
			P->SetActorLocation(E->GetFigureLocation() + FVector(0.f, 0.f, FeetToActor + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			Note(FString::Printf(TEXT("stopped in act %d, entity spawned %d, response %s, device %s powered %d"), Act(), bSpawned ? 1 : 0,
				LexToString(E->ContactResponse), *GetNameSafe(D), GRun.bSavedPowered ? 1 : 0));
			return bSpawned ? 0.f : -1.f;
		}));
		S.Add(WaitFor(Label + TEXT(": contact ends the Stop"), []() { return !IsStopped(); }, 10.f, [Label, bCheckDevice]()
		{
			const AEntity* E = Entity();
			const UHomeStateSubsystem* H = Home();
			const EEntityContactResponse R = E ? E->ContactResponse : EEntityContactResponse::Rewind;
			const int32 A = Act();
			const FActTuning T = Tuning(A);
			const int32 NewJumps = GRun.Listener->Jumps - GRun.SavedJumps;
			const double Added = H->GetHomeSecondsElapsed() - GRun.SavedHome;
			const double Dropped = GRun.SavedBattery - H->GetBatteryFraction();
			if (R == EEntityContactResponse::Rewind)
			{
				Check(NewJumps == 0 && Added == 0.0 && Dropped == 0.0, FString::Printf(TEXT("%s: Rewind in act %d: no jump (%d events), home +%.0f s, battery -%.6f"),
					*Label, A, NewJumps, Added, Dropped));
			}
			else
			{
				const FHomeJump& J = GRun.Listener->LastJump;
				const double ExpectedDrop = FMath::Min(T.JumpBatteryDrop, GRun.SavedBattery);
				Check(NewJumps == 1 && J.Cause == EHomeJumpCause::Contact && J.ActIndex == A && J.HomeSecondsAdded == T.JumpHomeSeconds && Added == T.JumpHomeSeconds
					&& FMath::IsNearlyEqual(Dropped, ExpectedDrop, 1.e-9) && J.BatteryBefore == GRun.SavedBattery && J.BatteryAfter == H->GetBatteryFraction(),
					FString::Printf(TEXT("%s: %s in act %d: %d OnHomeJump (cause %s, act %d, stopped %.2f s), home +%.0f s (JumpHomeSeconds %.0f), battery %.6f -> %.6f = -%.9f (JumpBatteryDrop %.6f)"),
						*Label, LexToString(R), A, NewJumps, LexToString(J.Cause), J.ActIndex, J.StoppedSeconds, J.HomeSecondsAdded, T.JumpHomeSeconds,
						J.BatteryBefore, J.BatteryAfter, Dropped, T.JumpBatteryDrop));
			}
			if (bCheckDevice)
			{
				const APoweredDevice* D = Device();
				const bool bExpectedPowered = R == EEntityContactResponse::Rewind ? GRun.bSavedPowered : false;
				Check(!D || !GRun.bSavedPowered || D->IsPowered() == bExpectedPowered, FString::Printf(TEXT("%s: device %s powered %d -> %d (expected %d)"),
					*Label, *GetNameSafe(D), GRun.bSavedPowered ? 1 : 0, D && D->IsPowered() ? 1 : 0, bExpectedPowered ? 1 : 0));
			}
			if (AEntity* Mutable = Entity())
			{
				Mutable->ContactResponse = GRun.SavedResponse;
			}
			return 0.5f;
		}));
	}

	/** Stop, the figure at the minimum distance, the pawn moved to path distance Distance from it, then a voluntary Resume. */
	static void AddGraze(TArray<FStep>& S, float Distance, float GrazeRadius, bool bExpectJump)
	{
		const FString Label = FString::Printf(TEXT("graze %.0f cm, GrazeRadius %.0f"), Distance, GrazeRadius);
		S.Add(Do(Label + TEXT(": stop"), [GrazeRadius]()
		{
			AEntity* E = Entity();
			if (!E || !Pawn())
			{
				Check(false, TEXT("graze: needs an entity and a pawn"));
				return -1.f;
			}
			GRun.SavedGrazeRadius = E->GrazeRadius;
			E->GrazeRadius = GrazeRadius;
			APoweredDevice* D = Device();
			if (D)
			{
				D->PowerOn();
			}
			GRun.bSavedPowered = D && D->IsPowered();
			StopTime();
			return E->SpawnNow(E->MinSpawnDistance) ? 0.1f : -1.f;
		}));
		S.Add(Do(Label + TEXT(": place the pawn"), [Distance]()
		{
			AEntity* E = Entity();
			APawn* P = Pawn();
			// Walk the entity's path (figure first, player last) for Distance.
			const TArray<FVector> Path = E->GetCurrentPath();
			FVector Target = Path.Num() > 0 ? Path.Last() : E->GetFigureLocation();
			float Left = Distance;
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
			const float FeetToActor = P->GetActorLocation().Z - P->GetNavAgentLocation().Z;
			P->SetActorLocation(Target + FVector(0.f, 0.f, FeetToActor + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			Energy()->AddSessionEnergy(1);
			GRun.SavedBanked = Energy()->GetBankedEnergy();
			GRun.SavedBattery = Home()->GetBatteryFraction();
			GRun.SavedHome = Home()->GetHomeSecondsElapsed();
			GRun.SavedJumps = GRun.Listener->Jumps;
			return 0.05f;
		}));
		S.Add(Do(Label + TEXT(": resume"), [Label, bExpectJump]()
		{
			AEntity* E = Entity();
			const float Before = E->GetPathDistanceToPlayer();
			ResumeTime();
			const UHomeStateSubsystem* H = Home();
			const int32 A = Act();
			const FActTuning T = Tuning(A);
			const double F = UTheLastShiftSettings::Get()->GetGrazeJumpFraction();
			const int32 NewJumps = GRun.Listener->Jumps - GRun.SavedJumps;
			const double Added = H->GetHomeSecondsElapsed() - GRun.SavedHome;
			const double Dropped = GRun.SavedBattery - H->GetBatteryFraction();
			const FHomeJump& J = GRun.Listener->LastJump;
			if (bExpectJump)
			{
				Check(NewJumps == 1 && J.Cause == EHomeJumpCause::Graze && J.HomeSecondsAdded == T.JumpHomeSeconds * F && FMath::IsNearlyEqual(Dropped, T.JumpBatteryDrop * F, 1.e-9),
					FString::Printf(TEXT("%s: exit path distance %.1f cm (just before: %.1f), graze %d: %d OnHomeJump (cause %s), home +%.0f s (= %.0f x %.2f), battery -%.9f (= %.6f x %.2f)"),
						*Label, E->GetLastExitPathDistance(), Before, E->WasLastExitGraze() ? 1 : 0, NewJumps, LexToString(J.Cause), Added, T.JumpHomeSeconds, F, Dropped, T.JumpBatteryDrop, F));
			}
			else
			{
				Check(NewJumps == 0 && Added == 0.0 && Dropped == 0.0, FString::Printf(TEXT("%s: exit path distance %.1f cm, graze %d: no jump (%d events, home +%.0f s, battery -%.6f)"),
					*Label, E->GetLastExitPathDistance(), E->WasLastExitGraze() ? 1 : 0, NewJumps, Added, Dropped));
			}
			Check(Energy()->GetBankedEnergy() == GRun.SavedBanked + 1 && Energy()->GetSessionEnergy() == 0,
				FString::Printf(TEXT("%s: still a Resume: the session energy is banked (%d -> %d)"), *Label, GRun.SavedBanked, Energy()->GetBankedEnergy()));
			const APoweredDevice* D = Device();
			Check(!D || !GRun.bSavedPowered || D->IsPowered(), FString::Printf(TEXT("%s: device %s keeps its power (%d)"), *Label, *GetNameSafe(D), D && D->IsPowered() ? 1 : 0));
			E->GrazeRadius = GRun.SavedGrazeRadius;
			return 0.3f;
		}));
	}

	/** Sets the battery so that the act's contact jump takes it to zero, then a contact with Jump: the run is lost. */
	static void AddLoss(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": battery below one jump"), []()
		{
			const FActTuning T = Tuning(Act());
			Home()->RestoreState(FMath::Max(T.JumpBatteryDrop * 0.5, 0.001), Home()->GetHomeSecondsElapsed());
			GRun.SavedSnapshots = Run()->GetSnapshots();
			return 0.f;
		}));
		AddContact(S, Label, EEntityContactResponse::Jump, false);
		S.Add(Do(Label + TEXT(": the run is lost"), [Label]()
		{
			const UHomeStateSubsystem* H = Home();
			const URunActListWidget* Screen = Run()->GetLossScreen();
			const int32 NewLosses = GRun.Listener->Losses - GRun.SavedLosses;
			Check(H->IsRunLost() && H->GetBatteryFraction() == 0.0 && NewLosses == 1,
				FString::Printf(TEXT("%s: lost %d, battery %.6f, OnRunLost %d time(s)"), *Label, H->IsRunLost() ? 1 : 0, H->GetBatteryFraction(), NewLosses));
			// A second contact queued in the same frame: nothing changes.
			const double Battery = H->GetBatteryFraction();
			const double HomeSeconds = H->GetHomeSecondsElapsed();
			const int32 Jumps = GRun.Listener->Jumps;
			const bool bSecond = Home()->ApplyJump(EHomeJumpCause::Contact, Act(), 0.f);
			const bool bThird = Home()->ApplyJump(EHomeJumpCause::Graze, Act(), 0.f);
			Check(!bSecond && !bThird && H->GetBatteryFraction() == Battery && H->GetHomeSecondsElapsed() == HomeSeconds && GRun.Listener->Jumps == Jumps && GRun.Listener->Losses - GRun.SavedLosses == 1,
				FString::Printf(TEXT("%s: two more jumps in the same frame are ignored (applied %d %d, jumps %d -> %d, OnRunLost still %d)"), *Label, bSecond ? 1 : 0, bThird ? 1 : 0, Jumps, GRun.Listener->Jumps, GRun.Listener->Losses - GRun.SavedLosses));
			Check(Screen != nullptr && Screen->GetMode() == ERunActListMode::RunLost, FString::Printf(TEXT("%s: loss screen shown: %s"), *Label, *TimeStateDebug::DescribeChoiceList(W())));
			int32 Matching = 0;
			for (int32 I = 0; Screen && I < GRun.SavedSnapshots.Num(); ++I)
			{
				const FString Expected = FString::Printf(TEXT("Area %d — %d%%"), GRun.SavedSnapshots[I].ActIndex, FMath::RoundToInt(GRun.SavedSnapshots[I].BatteryFraction * 100.0));
				Matching += Screen->GetEntryLabel(I).ToString() == Expected && Screen->GetActOfEntry(I) == GRun.SavedSnapshots[I].ActIndex ? 1 : 0;
			}
			Check(Screen && Matching == GRun.SavedSnapshots.Num() && Screen->GetEntryCount() == GRun.SavedSnapshots.Num() + 1 && Screen->GetActOfEntry(Screen->GetEntryCount() - 1) == 0,
				FString::Printf(TEXT("%s: the list has the %d acts reached with their entry battery, then Main menu (%d match)"), *Label, GRun.SavedSnapshots.Num(), Matching));
			// Input: the time stop key and pause.
			TimeStateDebug::PressKey(W(), TimeStateDebug::BoundKey(W(), TEXT("TimeStopAction")));
			AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC());
			const bool bPaused = StdPC && StdPC->PauseGame();
			Check(!bPaused && (!StdPC || !StdPC->IsPausedByMenu()), FString::Printf(TEXT("%s: the pause menu does not open (PauseGame %d, bCanPause %d)"), *Label, bPaused ? 1 : 0, StdPC && StdPC->bCanPause ? 1 : 0));
			const APawn* P = Pawn();
			Check(P && !P->InputEnabled() && PC()->IsMoveInputIgnored(), FString::Printf(TEXT("%s: pawn input %d, move input ignored %d"), *Label, P && P->InputEnabled() ? 1 : 0, PC()->IsMoveInputIgnored() ? 1 : 0));
			return 0.5f;
		}));
		S.Add(Do(Label + TEXT(": still Normal, UI input"), [Label]()
		{
			Check(!IsStopped(), FString::Printf(TEXT("%s: the time stop key on the loss screen did not stop time"), *Label));
			CheckInput(false, Label + TEXT(" (loss screen)"));
			return 0.f;
		}));
	}

	/** Clicks entry Index of the loss screen (or of Select act) and waits for the arrival in ExpectedAct. */
	static void AddChooseEntry(TArray<FStep>& S, const FString& Label, int32 Index, int32 ExpectedAct)
	{
		S.Add(Do(Label, [Label, Index]()
		{
			TravelStarted();
			const FString Before = TimeStateDebug::DescribeChoiceList(W());
			const bool bClicked = TimeStateDebug::ClickChoiceEntry(W(), Index);
			Check(bClicked, FString::Printf(TEXT("%s: clicked entry %d of [%s]"), *Label, Index, *Before));
			return bClicked ? 0.f : -1.f;
		}));
		S.Add(WaitArrival(Label + TEXT(": arrival"), ExpectedAct));
	}

	static void AddMenuClick(TArray<FStep>& S, const FString& Label, FName Button, int32 ExpectedAct)
	{
		S.Add(Do(Label, [Label, Button]()
		{
			TravelStarted();
			const bool bClicked = TimeStateDebug::ClickMainMenuButton(W(), Button);
			Check(bClicked, FString::Printf(TEXT("%s: clicked %s in [%s]"), *Label, *Button.ToString(), *TimeStateDebug::DescribeMainMenu(W())));
			return bClicked ? 0.f : -1.f;
		}));
		if (ExpectedAct >= 0)
		{
			S.Add(WaitArrival(Label + TEXT(": arrival"), ExpectedAct));
		}
	}

	/** Opens the pause menu and clicks its Main menu, as the player would; waits for the menu. */
	static void AddPauseToMenu(TArray<FStep>& S, const FString& Label)
	{
		S.Add(Do(Label + TEXT(": pause menu, Main menu"), [Label]()
		{
			AStdPlayerController* StdPC = Cast<AStdPlayerController>(PC());
			const bool bPaused = StdPC && StdPC->PauseGame();
			UStdPauseMenuWidget* Menu = StdPC ? StdPC->GetPauseMenu() : nullptr;
			UButton* MainMenu = Menu && Menu->WidgetTree ? Cast<UButton>(Menu->WidgetTree->FindWidget(TEXT("Button_MainMenu"))) : nullptr;
			Check(bPaused && MainMenu, FString::Printf(TEXT("%s: pause menu open %d, Main menu button %d"), *Label, bPaused ? 1 : 0, MainMenu ? 1 : 0));
			if (!MainMenu)
			{
				return -1.f;
			}
			TravelStarted();
			MainMenu->OnClicked.Broadcast();
			return 0.f;
		}));
		S.Add(WaitArrival(Label + TEXT(": in the main menu"), 0));
	}

	/** bCheckInput false: the menu the test was typed in (the editor's Cmd box holds the focus there; see tls.InputReport). */
	static FStep CheckMenu(const FString& Label, bool bSaved, bool bCheckInput = true)
	{
		return Do(Label, [Label, bSaved, bCheckInput]()
		{
			const FString Entries = TimeStateDebug::DescribeMainMenu(W());
			const FString Expected = bSaved ? TEXT("Continue | Select act | New game | Settings | Credits | Quit") : TEXT("New game | Settings | Credits | Quit");
			Check(Entries == Expected && Run()->HasSavedRun() == bSaved, FString::Printf(TEXT("%s: menu [%s] (expected [%s]), saved run %d"), *Label, *Entries, *Expected, Run()->HasSavedRun() ? 1 : 0));
			if (bCheckInput)
			{
				CheckInput(false, Label + TEXT(" (main menu)"));
			}
			else
			{
				TimeStateDebug::LogInputReport(W(), *GRun.Name);
				Note(TEXT("input not checked in the menu the test was started from (the console box holds the focus)"));
			}
			return 0.f;
		});
	}

	static FStep CheckState(const FString& Label, int32 SnapshotAct, TArray<int32> ExpectedActs)
	{
		return Do(Label, [Label, SnapshotAct, ExpectedActs]()
		{
			TArray<int32> Acts;
			for (const FAreaSnapshot& S : Run()->GetSnapshots()) { Acts.Add(S.ActIndex); }
			const FAreaSnapshot* Snap = FindSnapshot(SnapshotAct);
			const FAreaSnapshot* Saved = GRun.SavedSnapshots.FindByPredicate([SnapshotAct](const FAreaSnapshot& S) { return S.ActIndex == SnapshotAct; });
			Check(Acts == ExpectedActs, FString::Printf(TEXT("%s: snapshots %s"), *Label, *SnapshotsLine(Run()->GetSnapshots())));
			if (Saved)
			{
				Check(StateIs(*Saved) && Snap && SameSnapshot(*Snap, *Saved), FString::Printf(TEXT("%s: state %s = snapshot %d before the restart (battery %.4f, home %.0f s, banked %d)"),
					*Label, *StateLine(), SnapshotAct, Saved->BatteryFraction, Saved->HomeSecondsElapsed, Saved->BankedEnergy));
			}
			CheckInput(true, Label);
			return 0.f;
		});
	}

	// --- tls.HomeTest local: contacts, grazes, loss in the current map ---

	static void BuildLocal(TArray<FStep>& S)
	{
		S.Add(Setup(TEXT("local")));
		S.Add(Do(TEXT("fresh home state"), []()
		{
			Home()->RestoreState(1.0, 0.0);
			if (UTimeStateSubsystem* T = TS())
			{
				T->ResetCumulativeStoppedSeconds();
			}
			return 0.f;
		}));
		// Dormant entity: a Resume is never a graze.
		S.Add(Do(TEXT("dormant: stop"), []()
		{
			Check(Entity() && Entity()->GetState() == EEntityState::Dormant, FString::Printf(TEXT("entity %s dormant"), *GetNameSafe(Entity())));
			GRun.SavedJumps = GRun.Listener->Jumps;
			StopTime();
			return 1.f;
		}));
		S.Add(Do(TEXT("dormant: resume"), []()
		{
			ResumeTime();
			Check(GRun.Listener->Jumps == GRun.SavedJumps && !Entity()->WasLastExitGraze(), FString::Printf(TEXT("dormant entity: Resume, no jump (%d events)"), GRun.Listener->Jumps - GRun.SavedJumps));
			return 0.3f;
		}));
		AddContact(S, TEXT("contact Rewind"), EEntityContactResponse::Rewind, true);
		AddContact(S, TEXT("contact Jump"), EEntityContactResponse::Jump, true);
		AddContact(S, TEXT("contact RewindAndJump"), EEntityContactResponse::RewindAndJump, true);
		AddGraze(S, 300.f, 400.f, true);
		AddGraze(S, 500.f, 400.f, false);
		AddGraze(S, 300.f, 0.f, false);
		AddLoss(S, TEXT("loss"));
	}

	// --- tls.HomeTest chain: from the main menu through the run, the save, the loss screen and the menu entries ---

	static void BuildChain(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("in the main menu, no transition"), []()
		{
			UWorld* World = W();
			const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			const UStdFlowSubsystem* F = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			return World && Cast<AStdMenuPlayerController>(World->GetFirstPlayerController()) && F && !F->IsTransitioning();
		}, 30.f));
		S.Add(Setup(TEXT("chain")));
		S.Add(Do(TEXT("clear the save"), []() { Run()->ClearSave(); return 0.3f; }));
		S.Add(CheckMenu(TEXT("no saved run"), false, false));

		AddMenuClick(S, TEXT("New game"), TEXT("Button_NewGame"), 1);
		S.Add(Do(TEXT("area 1 entered"), []()
		{
			const FAreaSnapshot* Snap = FindSnapshot(1);
			Check(Run()->GetSnapshots().Num() == 1 && Snap && Snap->BatteryFraction == 1.0 && Snap->HomeSecondsElapsed == 0.0 && Snap->BankedEnergy == 0 && Run()->DidLastSaveSucceed(),
				FString::Printf(TEXT("after New game: snapshots %s, save written %d"), *SnapshotsLine(Run()->GetSnapshots()), Run()->DidLastSaveSucceed() ? 1 : 0));
			CheckInput(true, TEXT("New game"));
			return 0.f;
		}));
		AddContact(S, TEXT("act 1, map response"), TOptional<EEntityContactResponse>(), false);
		S.Add(Do(TEXT("set battery 0.6, home 12345 s, banked 2, travel"), []()
		{
			Home()->RestoreState(0.6, 12345.0);
			Energy()->RestoreBankedEnergy(2);
			TravelStarted();
			return Run()->TravelToNextArea() ? 0.f : -1.f;
		}));
		S.Add(WaitArrival(TEXT("travel to area 2"), 2));
		S.Add(Do(TEXT("area 2: state carried"), []()
		{
			const FAreaSnapshot* Snap = FindSnapshot(2);
			Check(FMath::IsNearlyEqual(Home()->GetBatteryFraction(), 0.6, 1.e-12) && Home()->GetHomeSecondsElapsed() == 12345.0 && Energy()->GetBankedEnergy() == 2,
				FString::Printf(TEXT("across the travel: %s (set 0.6, 12345 s, 2)"), *StateLine()));
			Check(Snap && Snap->BatteryFraction == Home()->GetBatteryFraction() && Snap->HomeSecondsElapsed == 12345.0 && Snap->BankedEnergy == 2 && Run()->GetSnapshots().Num() == 2,
				FString::Printf(TEXT("snapshot 2 at entry: %s"), *SnapshotsLine(Run()->GetSnapshots())));
			CheckInput(true, TEXT("travel to area 2"));
			return 0.f;
		}));
		AddContact(S, TEXT("act 2, map response"), TOptional<EEntityContactResponse>(), false);
		S.Add(Do(TEXT("travel to area 3"), []() { TravelStarted(); return Run()->TravelToNextArea() ? 0.f : -1.f; }));
		S.Add(WaitArrival(TEXT("travel to area 3"), 3));
		S.Add(Do(TEXT("area 3: snapshot"), []()
		{
			const FAreaSnapshot* Snap = FindSnapshot(3);
			Check(Snap && StateIs(*Snap) && Run()->GetSnapshots().Num() == 3, FString::Printf(TEXT("snapshot 3 at entry: %s"), *SnapshotsLine(Run()->GetSnapshots())));
			return 0.f;
		}));
		AddContact(S, TEXT("act 3, map response"), TOptional<EEntityContactResponse>(), false);
		S.Add(Do(TEXT("travel to area 4"), []() { TravelStarted(); return Run()->TravelToNextArea() ? 0.f : -1.f; }));
		S.Add(WaitArrival(TEXT("travel to area 4"), 4));
		AddContact(S, TEXT("act 4, map response"), TOptional<EEntityContactResponse>(), false);
		S.Add(Do(TEXT("restart from act 3 (tls)"), []()
		{
			GRun.SavedSnapshots = Run()->GetSnapshots();
			TravelStarted();
			return Run()->RestartFromAct(3) ? 0.f : -1.f;
		}));
		S.Add(WaitArrival(TEXT("restart act 3"), 3));
		S.Add(CheckState(TEXT("restart act 3"), 3, { 1, 2, 3 }));

		// Lost in area 3, restart from area 2 on the loss screen.
		AddLoss(S, TEXT("loss in area 3"));
		AddChooseEntry(S, TEXT("loss screen: Area 2"), 1, 2);
		S.Add(CheckState(TEXT("restart from the loss screen, area 2"), 2, { 1, 2 }));
		S.Add(Do(TEXT("travel to area 3 again"), []() { TravelStarted(); return Run()->TravelToNextArea() ? 0.f : -1.f; }));
		S.Add(WaitArrival(TEXT("area 3 again"), 3));
		S.Add(Do(TEXT("area 3 again: new snapshot 3"), []()
		{
			const FAreaSnapshot* Snap = FindSnapshot(3);
			Check(Snap && StateIs(*Snap) && Run()->GetSnapshots().Num() == 3, FString::Printf(TEXT("a new snapshot 3: %s"), *SnapshotsLine(Run()->GetSnapshots())));
			return 0.f;
		}));
		// Lost again, retry area 3.
		AddLoss(S, TEXT("loss in area 3 again"));
		AddChooseEntry(S, TEXT("loss screen: Area 3"), 2, 3);
		S.Add(CheckState(TEXT("retry area 3 from the loss screen"), 3, { 1, 2, 3 }));

		// Main menu from the pause menu, then Continue.
		AddPauseToMenu(S, TEXT("to the menu"));
		S.Add(CheckMenu(TEXT("saved run"), true));
		S.Add(Do(TEXT("remember the snapshots"), []() { GRun.SavedSnapshots = Run()->GetSnapshots(); return 0.f; }));
		AddMenuClick(S, TEXT("Continue"), TEXT("Button_Continue"), 3);
		S.Add(CheckState(TEXT("Continue"), 3, { 1, 2, 3 }));

		// Main menu again, Select act, Area 2.
		AddPauseToMenu(S, TEXT("to the menu again"));
		AddMenuClick(S, TEXT("Select act"), TEXT("Button_SelectAct"), -1);
		S.Add(Do(TEXT("select act list"), []()
		{
			const FString List = TimeStateDebug::DescribeChoiceList(W());
			FString Expected;
			for (const FAreaSnapshot& Snap : GRun.SavedSnapshots)
			{
				Expected += FString::Printf(TEXT("Area %d — %d%% | "), Snap.ActIndex, FMath::RoundToInt(Snap.BatteryFraction * 100.0));
			}
			Expected += TEXT("<back>");
			Check(List == Expected, FString::Printf(TEXT("Select act lists [%s] (expected [%s])"), *List, *Expected));
			CheckInput(false, TEXT("Select act"));
			return 0.2f;
		}));
		S.Add(Do(TEXT("Select act: back and again"), []()
		{
			const bool bBack = TimeStateDebug::ClickChoiceEntry(W(), INDEX_NONE);
			const FString Menu = TimeStateDebug::DescribeMainMenu(W());
			Check(bBack && TimeStateDebug::DescribeChoiceList(W()).IsEmpty(), FString::Printf(TEXT("Back closes Select act (menu [%s])"), *Menu));
			const bool bAgain = TimeStateDebug::ClickMainMenuButton(W(), TEXT("Button_SelectAct"));
			Check(bAgain, TEXT("Select act opened again"));
			return 0.2f;
		}));
		AddChooseEntry(S, TEXT("Select act: Area 2"), 1, 2);
		S.Add(CheckState(TEXT("act chosen in Select act"), 2, { 1, 2 }));

		// Lost in area 2, restart from area 1: a new run.
		AddLoss(S, TEXT("loss in area 2"));
		AddChooseEntry(S, TEXT("loss screen: Area 1"), 0, 1);
		S.Add(Do(TEXT("area 1 from the loss screen"), []()
		{
			const FAreaSnapshot* Snap = FindSnapshot(1);
			Check(Home()->GetBatteryFraction() == 1.0 && Home()->GetHomeSecondsElapsed() == 0.0 && Energy()->GetBankedEnergy() == 0 && !Home()->IsRunLost(),
				FString::Printf(TEXT("area 1 is a new run: %s"), *StateLine()));
			Check(Run()->GetSnapshots().Num() == 1 && Snap && Snap->BatteryFraction == 1.0, FString::Printf(TEXT("snapshots %s"), *SnapshotsLine(Run()->GetSnapshots())));
			CheckInput(true, TEXT("restart from the loss screen, area 1"));
			return 0.f;
		}));

		// Main menu, New game with a saved run: the save is cleared, area 1.
		S.Add(Do(TEXT("banked 3 before the menu"), []() { Energy()->RestoreBankedEnergy(3); return 0.f; }));
		AddPauseToMenu(S, TEXT("to the menu for New game"));
		AddMenuClick(S, TEXT("New game with a saved run"), TEXT("Button_NewGame"), 1);
		S.Add(Do(TEXT("New game state"), []()
		{
			Check(Run()->GetSnapshots().Num() == 1 && Energy()->GetBankedEnergy() == 0 && Home()->GetBatteryFraction() == 1.0,
				FString::Printf(TEXT("New game: %s, snapshots %s"), *StateLine(), *SnapshotsLine(Run()->GetSnapshots())));
			CheckInput(true, TEXT("New game with a saved run"));
			return 0.f;
		}));
	}

	static bool StartRun(const FString& Name, TArray<FStep>&& Steps)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: %s is already running."), *Name, *GRun.Name);
			return false;
		}
		GRun = FRun();
		GRun.Name = Name;
		GRun.Steps = MoveTemp(Steps);
		GRun.bRunning = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started: %d steps."), *Name, GRun.Steps.Num());
		return true;
	}

	static void LogSnapshots(UWorld* World, const TCHAR* Prefix)
	{
		const URunSubsystem* R = URunSubsystem::Get(World);
		const UHomeStateSubsystem* H = UHomeStateSubsystem::Get(World);
		if (!R || !H)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s no game world."), Prefix);
			return;
		}
		UE_LOG(LogTheLastShift, Display, TEXT("%s %d snapshots, saved run %d, slot '%s' exists %d; now: battery %.4f, home %.0f s, banked %d, lost %d"),
			Prefix, R->GetSnapshots().Num(), R->HasSavedRun() ? 1 : 0, URunSubsystem::SaveSlotName(),
			UGameplayStatics::DoesSaveGameExist(URunSubsystem::SaveSlotName(), 0) ? 1 : 0, H->GetBatteryFraction(), H->GetHomeSecondsElapsed(),
			UEnergySubsystem::Get(World) ? UEnergySubsystem::Get(World)->GetBankedEnergy() : -1, H->IsRunLost() ? 1 : 0);
		for (const FAreaSnapshot& S : R->GetSnapshots())
		{
			UE_LOG(LogTheLastShift, Display, TEXT("%s   act %d: battery %.6f (%d%%), home %.0f s, banked %d"), Prefix, S.ActIndex, S.BatteryFraction,
				FMath::RoundToInt(S.BatteryFraction * 100.0), S.HomeSecondsElapsed, S.BankedEnergy);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsHomeSetCommand(
	TEXT("tls.Home.Set"),
	TEXT("tls.Home.Set <battery 0..1> [homeSeconds]: sets the home battery (and home time; kept when omitted) and clears a loss."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(TimeStateDebug::ResolveGameWorld(InWorld));
		if (!Home || Args.Num() < 1)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Home.Set: needs a game world and a battery value."));
			return;
		}
		const double Battery = FCString::Atod(*Args[0]);
		const double HomeSeconds = Args.Num() > 1 ? FCString::Atod(*Args[1]) : Home->GetHomeSecondsElapsed();
		Home->RestoreState(Battery, HomeSeconds);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Home.Set: battery %.4f, home %.0f s."), Home->GetBatteryFraction(), Home->GetHomeSecondsElapsed());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsHomeJumpCommand(
	TEXT("tls.Home.Jump"),
	TEXT("tls.Home.Jump <contact|graze>: applies a jump with the current act's tuning, as a contact (full) or a graze (GrazeJumpFraction)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(World);
		if (!Home)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Home.Jump: no game world."));
			return;
		}
		const bool bGraze = Args.Num() > 0 && Args[0].Equals(TEXT("graze"), ESearchCase::IgnoreCase);
		const double Battery = Home->GetBatteryFraction();
		const double HomeSeconds = Home->GetHomeSecondsElapsed();
		const int32 Act = URunSubsystem::GetActIndexOf(World);
		const bool bApplied = Home->ApplyJump(bGraze ? EHomeJumpCause::Graze : EHomeJumpCause::Contact, Act, 0.f);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Home.Jump: %s in act %d %s: home +%.0f s, battery %.6f -> %.6f (-%.6f)."), bGraze ? TEXT("graze") : TEXT("contact"), Act,
			bApplied ? TEXT("applied") : TEXT("ignored"), Home->GetHomeSecondsElapsed() - HomeSeconds, Battery, Home->GetBatteryFraction(), Battery - Home->GetBatteryFraction());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsRunSnapshotsCommand(
	TEXT("tls.Run.Snapshots"),
	TEXT("tls.Run.Snapshots: logs the run snapshots (act, battery, home time, banked energy), whether a saved run exists, and the current home state."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		HomeDebug::LogSnapshots(TimeStateDebug::ResolveGameWorld(InWorld), TEXT("tls.Run.Snapshots:"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsRunRestartFromCommand(
	TEXT("tls.Run.RestartFrom"),
	TEXT("tls.Run.RestartFrom <act>: restarts the run from that act's snapshot (act 1: a new run), as the loss screen and Select act do."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		URunSubsystem* Run = URunSubsystem::Get(TimeStateDebug::ResolveGameWorld(InWorld));
		const int32 Act = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Run.RestartFrom %d: %s."), Act, Run && Run->RestartFromAct(Act) ? TEXT("travelling") : TEXT("refused"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsRunClearSaveCommand(
	TEXT("tls.Run.ClearSave"),
	TEXT("tls.Run.ClearSave: deletes the run snapshots and the save slot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		if (URunSubsystem* Run = URunSubsystem::Get(TimeStateDebug::ResolveGameWorld(InWorld)))
		{
			Run->ClearSave();
			UE_LOG(LogTheLastShift, Display, TEXT("tls.Run.ClearSave: done."));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsHomeTestCommand(
	TEXT("tls.HomeTest"),
	TEXT("tls.HomeTest [local|chain]: local (fresh PIE in an area map or TestLidar): dormant Resume, contacts with Rewind / Jump / RewindAndJump against the act's ActTuning and a powered device, grazes at 300 cm, at 500 cm and with GrazeRadius 0, then a loss (OnRunLost once, two more jumps ignored, loss screen list, no time stop, no pause, UI input). chain (fresh PIE in MainMenu; clears the save): New game, state across travels, snapshots, a contact in each act with the map's response, losses and restarts from the loss screen (area 2, area 3, area 1), the pause menu's Main menu, Continue, Select act, New game, with the input checked after each."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		const FString Part = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("local"));
		TArray<HomeDebug::FStep> Steps;
		if (Part == TEXT("chain"))
		{
			HomeDebug::BuildChain(Steps);
		}
		else
		{
			HomeDebug::BuildLocal(Steps);
		}
		HomeDebug::StartRun(FString::Printf(TEXT("HomeTest(%s)"), *Part), MoveTemp(Steps));
	}));

#endif // !UE_BUILD_SHIPPING

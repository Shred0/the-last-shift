// SPDX-License-Identifier: MIT

// Debug console commands for residues, energy and powered devices (Brief 4): tls.Energy.Give, tls.KeyHold,
// tls.ResidueTest, tls.DeviceTest, tls.MapChangeTest. Development builds only.

#include "Debug/EnergyTestListener.h"
#include "Debug/TlsDebugCommon.h"
#include "Containers/Ticker.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/PowerTerminal.h"
#include "Entity/Entity.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "LidarScanComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Residue/Residue.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/StrongObjectPtr.h"

// --- UTlsEnergyTestListener (reflected, so it exists in every configuration) ---

void UTlsEnergyTestListener::BindEnergy(UEnergySubsystem* Energy)
{
	if (Energy)
	{
		BoundEnergy = Energy;
		Energy->OnEnergyChanged.AddUniqueDynamic(this, &UTlsEnergyTestListener::HandleEnergyChanged);
	}
}

void UTlsEnergyTestListener::BindTerminal(APowerTerminal* Terminal)
{
	if (Terminal)
	{
		BoundTerminals.Add(Terminal);
		Terminal->OnActivated.AddUniqueDynamic(this, &UTlsEnergyTestListener::HandleActivated);
		Terminal->OnInsufficientEnergy.AddUniqueDynamic(this, &UTlsEnergyTestListener::HandleInsufficient);
	}
}

void UTlsEnergyTestListener::BindDoor(ADoorDevice* Door)
{
	if (Door)
	{
		BoundDoor = Door;
		Door->OnUnpowered.AddUniqueDynamic(this, &UTlsEnergyTestListener::HandleDoorUnpowered);
		Door->OnOpenChanged.AddUniqueDynamic(this, &UTlsEnergyTestListener::HandleDoorOpenChanged);
	}
}

void UTlsEnergyTestListener::UnbindAll()
{
	if (UEnergySubsystem* Energy = BoundEnergy.Get())
	{
		Energy->OnEnergyChanged.RemoveDynamic(this, &UTlsEnergyTestListener::HandleEnergyChanged);
	}
	for (const TWeakObjectPtr<APowerTerminal>& Weak : BoundTerminals)
	{
		if (APowerTerminal* Terminal = Weak.Get())
		{
			Terminal->OnActivated.RemoveDynamic(this, &UTlsEnergyTestListener::HandleActivated);
			Terminal->OnInsufficientEnergy.RemoveDynamic(this, &UTlsEnergyTestListener::HandleInsufficient);
		}
	}
	if (ADoorDevice* Door = BoundDoor.Get())
	{
		Door->OnUnpowered.RemoveDynamic(this, &UTlsEnergyTestListener::HandleDoorUnpowered);
		Door->OnOpenChanged.RemoveDynamic(this, &UTlsEnergyTestListener::HandleDoorOpenChanged);
	}
	BoundEnergy.Reset();
	BoundTerminals.Reset();
	BoundDoor.Reset();
}

double UTlsEnergyTestListener::GetWorldTime() const
{
	const ADoorDevice* Door = BoundDoor.Get();
	const UWorld* World = Door ? Door->GetWorld() : nullptr;
	return World ? World->GetTimeSeconds() : -1.0;
}

void UTlsEnergyTestListener::HandleEnergyChanged(int32 BankedEnergy, int32 SessionEnergy)
{
	++EnergyEvents;
	LastBanked = BankedEnergy;
	LastSession = SessionEnergy;
	UE_LOG(LogTheLastShift, Log, TEXT("EnergyTest   listener %s (outer %s): OnEnergyChanged banked=%d session=%d (event %d)"),
		*GetName(), *GetNameSafe(GetOuter()), BankedEnergy, SessionEnergy, EnergyEvents);
}

void UTlsEnergyTestListener::HandleActivated(int32 Cost)
{
	++Activations;
	LastActivationTime = GetWorldTime();
}

void UTlsEnergyTestListener::HandleInsufficient(int32 Cost, int32 BankedEnergy, bool bTimeStopped)
{
	++Refusals;
	bLastRefusalStopped = bTimeStopped;
	UE_LOG(LogTheLastShift, Log, TEXT("EnergyTest   listener: OnInsufficientEnergy cost=%d banked=%d timeStopped=%d"), Cost, BankedEnergy, bTimeStopped ? 1 : 0);
}

void UTlsEnergyTestListener::HandleDoorUnpowered()
{
	DoorUnpoweredTime = GetWorldTime();
}

void UTlsEnergyTestListener::HandleDoorOpenChanged(bool bOpen)
{
	if (!bOpen)
	{
		DoorClosedTime = GetWorldTime();
	}
}

#if !UE_BUILD_SHIPPING

namespace EnergyDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner: a list of steps run in order on the core ticker (survives map changes) ---

	struct FStep
	{
		FString Label;
		/** Runs the step; returns seconds to wait before the next one (< 0 aborts the run). */
		TFunction<float()> Do;
		/** If set, the step waits until this returns true (or Timeout real seconds pass) before running Do. */
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
		FTSTicker::FDelegateHandle Ticker;
		TStrongObjectPtr<UTlsEnergyTestListener> Listener;
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
			GRun.Listener->UnbindAll();
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

	static bool StartRun(const FString& Name, TArray<FStep>&& Steps)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("%s: %s is already running."), *Name, *GRun.Name);
			return false;
		}
		GRun.Name = Name;
		GRun.Steps = MoveTemp(Steps);
		GRun.Next = 0;
		GRun.Passed = 0;
		GRun.Failed = 0;
		GRun.WaitUntilReal = 0.0;
		GRun.ConditionStartReal = -1.0;
		GRun.bRunning = true;
		GRun.Listener.Reset(NewObject<UTlsEnergyTestListener>());
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started: %d steps."), *Name, GRun.Steps.Num());
		return true;
	}

	static FStep Do(const FString& Label, TFunction<float()> Fn)
	{
		FStep Step;
		Step.Label = Label;
		Step.Do = MoveTemp(Fn);
		return Step;
	}

	static FStep Wait(float Seconds)
	{
		return Do(FString::Printf(TEXT("wait %.2f s"), Seconds), [Seconds]() { return Seconds; });
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

	static APlayerController* PC()
	{
		UWorld* World = W();
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	static APawn* Pawn()
	{
		const APlayerController* Controller = PC();
		return Controller ? Controller->GetPawn() : nullptr;
	}

	static UTimeStateSubsystem* TS()
	{
		UWorld* World = W();
		return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	}

	static UScanDirectorComponent* Director()
	{
		UWorld* World = W();
		return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
	}

	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }

	static int32 Banked() { const UEnergySubsystem* E = Energy(); return E ? E->GetBankedEnergy() : -1; }
	static int32 Session() { const UEnergySubsystem* E = Energy(); return E ? E->GetSessionEnergy() : -1; }

	template <class T>
	static TArray<T*> Actors()
	{
		TArray<T*> Out;
		if (UWorld* World = W())
		{
			for (TActorIterator<T> It(World); It; ++It)
			{
				Out.Add(*It);
			}
		}
		Out.Sort([](const T& A, const T& B) { return A.GetName() < B.GetName(); });
		return Out;
	}

	static TArray<AResidue*> Residues(bool bRegenerating)
	{
		TArray<AResidue*> Out = Actors<AResidue>();
		Out.RemoveAll([bRegenerating](const AResidue* R) { return R->bRegenerates != bRegenerating; });
		return Out;
	}

	static AResidue* NormalResidue(int32 Index)
	{
		TArray<AResidue*> List = Residues(false);
		return List.IsValidIndex(Index) ? List[Index] : nullptr;
	}

	static AResidue* RegenResidue()
	{
		TArray<AResidue*> List = Residues(true);
		return List.Num() > 0 ? List[0] : nullptr;
	}

	template <class TDevice>
	static APowerTerminal* TerminalFor()
	{
		for (APowerTerminal* Terminal : Actors<APowerTerminal>())
		{
			for (const APoweredDevice* Target : Terminal->Targets)
			{
				if (Cast<TDevice>(Target))
				{
					return Terminal;
				}
			}
		}
		return nullptr;
	}

	static ADoorDevice* Door()
	{
		const APowerTerminal* Terminal = TerminalFor<ADoorDevice>();
		if (Terminal)
		{
			for (APoweredDevice* Target : Terminal->Targets)
			{
				if (ADoorDevice* D = Cast<ADoorDevice>(Target))
				{
					return D;
				}
			}
		}
		TArray<ADoorDevice*> All = Actors<ADoorDevice>();
		return All.Num() > 0 ? All[0] : nullptr;
	}

	static ALightGroupDevice* LightGroup()
	{
		TArray<ALightGroupDevice*> All = Actors<ALightGroupDevice>();
		return All.Num() > 0 ? All[0] : nullptr;
	}

	static AEntity* Entity()
	{
		TArray<AEntity*> All = Actors<AEntity>();
		return All.Num() > 0 ? All[0] : nullptr;
	}

	static double WorldNow()
	{
		const UWorld* World = W();
		return World ? World->GetTimeSeconds() : 0.0;
	}

	// --- Actions ---

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

	/** Next to a residue, within reach (offset towards -X). */
	static void TeleportTo(const AResidue* Residue)
	{
		if (Residue)
		{
			Teleport(Residue->GetActorLocation() - FVector(70.f, 0.f, 0.f));
		}
	}

	/** In front of a terminal (along its +X), within reach. */
	static void TeleportTo(const APowerTerminal* Terminal)
	{
		if (Terminal)
		{
			Teleport(Terminal->GetActorLocation() + Terminal->GetActorForwardVector() * 90.f);
		}
	}

	static void Key(EInputEvent Event)
	{
		if (APlayerController* Controller = PC())
		{
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, Event, Event == IE_Pressed ? 1.f : 0.f));
		}
	}

	static void PressInteract() { Key(IE_Pressed); }
	static void ReleaseInteract() { Key(IE_Released); }

	/** Stops time and restarts the cumulative stopped count, so the entity stays dormant through the test's short Stops. */
	static void StopTime()
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

	static bool IsStopped()
	{
		const UTimeStateSubsystem* T = TS();
		return T && T->IsStopped();
	}

	static FString ResidueName(const AResidue* R) { return R ? R->GetName() : TEXT("<none>"); }

	/** Steps: go to Residue, hold Interact HoldSeconds, release. */
	static void AddHold(TArray<FStep>& Steps, TFunction<AResidue*()> Which, float HoldSeconds)
	{
		Steps.Add(Do(TEXT("go to residue and press Interact"), [Which]()
		{
			AResidue* R = Which();
			TeleportTo(R);
			PressInteract();
			Note(FString::Printf(TEXT("holding Interact at %s (state %s)"), *ResidueName(R), R ? LexToString(R->GetState()) : TEXT("-")));
			return 0.f;
		}));
		Steps.Add(Wait(HoldSeconds));
		Steps.Add(Do(TEXT("release Interact"), []() { ReleaseInteract(); return 0.15f; }));
	}

	static void Drain()
	{
		if (UEnergySubsystem* E = Energy(); E && E->GetBankedEnergy() > 0)
		{
			Note(FString::Printf(TEXT("draining %d banked energy (test precondition)"), E->GetBankedEnergy()));
			E->Spend(E->GetBankedEnergy());
		}
	}

	// --- Walker: drives the pawn along a navigation path with movement input (crossing test) ---

	struct FWalker
	{
		bool bActive = false;
		bool bArrived = false;
		bool bStuck = false;
		TArray<FVector> Path;
		int32 Index = 1;
		float PathLength = 0.f;
		TWeakObjectPtr<ADoorDevice> Door;
		float StartSide = 0.f;
		double StartTime = 0.0;
		double CrossTime = -1.0;
		double NearDoorTime = -1.0;
		bool bCrossedWhileStopped = false;
		bool bDoorOpenAtCross = false;
		FVector LastProgressLocation = FVector::ZeroVector;
		double LastProgressTime = 0.0;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FWalker GWalker;

	static float DoorLocalX(const ADoorDevice* D, const FVector& Location)
	{
		return D ? static_cast<float>(D->GetActorTransform().InverseTransformPosition(Location).X) : 0.f;
	}

	static void StopWalker()
	{
		GWalker.bActive = false;
		if (GWalker.Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GWalker.Ticker);
			GWalker.Ticker.Reset();
		}
	}

	static bool TickWalker(float)
	{
		APawn* P = Pawn();
		if (!GWalker.bActive || !P)
		{
			return false;
		}
		const double Now = WorldNow();
		const FVector Location = P->GetActorLocation();

		if (ADoorDevice* D = GWalker.Door.Get())
		{
			const float LocalX = DoorLocalX(D, Location);
			if (GWalker.NearDoorTime < 0.0 && FMath::Abs(LocalX) < 100.f)
			{
				GWalker.NearDoorTime = Now;
			}
			if (GWalker.CrossTime < 0.0 && FMath::Sign(LocalX) != FMath::Sign(GWalker.StartSide) && FMath::Abs(LocalX) > 40.f)
			{
				GWalker.CrossTime = Now;
				GWalker.bCrossedWhileStopped = IsStopped();
				GWalker.bDoorOpenAtCross = D->IsOpen();
			}
		}

		if (Now - GWalker.LastProgressTime > 1.5)
		{
			if (FVector::Dist2D(Location, GWalker.LastProgressLocation) < 10.f)
			{
				GWalker.bStuck = true;
				StopWalker();
				return false;
			}
			GWalker.LastProgressLocation = Location;
			GWalker.LastProgressTime = Now;
		}

		while (GWalker.Path.IsValidIndex(GWalker.Index) && FVector::Dist2D(Location, GWalker.Path[GWalker.Index]) < 40.f)
		{
			++GWalker.Index;
		}
		if (!GWalker.Path.IsValidIndex(GWalker.Index))
		{
			GWalker.bArrived = true;
			StopWalker();
			return false;
		}
		const FVector Direction = (GWalker.Path[GWalker.Index] - Location).GetSafeNormal2D();
		P->AddMovementInput(Direction, 1.f);
		if (AController* Controller = P->GetController())
		{
			Controller->SetControlRotation(Direction.Rotation());
		}
		return true;
	}

	static bool StartWalker(const FVector& Target, ADoorDevice* D)
	{
		APawn* P = Pawn();
		UWorld* World = W();
		const UNavigationPath* NavPath = (P && World) ? UNavigationSystemV1::FindPathToLocationSynchronously(World, P->GetNavAgentLocation(), Target, P) : nullptr;
		if (!NavPath || !NavPath->IsValid() || NavPath->PathPoints.Num() < 2)
		{
			return false;
		}
		StopWalker();
		GWalker = FWalker();
		GWalker.Path = NavPath->PathPoints;
		GWalker.PathLength = static_cast<float>(NavPath->GetPathLength());
		GWalker.Door = D;
		GWalker.StartSide = DoorLocalX(D, P->GetActorLocation());
		GWalker.StartTime = WorldNow();
		GWalker.LastProgressLocation = P->GetActorLocation();
		GWalker.LastProgressTime = GWalker.StartTime;
		GWalker.bActive = true;
		GWalker.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickWalker));
		return true;
	}

	/** 2.5 m past the door, on the side opposite to Terminal. */
	static FVector BeyondDoor(const ADoorDevice* D, const APowerTerminal* Terminal)
	{
		const float Side = DoorLocalX(D, Terminal->GetActorLocation());
		return D->GetActorTransform().TransformPosition(FVector(-FMath::Sign(Side) * 250.f, 0.f, 0.f));
	}

	static FString DeviceLine(const APoweredDevice* Device)
	{
		if (!Device)
		{
			return TEXT("<none>");
		}
		FString Extra;
		if (const ADoorDevice* D = Cast<ADoorDevice>(Device))
		{
			Extra = FString::Printf(TEXT(" open=%d pending=%d"), D->IsOpen() ? 1 : 0, D->IsClosePending() ? 1 : 0);
		}
		else if (const ALightGroupDevice* L = Cast<ALightGroupDevice>(Device))
		{
			Extra = FString::Printf(TEXT(" lit=%d/%d"), L->GetLitCount(), L->Lights.Num());
		}
		return FString::Printf(TEXT("%s powered=%d remaining=%.3f%s"), *Device->GetName(), Device->IsPowered() ? 1 : 0, Device->GetRemainingSeconds(), *Extra);
	}

	// ======================================================================================================
	// tls.ResidueTest: criteria 1 (scan points), 2 (hold), 3 (banking and every lost exit), 8 (dead end)
	// ======================================================================================================

	struct FResidueTestState
	{
		int32 Banked0 = 0;
		int32 Drained = 0;
	};
	static FResidueTestState GResidue;

	static void AddCollectTwoAndExit(TArray<FStep>& Steps, EStopExitReason Reason)
	{
		const FString R = LexToString(Reason);
		Steps.Add(Do(FString::Printf(TEXT("stop (%s round)"), *R), []() { StopTime(); return 1.3f; }));
		AddHold(Steps, []() { return NormalResidue(2); }, 2.7f);
		AddHold(Steps, []() { return NormalResidue(3); }, 2.7f);
		Steps.Add(Do(TEXT("exit"), [Reason, R]()
		{
			const AResidue* A = NormalResidue(2);
			const AResidue* B = NormalResidue(3);
			Check(Session() == 2 && A && B && A->GetState() == EResidueState::Collected && B->GetState() == EResidueState::Collected,
				FString::Printf(TEXT("%s round: two collected before the exit (session %d)"), *R, Session()));
			UScanDirectorComponent* D = Director();
			Check(D && D->ForceExit(Reason), FString::Printf(TEXT("ForceExit(%s)"), *R));
			return 0.3f;
		}));
		Steps.Add(Do(TEXT("after exit"), [Reason, R]()
		{
			const AResidue* A = NormalResidue(2);
			const AResidue* B = NormalResidue(3);
			const UTimeStateSubsystem* T = TS();
			Check(T && !T->IsStopped() && T->GetLastExitReason() == Reason, FString::Printf(TEXT("%s: time Normal, last exit %s"), *R,
				T ? LexToString(T->GetLastExitReason()) : TEXT("-")));
			Check(Banked() == GResidue.Banked0 + 2 && Session() == 0, FString::Printf(TEXT("%s: banked unchanged %d (expected %d), session %d"),
				*R, Banked(), GResidue.Banked0 + 2, Session()));
			Check(A && B && A->GetState() == EResidueState::Available && B->GetState() == EResidueState::Available,
				FString::Printf(TEXT("%s: both residues back in place (%s, %s)"), *R, A ? LexToString(A->GetState()) : TEXT("-"),
					B ? LexToString(B->GetState()) : TEXT("-")));
			return 0.3f;
		}));
	}

	static TArray<FStep> BuildResidueTest()
	{
		TArray<FStep> S;

		S.Add(Do(TEXT("setup"), []()
		{
			ResumeTime();
			if (Residues(false).Num() < 4 || !RegenResidue() || !Energy() || !Pawn() || !TerminalFor<ADoorDevice>())
			{
				Check(false, FString::Printf(TEXT("setup: needs 4 residues, 1 regenerating, a door terminal, a pawn (have %d, %d)"),
					Residues(false).Num(), Residues(true).Num()));
				return -1.f;
			}
			for (const AResidue* R : Actors<AResidue>())
			{
				if (R->GetState() != EResidueState::Available)
				{
					Check(false, FString::Printf(TEXT("setup: %s is %s; run on a fresh PIE"), *R->GetName(), LexToString(R->GetState())));
					return -1.f;
				}
			}
			GResidue = FResidueTestState();
			GResidue.Banked0 = Banked();
			GRun.Listener->BindEnergy(Energy());
			Note(FString::Printf(TEXT("banked at start %d; residues %d normal + %d regenerating"), GResidue.Banked0, Residues(false).Num(), Residues(true).Num()));
			return 0.2f;
		}));

		// Criterion 2: no collection in Normal time.
		AddHold(S, []() { return NormalResidue(0); }, 3.0f);
		S.Add(Do(TEXT("check normal hold"), []()
		{
			const AResidue* R = NormalResidue(0);
			Check(R && R->GetState() == EResidueState::Available && Session() == 0 && Banked() == GResidue.Banked0,
				FString::Printf(TEXT("3.0 s hold in Normal time collects nothing (state %s, session %d)"), R ? LexToString(R->GetState()) : TEXT("-"), Session()));
			return 0.f;
		}));

		// Criterion 1 (scan side) and 2 (release at 2.0 s, collect at 2.5 s).
		S.Add(Do(TEXT("stop"), []() { StopTime(); return 1.4f; }));
		S.Add(Do(TEXT("check first scan"), []()
		{
			const UScanDirectorComponent* D = Director();
			const ULidarScanComponent* Lidar = D ? D->GetLidarScan() : nullptr;
			const FLidarInjectionStats Stats = Lidar ? Lidar->GetLastInjectionStats() : FLidarInjectionStats();
			int32 Total = 0;
			for (const AResidue* R : Actors<AResidue>())
			{
				Total += R->GetLastGatheredPoints();
			}
			const AResidue* R0 = NormalResidue(0);
			Check(R0 && R0->GetLastGatheredPoints() == FMath::Clamp(R0->PointCount, 1, 2000),
				FString::Printf(TEXT("first scan gathered %d residue points from %s (all residues %d); injection: sources %d gathered %d outside %d uploaded %d"),
					R0 ? R0->GetLastGatheredPoints() : -1, *ResidueName(R0), Total, Stats.Sources, Stats.Gathered, Stats.OutsideCoverage, Stats.Uploaded));
			return 0.f;
		}));
		AddHold(S, []() { return NormalResidue(0); }, 2.0f);
		S.Add(Do(TEXT("check release at 2.0 s"), []()
		{
			const AResidue* R = NormalResidue(0);
			Check(R && R->GetState() == EResidueState::Available && !R->IsCollecting() && R->GetCollectProgress() == 0.f && Session() == 0,
				FString::Printf(TEXT("release at 2.0 s resets (state %s, collecting %d, session %d)"), R ? LexToString(R->GetState()) : TEXT("-"),
					R && R->IsCollecting() ? 1 : 0, Session()));
			return 0.f;
		}));
		AddHold(S, []() { return NormalResidue(0); }, 2.7f);
		S.Add(Do(TEXT("check collect"), []()
		{
			const AResidue* R = NormalResidue(0);
			Check(R && R->GetState() == EResidueState::Collected && Session() == 1,
				FString::Printf(TEXT("2.7 s hold while Stopped collects (state %s, session %d)"), R ? LexToString(R->GetState()) : TEXT("-"), Session()));
			return 0.f;
		}));
		AddHold(S, []() { return NormalResidue(1); }, 2.7f);

		// Criterion 3: banking on Resume.
		S.Add(Do(TEXT("resume"), []()
		{
			Check(Session() == 2, FString::Printf(TEXT("two collected (session %d)"), Session()));
			ResumeTime();
			return 0.3f;
		}));
		S.Add(Do(TEXT("check banking"), []()
		{
			const AResidue* A = NormalResidue(0);
			const AResidue* B = NormalResidue(1);
			Check(Banked() == GResidue.Banked0 + 2 && Session() == 0, FString::Printf(TEXT("Resume banks +2 (banked %d, session %d)"), Banked(), Session()));
			Check(A && B && A->GetState() == EResidueState::Spent && B->GetState() == EResidueState::Spent, TEXT("banked residues are spent"));
			return 0.f;
		}));

		AddCollectTwoAndExit(S, EStopExitReason::Rewound);
		AddCollectTwoAndExit(S, EStopExitReason::Jumped);
		AddCollectTwoAndExit(S, EStopExitReason::RewoundAndJumped);

		S.Add(Do(TEXT("stop (collectable again)"), []() { StopTime(); return 1.3f; }));
		AddHold(S, []() { return NormalResidue(2); }, 2.7f);
		S.Add(Do(TEXT("check collectable again"), []()
		{
			const AResidue* R = NormalResidue(2);
			Check(R && R->GetState() == EResidueState::Collected, TEXT("a residue lost to RewoundAndJumped is collectable again"));
			return 0.f;
		}));
		AddHold(S, []() { return NormalResidue(3); }, 2.7f);
		// Maps with more than four normal residues (generated areas): collect the rest in the same Stop, so every normal
		// residue is spent before the dead end.
		for (int32 Extra = 4; Extra < Residues(false).Num(); ++Extra)
		{
			AddHold(S, [Extra]() { return NormalResidue(Extra); }, 2.7f);
		}
		S.Add(Do(TEXT("resume: every normal residue spent"), []() { ResumeTime(); return 0.3f; }));

		// Criterion 8: dead end. Collect the regenerating residue, spend everything, it comes back and pays for the door.
		S.Add(Do(TEXT("stop (regenerating)"), []()
		{
			bool bAllSpent = true;
			for (const AResidue* R : Residues(false))
			{
				bAllSpent &= R->GetState() == EResidueState::Spent;
			}
			const int32 Normal = Residues(false).Num();
			Check(bAllSpent && Banked() == GResidue.Banked0 + Normal, FString::Printf(TEXT("every normal residue spent (%d), banked %d"), Normal, Banked()));
			StopTime();
			return 1.3f;
		}));
		AddHold(S, []() { return RegenResidue(); }, 2.7f);
		S.Add(Do(TEXT("resume"), []() { ResumeTime(); return 0.3f; }));
		S.Add(Do(TEXT("spend everything at the door terminal"), []()
		{
			const AResidue* G = RegenResidue();
			Check(G && G->GetState() == EResidueState::Spent, FString::Printf(TEXT("regenerating residue banked and spent while energy %d >= %d"),
				Banked(), G ? G->RegenerateBelowEnergy : -1));
			APowerTerminal* Terminal = TerminalFor<ADoorDevice>();
			TeleportTo(Terminal);
			int32 Guard = 100;
			while (Banked() > 0 && Guard-- > 0)
			{
				if (!Terminal->TryActivate(Pawn()))
				{
					break;
				}
				++GResidue.Drained;
			}
			Note(FString::Printf(TEXT("paid the door %d times"), GResidue.Drained));
			return 0.2f;
		}));
		S.Add(Do(TEXT("check dead end"), []()
		{
			const AResidue* G = RegenResidue();
			Check(Banked() == 0 && Session() == 0, FString::Printf(TEXT("energy 0 (banked %d, session %d)"), Banked(), Session()));
			Check(G && G->GetState() == EResidueState::Available, FString::Printf(TEXT("regenerating residue is back (state %s)"), G ? LexToString(G->GetState()) : TEXT("-")));
			StopTime();
			return 1.3f;
		}));
		AddHold(S, []() { return RegenResidue(); }, 2.7f);
		S.Add(Do(TEXT("resume"), []() { ResumeTime(); return 0.3f; }));
		S.Add(Do(TEXT("pay the door with it"), []()
		{
			Check(Banked() == 1, FString::Printf(TEXT("regenerated residue banked (banked %d)"), Banked()));
			TeleportTo(TerminalFor<ADoorDevice>());
			PressInteract();
			return 0.1f;
		}));
		S.Add(Do(TEXT("check door"), []()
		{
			ReleaseInteract();
			const ADoorDevice* D = Door();
			Check(D && D->IsPowered() && D->IsOpen() && Banked() == 0, FString::Printf(TEXT("Interact at the terminal pays for the door: %s, banked %d"),
				*DeviceLine(D), Banked()));
			const AResidue* G = RegenResidue();
			Note(FString::Printf(TEXT("regenerating residue after paying: %s"), G ? LexToString(G->GetState()) : TEXT("-")));
			return 0.f;
		}));
		return S;
	}

	// ======================================================================================================
	// tls.DeviceTest: criteria 4 (terminal), 5 (timer and doorway), 6 (crossing), 7 (reset on jump, kept on rewind)
	// ======================================================================================================

	struct FDeviceTestState
	{
		double PayTime = 0.0;
		float RemainingBeforeStop = 0.f;
		float RemainingStopped = 0.f;
		float RemainingAfterResume = 0.f;
		float StoppedSeconds = 0.f;
		float DoorLightsRemaining[2] = { 0.f, 0.f };
		EEntityContactResponse SavedResponse = EEntityContactResponse::Rewind;
		FVector WalkStart = FVector::ZeroVector;
	};
	static FDeviceTestState GDevice;

	static float DoorSeconds()
	{
		const ADoorDevice* D = Door();
		return D ? FMath::Clamp(D->PoweredSeconds, 0.f, 600.f) : 0.f;
	}

	static void AddPay(TArray<FStep>& Steps, TFunction<APowerTerminal*()> Which, const FString& Label)
	{
		Steps.Add(Do(FString::Printf(TEXT("press Interact at %s"), *Label), [Which]()
		{
			TeleportTo(Which());
			PressInteract();
			GDevice.PayTime = WorldNow();
			return 0.1f;
		}));
		Steps.Add(Do(TEXT("release Interact"), []()
		{
			ReleaseInteract();
			if (GRun.Listener->LastActivationTime > 0.0 && GRun.Listener->LastActivationTime >= GDevice.PayTime - 0.001)
			{
				GDevice.PayTime = GRun.Listener->LastActivationTime;
			}
			return 0.1f;
		}));
	}

	static void AddTerminalPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("terminal: setup"), []()
		{
			ResumeTime();
			Drain();
			GRun.Listener->BindTerminal(TerminalFor<ADoorDevice>());
			GRun.Listener->BindTerminal(TerminalFor<ALightGroupDevice>());
			GRun.Listener->BindDoor(Door());
			return 0.1f;
		}));
		AddPay(S, []() { return TerminalFor<ADoorDevice>(); }, TEXT("door terminal with 0 energy"));
		S.Add(Do(TEXT("check refused (energy)"), []()
		{
			const UTlsEnergyTestListener* L = GRun.Listener.Get();
			Check(L->Refusals == 1 && !L->bLastRefusalStopped && !Door()->IsPowered() && Banked() == 0,
				FString::Printf(TEXT("0 energy: refused, OnInsufficientEnergy fired (%d, stopped %d), %s, banked %d"), L->Refusals,
					L->bLastRefusalStopped ? 1 : 0, *DeviceLine(Door()), Banked()));
			Energy()->AddBankedEnergy(1);
			StopTime();
			return 0.3f;
		}));
		AddPay(S, []() { return TerminalFor<ADoorDevice>(); }, TEXT("door terminal while Stopped"));
		S.Add(Do(TEXT("check refused (stopped)"), []()
		{
			const UTlsEnergyTestListener* L = GRun.Listener.Get();
			Check(L->Refusals == 2 && L->bLastRefusalStopped && !Door()->IsPowered() && Banked() == 1,
				FString::Printf(TEXT("Stopped: refused, OnInsufficientEnergy fired (%d, stopped %d), %s, banked %d"), L->Refusals,
					L->bLastRefusalStopped ? 1 : 0, *DeviceLine(Door()), Banked()));
			ResumeTime();
			return 0.2f;
		}));
		AddPay(S, []() { return TerminalFor<ADoorDevice>(); }, TEXT("door terminal in Normal time"));
		S.Add(Do(TEXT("check activated"), []()
		{
			const UTlsEnergyTestListener* L = GRun.Listener.Get();
			const ADoorDevice* D = Door();
			Check(L->Activations == 1 && D->IsPowered() && D->IsOpen() && Banked() == 0 && D->GetRemainingSeconds() > DoorSeconds() - 0.5f,
				FString::Printf(TEXT("Normal, 1 energy: spent, door powered and open (%s), banked %d, OnActivated %d"), *DeviceLine(D), Banked(), L->Activations));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("door closes when its time runs out"), []() { return !Door()->IsOpen(); }, DoorSeconds() + 2.f));
	}

	static void AddTimerPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("timer: setup"), []() { ResumeTime(); Energy()->AddBankedEnergy(1); return 0.1f; }));
		AddPay(S, []() { return TerminalFor<ADoorDevice>(); }, TEXT("door terminal"));
		S.Add(Wait(1.8f));
		S.Add(Do(TEXT("stop for 20 s"), []()
		{
			GDevice.RemainingBeforeStop = Door()->GetRemainingSeconds();
			StopTime();
			GDevice.RemainingStopped = Door()->GetRemainingSeconds();
			Note(FString::Printf(TEXT("remaining %.3f s before the Stop, %.3f s right after it"), GDevice.RemainingBeforeStop, GDevice.RemainingStopped));
			return 10.f;
		}));
		S.Add(Do(TEXT("keep the entity dormant"), []() { TS()->ResetCumulativeStoppedSeconds(); return 10.f; }));
		S.Add(Do(TEXT("resume after 20 s"), []()
		{
			const float BeforeResume = Door()->GetRemainingSeconds();
			ResumeTime();
			GDevice.StoppedSeconds = TS()->GetLastStopDuration();
			GDevice.RemainingAfterResume = Door()->GetRemainingSeconds();
			Check(FMath::Abs(GDevice.RemainingBeforeStop - BeforeResume) <= 0.05f && FMath::Abs(GDevice.RemainingBeforeStop - GDevice.RemainingAfterResume) <= 0.05f,
				FString::Printf(TEXT("remaining equal before and after a %.3f s Stop: %.3f s before, %.3f s at the end of the Stop, %.3f s after Resume (diff %.3f s)"),
					GDevice.StoppedSeconds, GDevice.RemainingBeforeStop, BeforeResume, GDevice.RemainingAfterResume,
					GDevice.RemainingAfterResume - GDevice.RemainingBeforeStop));
			Check(Door()->IsPowered() && Door()->IsOpen(), FString::Printf(TEXT("door still open after the Stop: %s"), *DeviceLine(Door())));
			return FMath::Max(GDevice.RemainingAfterResume - 1.0f, 0.f);
		}));
		S.Add(Do(TEXT("step into the doorway"), []()
		{
			Teleport(Door()->GetActorLocation());
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("door's power runs out"), []() { return !Door()->IsPowered(); }, 3.f, []()
		{
			const UTlsEnergyTestListener* L = GRun.Listener.Get();
			const double OpenNormal = (L->DoorUnpoweredTime - GDevice.PayTime) - GDevice.StoppedSeconds;
			Check(FMath::Abs(OpenNormal - DoorSeconds()) <= 0.05,
				FString::Printf(TEXT("powered for %.3f s of Normal time in total (PoweredSeconds %.2f; %.3f s since paying minus %.3f s stopped)"),
					OpenNormal, DoorSeconds(), L->DoorUnpoweredTime - GDevice.PayTime, GDevice.StoppedSeconds));
			return 1.5f;
		}));
		S.Add(Do(TEXT("check doorway hold"), []()
		{
			const ADoorDevice* D = Door();
			Check(!D->IsPowered() && D->IsOpen() && D->IsClosePending() && D->IsDoorwayOccupied(),
				FString::Printf(TEXT("1.5 s after the time ran out, a pawn in the doorway keeps it open: %s occupied=%d"), *DeviceLine(D), D->IsDoorwayOccupied() ? 1 : 0));
			TeleportTo(TerminalFor<ADoorDevice>());
			return 0.3f;
		}));
		S.Add(Do(TEXT("check close"), []()
		{
			Check(!Door()->IsOpen(), FString::Printf(TEXT("doorway clear: door closes (%s)"), *DeviceLine(Door())));
			return 0.f;
		}));
	}

	static void AddCrossingPhase(TArray<FStep>& S)
	{
		// Part A: pay and walk in Normal time.
		S.Add(Do(TEXT("crossing: setup"), []() { ResumeTime(); Energy()->AddBankedEnergy(2); GRun.Listener->DoorClosedTime = -1.0; return 0.1f; }));
		AddPay(S, []() { return TerminalFor<ADoorDevice>(); }, TEXT("door terminal"));
		S.Add(Do(TEXT("walk in Normal time"), []()
		{
			Check(Door()->IsOpen(), FString::Printf(TEXT("paid: %s"), *DeviceLine(Door())));
			const bool bStarted = StartWalker(BeyondDoor(Door(), TerminalFor<ADoorDevice>()), Door());
			Check(bStarted, FString::Printf(TEXT("navigation path from the terminal to 2.5 m past the door: %.0f cm"), GWalker.PathLength));
			return bStarted ? 0.f : -1.f;
		}));
		S.Add(WaitFor(TEXT("walk in Normal time ends (arrived or blocked)"), []() { return !GWalker.bActive; }, 25.f, []()
		{
			StopWalker();
			const UTlsEnergyTestListener* L = GRun.Listener.Get();
			Note(FString::Printf(TEXT("Normal walk: path %.0f cm, door closed at +%.2f s, reached the door at +%.2f s, crossed %s, arrived %d stuck %d"),
				GWalker.PathLength, L->DoorClosedTime - GDevice.PayTime, GWalker.NearDoorTime >= 0.0 ? GWalker.NearDoorTime - GDevice.PayTime : -1.0,
				GWalker.CrossTime >= 0.0 ? *FString::Printf(TEXT("at +%.2f s"), GWalker.CrossTime - GDevice.PayTime) : TEXT("never"),
				GWalker.bArrived ? 1 : 0, GWalker.bStuck ? 1 : 0));
			Check(GWalker.CrossTime < 0.0 && L->DoorClosedTime > 0.0 && (GWalker.NearDoorTime < 0.0 || GWalker.NearDoorTime > L->DoorClosedTime),
				TEXT("walking in Normal time reaches the door after it has closed, and does not get through"));
			return 0.2f;
		}));

		// Part B: pay, stop within 1 s, walk while Stopped.
		AddPay(S, []() { return TerminalFor<ADoorDevice>(); }, TEXT("door terminal again"));
		S.Add(Wait(0.4f));
		S.Add(Do(TEXT("stop and walk while Stopped"), []()
		{
			StopTime();
			Note(FString::Printf(TEXT("stopped %.2f s after paying, door remaining %.3f s"), WorldNow() - GDevice.PayTime, Door()->GetRemainingSeconds()));
			GDevice.RemainingStopped = Door()->GetRemainingSeconds();
			const bool bStarted = StartWalker(BeyondDoor(Door(), TerminalFor<ADoorDevice>()), Door());
			return bStarted ? 0.f : -1.f;
		}));
		S.Add(WaitFor(TEXT("walk while Stopped arrives past the door"), []() { return !GWalker.bActive; }, 25.f, []()
		{
			StopWalker();
			Check(GWalker.bArrived && GWalker.CrossTime > 0.0 && GWalker.bCrossedWhileStopped && GWalker.bDoorOpenAtCross,
				FString::Printf(TEXT("crossed the door while Stopped, door open (crossed at +%.2f s of walking, arrived %d, stuck %d)"),
					GWalker.CrossTime - GWalker.StartTime, GWalker.bArrived ? 1 : 0, GWalker.bStuck ? 1 : 0));
			const float Before = Door()->GetRemainingSeconds();
			ResumeTime();
			const float After = Door()->GetRemainingSeconds();
			Check(FMath::Abs(After - GDevice.RemainingStopped) <= 0.05f,
				FString::Printf(TEXT("the door's timer stood still during the %.2f s Stop: %.3f s at the Stop, %.3f s at Resume, %.3f s after"),
					TS()->GetLastStopDuration(), GDevice.RemainingStopped, Before, After));
			GRun.Listener->DoorClosedTime = -1.0;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("after Resume the door closes behind"), []() { return !Door()->IsOpen(); }, DoorSeconds() + 2.f, []()
		{
			const APawn* P = Pawn();
			const float Side = DoorLocalX(Door(), P->GetActorLocation());
			const float TerminalSide = DoorLocalX(Door(), TerminalFor<ADoorDevice>()->GetActorLocation());
			Check(FMath::Sign(Side) != FMath::Sign(TerminalSide), FString::Printf(TEXT("player on the far side of the closed door (local X %.0f cm)"), Side));
			TeleportTo(TerminalFor<ADoorDevice>());
			return 0.2f;
		}));
	}

	static void AddResetPhase(TArray<FStep>& S)
	{
		auto PayBoth = [](TArray<FStep>& Steps)
		{
			Steps.Add(Do(TEXT("give 2"), []() { ResumeTime(); Energy()->AddBankedEnergy(2); return 0.1f; }));
			AddPay(Steps, []() { return TerminalFor<ADoorDevice>(); }, TEXT("door terminal"));
			AddPay(Steps, []() { return TerminalFor<ALightGroupDevice>(); }, TEXT("light terminal"));
			Steps.Add(Do(TEXT("check both powered"), []()
			{
				Check(Door()->IsPowered() && LightGroup()->IsPowered() && LightGroup()->GetLitCount() == LightGroup()->Lights.Num(),
					FString::Printf(TEXT("paid: %s; %s"), *DeviceLine(Door()), *DeviceLine(LightGroup())));
				return 0.f;
			}));
		};
		auto ContactStop = [](TArray<FStep>& Steps, EEntityContactResponse Response)
		{
			Steps.Add(Do(FString::Printf(TEXT("stop, entity at contact range (%s)"), LexToString(Response)), [Response]()
			{
				AEntity* E = Entity();
				if (!E)
				{
					Check(false, TEXT("no entity"));
					return -1.f;
				}
				StopTime();
				GDevice.DoorLightsRemaining[0] = Door()->GetRemainingSeconds();
				GDevice.DoorLightsRemaining[1] = LightGroup()->GetRemainingSeconds();
				E->ContactResponse = Response;
				const bool bSpawned = E->SpawnNow(E->MinSpawnDistance);
				Note(FString::Printf(TEXT("stopped: door %.3f s, lights %.3f s; entity spawned %d at path distance %.0f cm"),
					GDevice.DoorLightsRemaining[0], GDevice.DoorLightsRemaining[1], bSpawned ? 1 : 0, E->GetPathDistanceToPlayer()));
				Check(Door()->IsPowered() && LightGroup()->IsPowered() && LightGroup()->GetLitCount() == 0,
					FString::Printf(TEXT("while Stopped both stay powered and the lights are dark: %s; %s"), *DeviceLine(Door()), *DeviceLine(LightGroup())));
				return bSpawned ? 0.f : -1.f;
			}));
			Steps.Add(WaitFor(TEXT("contact ends the Stop"), []() { return !IsStopped(); }, 20.f));
		};

		S.Add(Do(TEXT("reset: setup"), []()
		{
			if (!Entity() || !LightGroup() || !TerminalFor<ALightGroupDevice>())
			{
				Check(false, TEXT("reset: needs an entity, a light group and its terminal"));
				return -1.f;
			}
			GDevice.SavedResponse = Entity()->ContactResponse;
			return 0.f;
		}));
		PayBoth(S);
		ContactStop(S, EEntityContactResponse::Jump);
		S.Add(Do(TEXT("check jump"), []()
		{
			Check(TS()->GetLastExitReason() == EStopExitReason::Jumped, FString::Printf(TEXT("exit %s"), LexToString(TS()->GetLastExitReason())));
			Check(!Door()->IsPowered() && !Door()->IsOpen() && !LightGroup()->IsPowered() && LightGroup()->GetLitCount() == 0,
				FString::Printf(TEXT("contact with Jump switches both off: %s; %s"), *DeviceLine(Door()), *DeviceLine(LightGroup())));
			return 0.3f;
		}));
		PayBoth(S);
		ContactStop(S, EEntityContactResponse::Rewind);
		S.Add(Do(TEXT("check rewind"), []()
		{
			const float DoorAfter = Door()->GetRemainingSeconds();
			const float LightsAfter = LightGroup()->GetRemainingSeconds();
			Check(TS()->GetLastExitReason() == EStopExitReason::Rewound, FString::Printf(TEXT("exit %s"), LexToString(TS()->GetLastExitReason())));
			Check(Door()->IsPowered() && Door()->IsOpen() && LightGroup()->IsPowered() && LightGroup()->GetLitCount() == LightGroup()->Lights.Num()
				&& FMath::Abs(DoorAfter - GDevice.DoorLightsRemaining[0]) <= 0.05f && FMath::Abs(LightsAfter - GDevice.DoorLightsRemaining[1]) <= 0.05f,
				FString::Printf(TEXT("contact with Rewind keeps both: door %.3f -> %.3f s, lights %.3f -> %.3f s; %s; %s"),
					GDevice.DoorLightsRemaining[0], DoorAfter, GDevice.DoorLightsRemaining[1], LightsAfter, *DeviceLine(Door()), *DeviceLine(LightGroup())));
			Entity()->ContactResponse = GDevice.SavedResponse;
			return 0.f;
		}));
	}

	static TArray<FStep> BuildDeviceTest(const FString& Phase)
	{
		TArray<FStep> S;
		S.Add(Do(TEXT("setup"), []()
		{
			if (!Door() || !TerminalFor<ADoorDevice>() || !Energy() || !Pawn())
			{
				Check(false, TEXT("setup: needs a door with a terminal and a pawn"));
				return -1.f;
			}
			Note(FString::Printf(TEXT("door %s PoweredSeconds %.2f, terminal %s, light group %s"), *Door()->GetName(), DoorSeconds(),
				*TerminalFor<ADoorDevice>()->GetName(), *GetNameSafe(LightGroup())));
			return 0.f;
		}));
		const bool bAll = Phase.IsEmpty() || Phase == TEXT("all");
		if (bAll || Phase == TEXT("terminal")) { AddTerminalPhase(S); }
		if (bAll || Phase == TEXT("timer")) { if (!bAll) { S.Add(Do(TEXT("bind"), []() { GRun.Listener->BindDoor(Door()); return 0.f; })); } AddTimerPhase(S); }
		if (bAll || Phase == TEXT("crossing")) { if (!bAll) { S.Add(Do(TEXT("bind"), []() { GRun.Listener->BindDoor(Door()); return 0.f; })); } AddCrossingPhase(S); }
		if (bAll || Phase == TEXT("reset")) { AddResetPhase(S); }
		return S;
	}

	// ======================================================================================================
	// tls.MapChangeTest: criterion 10
	// ======================================================================================================

	struct FMapChangeState
	{
		TWeakObjectPtr<UWorld> OldWorld;
		FString MapName;
	};
	static FMapChangeState GMap;

	static AResidue* FirstAvailable()
	{
		for (AResidue* R : Residues(false))
		{
			if (R->GetState() == EResidueState::Available)
			{
				return R;
			}
		}
		return nullptr;
	}

	static TArray<FStep> BuildMapChangeTest(const FString& MapName)
	{
		TArray<FStep> S;
		GMap = FMapChangeState();
		GMap.MapName = MapName;

		S.Add(Do(TEXT("setup: 3 banked"), []()
		{
			ResumeTime();
			if (!Energy() || !FirstAvailable())
			{
				Check(false, TEXT("setup: needs an energy subsystem and an available residue"));
				return -1.f;
			}
			Drain();
			Energy()->AddBankedEnergy(3);
			StopTime();
			return 1.3f;
		}));
		AddHold(S, []() { return FirstAvailable(); }, 2.7f);
		S.Add(Do(TEXT("open the map"), []()
		{
			Check(Banked() == 3 && Session() == 1, FString::Printf(TEXT("before the map change: banked %d, session %d (expected 3 and 1)"), Banked(), Session()));
			GMap.OldWorld = W();
			APlayerController* Controller = PC();
			Note(FString::Printf(TEXT("open %s from world %s"), *GMap.MapName, *GetNameSafe(GMap.OldWorld.Get())));
			Controller->ConsoleCommand(FString::Printf(TEXT("open %s"), *GMap.MapName));
			return 0.5f;
		}));
		S.Add(WaitFor(TEXT("new world up, energy bound to its director"), []()
		{
			UWorld* World = W();
			const UEnergySubsystem* E = Energy();
			return World && World != GMap.OldWorld.Get() && World->HasBegunPlay() && Pawn() && Director() && E && E->HasBoundDirector();
		}, 30.f, []() { return 1.0f; }));
		S.Add(Do(TEXT("check fresh map"), []()
		{
			Check(!GMap.OldWorld.IsValid() || GMap.OldWorld.Get() != W(), FString::Printf(TEXT("world changed to %s"), *GetNameSafe(W())));
			Check(Banked() == 3 && Session() == 0, FString::Printf(TEXT("after the map change: banked %d, session %d (expected 3 and 0)"), Banked(), Session()));
			int32 NotAvailable = 0;
			for (const AResidue* R : Actors<AResidue>())
			{
				NotAvailable += R->GetState() != EResidueState::Available ? 1 : 0;
			}
			int32 Powered = 0;
			for (const APoweredDevice* D : Actors<APoweredDevice>())
			{
				Powered += D->IsPowered() ? 1 : 0;
			}
			const ADoorDevice* D = Door();
			const ALightGroupDevice* L = LightGroup();
			Check(NotAvailable == 0 && Powered == 0 && (!D || !D->IsOpen()) && (!L || L->GetLitCount() == 0) && !IsStopped(),
				FString::Printf(TEXT("residues %d (not available %d), devices %d (powered %d), door open %d, lights lit %d, time %s"),
					Actors<AResidue>().Num(), NotAvailable, Actors<APoweredDevice>().Num(), Powered, D && D->IsOpen() ? 1 : 0, L ? L->GetLitCount() : 0,
					IsStopped() ? TEXT("Stopped") : TEXT("Normal")));
			Check(TS()->GetCumulativeStoppedSeconds() == 0.f, FString::Printf(TEXT("cumulative stopped seconds reset with the map (%.3f)"), TS()->GetCumulativeStoppedSeconds()));

			// A listener that belongs to the new world.
			GRun.Listener.Reset(NewObject<UTlsEnergyTestListener>(W()));
			GRun.Listener->BindEnergy(Energy());
			StopTime();
			return 1.3f;
		}));
		AddHold(S, []() { return FirstAvailable(); }, 2.7f);
		S.Add(Do(TEXT("resume"), []() { ResumeTime(); return 0.3f; }));
		S.Add(Do(TEXT("check banking in the new world"), []()
		{
			const UTlsEnergyTestListener* L = GRun.Listener.Get();
			Check(Banked() == 4 && Session() == 0, FString::Printf(TEXT("collect 1 and Resume: banked %d (expected 4)"), Banked()));
			Check(L->EnergyEvents > 0 && L->LastBanked == 4 && L->GetOuter() == W(),
				FString::Printf(TEXT("OnEnergyChanged reached the listener bound in the new world (outer %s): %d events, last banked %d"),
					*GetNameSafe(L->GetOuter()), L->EnergyEvents, L->LastBanked));
			return 0.f;
		}));
		return S;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsEnergyGiveCommand(
	TEXT("tls.Energy.Give"),
	TEXT("tls.Energy.Give [N=1]: adds N banked energy (negative removes)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UEnergySubsystem* Energy = UEnergySubsystem::Get(World);
		if (!Energy)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Energy.Give: no game world / energy subsystem."));
			return;
		}
		const int32 Amount = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
		Energy->AddBankedEnergy(Amount);
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Energy.Give: %+d, banked %d, session %d."), Amount, Energy->GetBankedEnergy(), Energy->GetSessionEnergy());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsKeyHoldCommand(
	TEXT("tls.KeyHold"),
	TEXT("tls.KeyHold Key [Seconds=1]: simulates holding a key on the local player for Seconds, e.g. 'tls.KeyHold E 2.5'."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const FKey Key(Args.Num() > 0 ? FName(*Args[0]) : NAME_None);
		const float Seconds = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.01f, 60.f) : 1.f;
		if (!PC || !Key.IsValid())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.KeyHold: no game world / player controller, or invalid key."));
			return;
		}
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		UE_LOG(LogTheLastShift, Log, TEXT("tls.KeyHold: %s down at t=%.3f for %.2f s."), *Key.ToString(), World->GetTimeSeconds(), Seconds);
		TWeakObjectPtr<APlayerController> WeakPC = PC;
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakPC, Key]()
		{
			if (APlayerController* Controller = WeakPC.Get())
			{
				Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
				UE_LOG(LogTheLastShift, Log, TEXT("tls.KeyHold: %s up at t=%.3f."), *Key.ToString(), Controller->GetWorld()->GetTimeSeconds());
			}
		}), Seconds, false);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsResidueTestCommand(
	TEXT("tls.ResidueTest"),
	TEXT("tls.ResidueTest: on a fresh PIE, with simulated Interact holds: no collection in Normal time, residue points in the first scan, release at 2.0 s resets, 2.5 s collects; Resume banks; Rewound / Jumped / RewoundAndJumped lose the session and return the residues; then the dead end: every normal residue spent and 0 energy, the regenerating residue returns and pays for the door."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		EnergyDebug::StartRun(TEXT("ResidueTest"), EnergyDebug::BuildResidueTest());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsDeviceTestCommand(
	TEXT("tls.DeviceTest"),
	TEXT("tls.DeviceTest [all|terminal|timer|crossing|reset]: terminal refusals and payment; a 6 s door with a 20 s Stop in the middle and a pawn in the doorway; crossing from the terminal in Normal time and while Stopped; contact with Jump and with Rewind against a powered door and light group."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		const FString Phase = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("all"));
		EnergyDebug::StartRun(FString::Printf(TEXT("DeviceTest(%s)"), *Phase), EnergyDebug::BuildDeviceTest(Phase));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsMapChangeTestCommand(
	TEXT("tls.MapChangeTest"),
	TEXT("tls.MapChangeTest [Map=TestLidar]: with 3 banked and 1 session energy, reloads the map; checks banked 3 / session 0, residues and devices fresh; then collects 1 and resumes: banked 4, seen by an OnEnergyChanged listener bound in the new world."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		const FString Map = Args.Num() > 0 ? Args[0] : FString(TEXT("TestLidar"));
		EnergyDebug::StartRun(TEXT("MapChangeTest"), EnergyDebug::BuildMapChangeTest(Map));
	}));

#endif // !UE_BUILD_SHIPPING

// SPDX-License-Identifier: MIT

// Debug console commands for the run across maps: tls.TravelTest (state across a travel, the player held until the
// next area is ready, black-screen timings) and tls.ExitTest (the exit volume in Normal and in Stopped time).
// They drive a core ticker that survives the map change. Compiled out of Shipping.

#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Components/BoxComponent.h"
#include "Containers/Ticker.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/EnergySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace RunDebug
{
	static UWorld* GameWorld() { return TimeStateDebug::ResolveGameWorld(nullptr); }

	static UStdFlowSubsystem* Flow(UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	}

	static void Report(const FString& Name, bool bOk, const FString& What, int32& Failures)
	{
		Failures += bOk ? 0 : 1;
		UE_LOG(LogTheLastShift, Display, TEXT("%s: %s - %s"), *Name, bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	}

	// --- tls.TravelTest ---

	struct FTravel
	{
		bool bActive = false;
		TWeakObjectPtr<UWorld> OldWorld;
		int32 FromAct = 0;
		int32 Banked = 0;
		double StartReal = 0.0;
		double LastProbeReal = 0.0;
		bool bNewWorldSeen = false;
		bool bReadySeen = false;
		int32 Probes = 0;
		int32 StopsWhileHeld = 0;
		double MaxDisplacement = 0.0;
		FVector HeldAt = FVector::ZeroVector;
		bool bHeldAtSet = false;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FTravel GTravel;

	static void FinishTravel(UWorld* World)
	{
		const FString Name = TEXT("tls.TravelTest");
		int32 Failures = 0;
		URunSubsystem* Run = URunSubsystem::Get(World);
		const UEnergySubsystem* Energy = UEnergySubsystem::Get(World);
		const UTimeStateSubsystem* Time = World->GetSubsystem<UTimeStateSubsystem>();
		const AEntity* Entity = nullptr;
		for (TActorIterator<AEntity> It(World); It; ++It) { Entity = *It; break; }
		const AAreaGenerator* Gen = AAreaGenerator::Find(World);
		const int32 Act = Run ? Run->GetActIndex() : -1;
		const float Hold = FMath::Clamp(UTheLastShiftSettings::Get()->TravelHoldSeconds, 0.f, 30.f);
		const float Covered = Run ? Run->GetLastTravelToCoveredSeconds() : -1.f;
		const float Ready = Run ? Run->GetLastTravelToReadySeconds() : -1.f;
		const float FadeIn = Run ? Run->GetLastTravelToFadeInSeconds() : -1.f;
		Report(Name, true, FString::Printf(TEXT("act %d -> act %d (%s), area ready %d, seed %d, hash %016llx, time to ready %.2f s"),
			GTravel.FromAct, Act, *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Gen && Gen->IsReady() ? 1 : 0, Gen ? Gen->GetSeed() : -1,
			Gen ? static_cast<uint64>(Gen->GetLayoutHash()) : 0ull, Gen ? Gen->GetTimeToReadySeconds() : -1.f), Failures);
		Report(Name, Energy && Energy->GetBankedEnergy() == GTravel.Banked, FString::Printf(TEXT("banked energy %d before, %d after"), GTravel.Banked, Energy ? Energy->GetBankedEnergy() : -1), Failures);
		Report(Name, Energy && Energy->GetSessionEnergy() == 0, FString::Printf(TEXT("session energy %d"), Energy ? Energy->GetSessionEnergy() : -1), Failures);
		Report(Name, Time && Time->GetCumulativeStoppedSeconds() < 0.01f && !Time->IsStopped(), FString::Printf(TEXT("cumulative stopped %.3f s, state %s"),
			Time ? Time->GetCumulativeStoppedSeconds() : -1.f, Time && Time->IsStopped() ? TEXT("Stopped") : TEXT("Normal")), Failures);
		Report(Name, Entity && Entity->GetState() == EEntityState::Dormant, FString::Printf(TEXT("entity %s %s (ContactResponse %s)"), *GetNameSafe(Entity),
			Entity && Entity->GetState() == EEntityState::Dormant ? TEXT("dormant") : TEXT("AWAKE"), Entity ? LexToString(Entity->ContactResponse) : TEXT("-")), Failures);
		Report(Name, GTravel.Probes > 0 && GTravel.StopsWhileHeld == 0 && GTravel.MaxDisplacement < 1.0,
			FString::Printf(TEXT("before the area was ready: %d probes (Q pressed, movement input), time stopped %d times, pawn moved %.1f cm"),
				GTravel.Probes, GTravel.StopsWhileHeld, GTravel.MaxDisplacement), Failures);
		Report(Name, Covered >= 0.f && Ready >= Covered && FadeIn >= Ready && FadeIn - Covered >= Hold - 0.05f,
			FString::Printf(TEXT("black from %.2f s, area ready at %.2f s, fade in at %.2f s after the request: black for %.2f s (TravelHoldSeconds %.1f), fade in %.2f s after ready"),
				Covered, Ready, FadeIn, FadeIn - Covered, Hold, FadeIn - Ready), Failures);
		UE_LOG(LogTheLastShift, Display, TEXT("%s: ===== %s (%d failures) ====="), *Name, Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), Failures);
	}

	static bool TickTravel(float)
	{
		UWorld* World = GameWorld();
		const double Now = FPlatformTime::Seconds();
		if (!World || Now - GTravel.StartReal > 60.0)
		{
			UE_LOG(LogTheLastShift, Display, TEXT("tls.TravelTest: ===== FAIL (no game world or no arrival within 60 s) ====="));
			GTravel.bActive = false;
			return false;
		}
		if (World == GTravel.OldWorld.Get())
		{
			return true;
		}
		const AAreaGenerator* Gen = AAreaGenerator::Find(World);
		APlayerController* PC = World->GetFirstPlayerController();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		GTravel.bNewWorldSeen = true;
		if (Gen && !Gen->IsReady())
		{
			// Held: Q and movement must do nothing.
			const UTimeStateSubsystem* Time = World->GetSubsystem<UTimeStateSubsystem>();
			GTravel.StopsWhileHeld += Time && Time->IsStopped() ? 1 : 0;
			if (Pawn)
			{
				if (!GTravel.bHeldAtSet)
				{
					GTravel.HeldAt = Pawn->GetActorLocation();
					GTravel.bHeldAtSet = true;
				}
				GTravel.MaxDisplacement = FMath::Max(GTravel.MaxDisplacement, FVector::Dist(Pawn->GetActorLocation(), GTravel.HeldAt));
				// Every tick while held.
				{
					GTravel.LastProbeReal = Now;
					TimeStateDebug::PressKey(World, EKeys::Q);
					Pawn->AddMovementInput(Pawn->GetActorForwardVector(), 1.f, true);
					++GTravel.Probes;
				}
			}
			return true;
		}
		if (!Gen || !Gen->IsReady())
		{
			return true;
		}
		const UStdFlowSubsystem* F = Flow(World);
		if (F && F->IsTransitioning() && F->GetState() != EStdFlowState::FadingIn)
		{
			// Ready but still black (TravelHoldSeconds): keep probing a moment is pointless; wait for the fade.
			return true;
		}
		if (F && F->IsTransitioning())
		{
			return true;
		}
		FinishTravel(World);
		GTravel.bActive = false;
		return false;
	}

	// --- tls.ExitTest ---

	struct FExit
	{
		bool bActive = false;
		bool bStopped = false;
		int32 Step = 0;
		double StepReal = 0.0;
		int32 Failures = 0;
		TWeakObjectPtr<UWorld> World;
	};
	static FExit GExit;

	static AAreaExit* FindExit(UWorld* World)
	{
		for (TActorIterator<AAreaExit> It(World); It; ++It) { return *It; }
		return nullptr;
	}

	static bool TickExit(float)
	{
		UWorld* World = GExit.World.Get();
		const FString Name = TEXT("tls.ExitTest");
		const double Now = FPlatformTime::Seconds();
		const double Since = Now - GExit.StepReal;
		AAreaExit* Exit = World ? FindExit(World) : nullptr;
		UStdFlowSubsystem* F = Flow(World);
		UTimeStateSubsystem* Time = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!World || !Exit || !F || !Time || !Pawn)
		{
			if (GExit.Step >= 3)
			{
				// The map changed after the travel started: done.
				UE_LOG(LogTheLastShift, Display, TEXT("%s: ===== %s (%d failures) ====="), *Name, GExit.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), GExit.Failures);
			}
			else
			{
				UE_LOG(LogTheLastShift, Display, TEXT("%s: ===== FAIL (world, exit, flow, time state or pawn missing at step %d) ====="), *Name, GExit.Step);
			}
			GExit.bActive = false;
			return false;
		}
		auto Next = [&]() { ++GExit.Step; GExit.StepReal = Now; };
		const FVector Inside = Exit->GetActorLocation() + FVector(0.f, 0.f, 0.f);
		switch (GExit.Step)
		{
		case 0:
			if (GExit.bStopped)
			{
				TimeStateDebug::PressKey(World, EKeys::Q);
			}
			Next();
			break;
		case 1:
			if (Since < 1.5)
			{
				break;
			}
			Report(Name, Time->IsStopped() == GExit.bStopped, FString::Printf(TEXT("time %s before entering the exit"), Time->IsStopped() ? TEXT("Stopped") : TEXT("Normal")), GExit.Failures);
			Pawn->SetActorLocation(Inside, false, nullptr, ETeleportType::TeleportPhysics);
			Next();
			break;
		case 2:
			// Checked within 0.1 s: a travel starts fading out at once and the world goes with the map.
			if (Since < (GExit.bStopped ? 2.0 : 0.1))
			{
				break;
			}
			if (!GExit.bStopped)
			{
				Report(Name, Exit->HasTriggered() && F->IsTransitioning(), FString::Printf(TEXT("inside the exit in Normal time: travel started %d, transition %d"),
					Exit->HasTriggered() ? 1 : 0, F->IsTransitioning() ? 1 : 0), GExit.Failures);
				// A second entry does nothing more: leave and come back while the screen goes black.
				Pawn->SetActorLocation(Inside - Exit->GetActorForwardVector() * 600.f, false, nullptr, ETeleportType::TeleportPhysics);
				Pawn->SetActorLocation(Inside, false, nullptr, ETeleportType::TeleportPhysics);
				Exit->TryTravel();
				Report(Name, Exit->HasTriggered(), TEXT("re-entering does not start a second travel (the exit fires once; the flow refuses meanwhile)"), GExit.Failures);
				Next();
				break;
			}
			Report(Name, !Exit->HasTriggered() && !F->IsTransitioning() && Exit->GetVolume()->IsOverlappingActor(Pawn),
				FString::Printf(TEXT("inside the exit for 2 s while Stopped: no travel (overlapping %d, triggered %d, transition %d)"),
					Exit->GetVolume()->IsOverlappingActor(Pawn) ? 1 : 0, Exit->HasTriggered() ? 1 : 0, F->IsTransitioning() ? 1 : 0), GExit.Failures);
			TimeStateDebug::PressKey(World, EKeys::Q);
			Next();
			break;
		case 3:
			if (Since < 0.1)
			{
				break;
			}
			if (GExit.bStopped)
			{
				Report(Name, !Time->IsStopped() && Exit->HasTriggered() && F->IsTransitioning(), FString::Printf(TEXT("resumed inside the exit: time %s, travel started %d, transition %d"),
					Time->IsStopped() ? TEXT("Stopped") : TEXT("Normal"), Exit->HasTriggered() ? 1 : 0, F->IsTransitioning() ? 1 : 0), GExit.Failures);
			}
			Next();
			break;
		default:
			if (Since > 1.0)
			{
				UE_LOG(LogTheLastShift, Display, TEXT("%s: ===== %s (%d failures) ====="), *Name, GExit.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), GExit.Failures);
				GExit.bActive = false;
				return false;
			}
			break;
		}
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsTravelTestCommand(
	TEXT("tls.TravelTest"),
	TEXT("tls.TravelTest [Banked=3]: (PIE, Normal time) sets the banked energy, travels to the next area and checks on arrival: banked energy kept, session energy 0, cumulative stopped 0, entity dormant, no time stop and no movement while the area was not ready (Q and movement probed every tick), black until ready and for at least TravelHoldSeconds."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		URunSubsystem* Run = URunSubsystem::Get(World);
		UEnergySubsystem* Energy = UEnergySubsystem::Get(World);
		if (!World || !Run || !Energy || RunDebug::GTravel.bActive)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.TravelTest: needs a game world, or a test is already running."));
			return;
		}
		const int32 Banked = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 3;
		Energy->AddBankedEnergy(Banked - Energy->GetBankedEnergy());
		RunDebug::GTravel = RunDebug::FTravel();
		RunDebug::GTravel.bActive = true;
		RunDebug::GTravel.OldWorld = World;
		RunDebug::GTravel.FromAct = Run->GetActIndex();
		RunDebug::GTravel.Banked = Energy->GetBankedEnergy();
		RunDebug::GTravel.StartReal = FPlatformTime::Seconds();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.TravelTest: act %d, banked %d, session %d: travelling."), RunDebug::GTravel.FromAct, Energy->GetBankedEnergy(), Energy->GetSessionEnergy());
		if (!Run->TravelToNextArea())
		{
			UE_LOG(LogTheLastShift, Display, TEXT("tls.TravelTest: ===== FAIL (travel refused) ====="));
			RunDebug::GTravel.bActive = false;
			return;
		}
		RunDebug::GTravel.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&RunDebug::TickTravel));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsExitTestCommand(
	TEXT("tls.ExitTest"),
	TEXT("tls.ExitTest [Stopped=0]: (PIE, area ready, Normal time) Stopped=0: teleports into the exit volume: one travel starts, re-entering adds none. Stopped=1: stops time, teleports into the exit, 2 s later still no travel, resumes inside it: the travel starts."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (!World || !AAreaGenerator::IsAreaReady(World) || RunDebug::GExit.bActive || !RunDebug::FindExit(World))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ExitTest: needs a ready area with an exit, or a test is already running."));
			return;
		}
		RunDebug::GExit = RunDebug::FExit();
		RunDebug::GExit.bActive = true;
		RunDebug::GExit.bStopped = Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0;
		RunDebug::GExit.World = World;
		RunDebug::GExit.StepReal = FPlatformTime::Seconds();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.ExitTest: %s."), RunDebug::GExit.bStopped ? TEXT("entering while Stopped, then resuming") : TEXT("entering in Normal time"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&RunDebug::TickExit));
	}));

#endif // !UE_BUILD_SHIPPING

// SPDX-License-Identifier: MIT

// Debug console commands for the run across maps: tls.TravelTest (state across a travel, the player held until the
// next area is ready, black-screen timings), tls.ExitTest (the exit volume in Normal and in Stopped time),
// tls.RunChainTest (the whole run, map after map, to the main menu) and tls.Settings.Reload (recipes and seeds re-read
// from Config/DefaultGame.ini while tuning). They drive a core ticker that survives the map change. Compiled out of Shipping.

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
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Settings/StdProjectSettings.h"
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

	// --- tls.RunChainTest ---

	struct FChain
	{
		bool bActive = false;
		int32 TravelsLeft = 0;
		bool bQuit = false;
		/** FromMenu: Play is pressed once the menu's own transition is over (StdCore refuses it during one). */
		bool bPendingStart = false;
		TWeakObjectPtr<UWorld> LastWorld;
		double StartReal = 0.0;
		double ArrivedReal = 0.0;
		bool bArrived = false;
		/** Act the next arrival must have; 0 = the main menu, -1 = any (the map the test starts in). */
		int32 ExpectedAct = 0;
		int32 Arrivals = 0;
		int32 Failures = 0;
	};
	static FChain GChain;

	static void FinishChain(const TCHAR* Why)
	{
		UE_LOG(LogTheLastShift, Display, TEXT("tls.RunChainTest: ===== %s (%d arrivals, %d failures%s%s) ====="), GChain.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"),
			GChain.Arrivals, GChain.Failures, Why ? TEXT(", ") : TEXT(""), Why ? Why : TEXT(""));
		GChain.bActive = false;
		if (GChain.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.RunChainTest"));
		}
	}

	static void ReportArrival(UWorld* World)
	{
		const FString Name = TEXT("tls.RunChainTest");
		const FString Map = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
		const URunSubsystem* Run = URunSubsystem::Get(World);
		const int32 Act = Run ? Run->GetActIndex() : -1;
		const AAreaGenerator* Gen = AAreaGenerator::Find(World);
		++GChain.Arrivals;
		if (!Gen)
		{
			const FString Menu = UStdProjectSettings::Get()->MainMenuMap.ToSoftObjectPath().GetLongPackageName();
			Report(Name, GChain.ExpectedAct == 0 && Act == 0 && Map == Menu,
				FString::Printf(TEXT("arrival %d: %s, act %d (expected the main menu %s)"), GChain.Arrivals, *Map, Act, *Menu), GChain.Failures);
			return;
		}
		const AEntity* Entity = nullptr;
		for (TActorIterator<AEntity> It(World); It; ++It) { Entity = *It; break; }
		const bool bOk = Gen->IsReady() && Gen->bLastValidationPassed && (GChain.ExpectedAct < 0 || Act == GChain.ExpectedAct);
		Report(Name, bOk, FString::Printf(TEXT("arrival %d: %s, act %d (expected %d), seed %d (%d tried), hash %016llx, route %.1f m, PCG instances %d, time to ready %.2f s, validation %s, entity %s ContactResponse %s"),
			GChain.Arrivals, *Map, Act, GChain.ExpectedAct, Gen->GetSeed(), Gen->LastSeedsTried, static_cast<uint64>(Gen->GetLayoutHash()), Gen->GetRouteLength() / 100.f,
			Gen->GetPCGInstanceCount(), Gen->GetTimeToReadySeconds(), Gen->bLastValidationPassed ? TEXT("passed") : TEXT("FAILED"), *GetNameSafe(Entity),
			Entity ? LexToString(Entity->ContactResponse) : TEXT("-")), GChain.Failures);
	}

	static bool TickChain(float)
	{
		UWorld* World = GameWorld();
		const double Now = FPlatformTime::Seconds();
		if (Now - GChain.StartReal > 120.0)
		{
			++GChain.Failures;
			FinishChain(TEXT("no arrival within 120 s"));
			return false;
		}
		if (!World)
		{
			return true;
		}
		if (GChain.bPendingStart)
		{
			UStdFlowSubsystem* F = Flow(World);
			if (F && F->IsTransitioning())
			{
				return true;
			}
			GChain.bPendingStart = false;
			GChain.StartReal = Now;
			if (!F || !F->StartGameplay())
			{
				++GChain.Failures;
				FinishChain(TEXT("StartGameplay refused"));
				return false;
			}
			return true;
		}
		if (World != GChain.LastWorld.Get())
		{
			// A new world: wait until its area is ready (or failed) and the transition is over.
			const UStdFlowSubsystem* F = Flow(World);
			const AAreaGenerator* Gen = AAreaGenerator::Find(World);
			if ((Gen && !Gen->IsReady() && Gen->GetGenerationState() != EAreaGenerationState::Failed) || (F && F->IsTransitioning()))
			{
				return true;
			}
			GChain.LastWorld = World;
			GChain.bArrived = true;
			GChain.ArrivedReal = Now;
			ReportArrival(World);
			if (!Gen)
			{
				FinishChain(nullptr);
				return false;
			}
			return true;
		}
		if (!GChain.bArrived || Now - GChain.ArrivedReal < 1.0)
		{
			return true;
		}
		// Arrived and settled: travel on, or stop.
		GChain.bArrived = false;
		if (GChain.TravelsLeft <= 0)
		{
			FinishChain(nullptr);
			return false;
		}
		URunSubsystem* Run = URunSubsystem::Get(World);
		const int32 Act = Run ? Run->GetActIndex() : -1;
		GChain.ExpectedAct = Act >= UTheLastShiftSettings::Get()->AreaMaps.Num() ? 0 : Act + 1;
		--GChain.TravelsLeft;
		GChain.StartReal = Now;
		UE_LOG(LogTheLastShift, Display, TEXT("tls.RunChainTest: tls.Travel.Next from act %d (expecting %s)."), Act,
			GChain.ExpectedAct > 0 ? *FString::Printf(TEXT("act %d"), GChain.ExpectedAct) : TEXT("the main menu"));
		if (!Run || !Run->TravelToNextArea())
		{
			++GChain.Failures;
			FinishChain(TEXT("travel refused"));
			return false;
		}
		return true;
	}

	// --- tls.Settings.Reload ---

	static bool ParseRecipe(const FString& Value, FAreaRecipe& Out)
	{
		return FAreaRecipe::StaticStruct()->ImportText(*Value, &Out, nullptr, PPF_None, GLog, TEXT("FAreaRecipe")) != nullptr;
	}

	static void ReloadSettings()
	{
		const FString Path = FPaths::ProjectConfigDir() / TEXT("DefaultGame.ini");
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Settings.Reload: cannot read %s."), *Path);
			return;
		}
		FConfigFile File;
		File.CombineFromBuffer(Text, Path);
		const FConfigSection* Section = File.FindSection(TEXT("/Script/TheLastShift.TheLastShiftSettings"));
		if (!Section)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Settings.Reload: no [/Script/TheLastShift.TheLastShiftSettings] section in %s."), *Path);
			return;
		}
		TArray<FString> Values;
		Section->MultiFind(TEXT("AreaRecipes"), Values, true);
		TArray<FAreaRecipe> Recipes;
		for (const FString& Value : Values)
		{
			FAreaRecipe Recipe;
			if (!ParseRecipe(Value, Recipe))
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("tls.Settings.Reload: AreaRecipes entry %d does not parse; nothing changed."), Recipes.Num());
				return;
			}
			Recipes.Add(Recipe);
		}
		Values.Reset();
		Section->MultiFind(TEXT("AreaSeeds"), Values, true);
		TArray<int32> Seeds;
		for (const FString& Value : Values) { Seeds.Add(FCString::Atoi(*Value)); }
		Values.Reset();
		Section->MultiFind(TEXT("TestRecipe"), Values, true);
		FAreaRecipe Test;
		const bool bTest = Values.Num() > 0 && ParseRecipe(Values.Last(), Test);

		UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
		Settings->AreaRecipes = Recipes;
		Settings->AreaSeeds = Seeds;
		if (bTest)
		{
			Settings->TestRecipe = Test;
		}
		FString Summary;
		for (int32 I = 0; I < Recipes.Num(); ++I)
		{
			Summary += FString::Printf(TEXT("%sact %d: %d segments, seed %d"), I > 0 ? TEXT("; ") : TEXT(""), I + 1, Recipes[I].Segments.Num(), Seeds.IsValidIndex(I) ? Seeds[I] : -1);
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Settings.Reload: %s; test recipe %s (from %s, in memory only)."), *Summary, bTest ? TEXT("reloaded") : TEXT("kept"), *Path);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsRunChainTestCommand(
	TEXT("tls.RunChainTest"),
	TEXT("tls.RunChainTest [Travels=4] [FromMenu=0] [Quit=0]: FromMenu=1 first presses the main menu's Play (StdCore StartGameplay) once no transition is running. On every arrival (area ready, transition over) logs map, act, seed, hash, route, PCG instances, time to ready, validation and the entity's ContactResponse, then calls tls.Travel.Next until Travels are done; a travel after the last area must reach the main menu. Quit=1 exits the game at the end (packaged builds)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (!World || RunDebug::GChain.bActive)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.RunChainTest: needs a game world, or the test is already running."));
			return;
		}
		RunDebug::GChain = RunDebug::FChain();
		RunDebug::GChain.bActive = true;
		RunDebug::GChain.TravelsLeft = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, 8) : 4;
		const bool bFromMenu = Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0;
		RunDebug::GChain.bQuit = Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0;
		RunDebug::GChain.StartReal = FPlatformTime::Seconds();
		const FString From = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
		if (bFromMenu)
		{
			RunDebug::GChain.LastWorld = World;
			RunDebug::GChain.bPendingStart = true;
			RunDebug::GChain.ExpectedAct = UTheLastShiftSettings::Get()->GetActOfMap(UStdProjectSettings::Get()->GameplayMap.ToSoftObjectPath().GetLongPackageName());
			UE_LOG(LogTheLastShift, Display, TEXT("tls.RunChainTest: from %s, Play (StartGameplay) once the menu's transition is over, then %d travels."), *From, RunDebug::GChain.TravelsLeft);
		}
		else
		{
			// The current map is the first arrival.
			RunDebug::GChain.ExpectedAct = -1;
			UE_LOG(LogTheLastShift, Display, TEXT("tls.RunChainTest: from %s, %d travels."), *From, RunDebug::GChain.TravelsLeft);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&RunDebug::TickChain));
	}));

static FAutoConsoleCommand GTlsSettingsReloadCommand(
	TEXT("tls.Settings.Reload"),
	TEXT("tls.Settings.Reload: re-reads AreaRecipes, AreaSeeds and TestRecipe from Config/DefaultGame.ini into the settings in memory (recipe tuning without an editor restart); the next generation uses them."),
	FConsoleCommandDelegate::CreateStatic(&RunDebug::ReloadSettings));

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

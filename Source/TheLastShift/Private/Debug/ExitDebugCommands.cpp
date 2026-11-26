// SPDX-License-Identifier: MIT

// Brief 26, the way out: tls.ExitCheck (the exit corridor of an area or of Prologue_Arrival, the twin AI's room of area 3,
// the floor reaching the walls in an area's first closed corridor, with their shots) and tls.Exit.View (stands the player at
// the mouth of the map's exit corridor, looking in). Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Area/AreaExit.h"
#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Debug/DebugDrawComponent.h"
#include "Components/SpotLightComponent.h"
#include "Containers/Ticker.h"
#include "Energy/BeaconBody.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/LogTerminal.h"
#include "Energy/PowerTerminal.h"
#include "Energy/TwinAITerminal.h"
#include "Engine/GameInstance.h"
#include "Engine/Light.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flow/StdFlowSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Prologue/PrologueHatch.h"
#include "Residue/Residue.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TextChannel/TwinWriting.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/InteractPromptWidget.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace ExitDebug
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
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntil = 0.0;
		double ConditionStart = -1.0;
		bool bRunning = false;
		bool bQuit = false;
		FString Map;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FRun GRun;

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("ExitCheck   %s  %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What) { UE_LOG(LogTheLastShift, Log, TEXT("ExitCheck   ..  %s"), *What); }

	static FStep Do(const FString& Label, TFunction<float()> Fn) { FStep S; S.Label = Label; S.Do = MoveTemp(Fn); return S; }

	static FStep WaitFor(const FString& Label, TFunction<bool()> Until, float Timeout, TFunction<float()> Then = nullptr)
	{
		FStep S;
		S.Label = Label;
		S.Until = MoveTemp(Until);
		S.Timeout = Timeout;
		S.Do = MoveTemp(Then);
		return S;
	}

	// --- World access ---

	static UWorld* W() { return ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }
	static AAreaGenerator* Generator() { return AAreaGenerator::Find(W()); }
	static AAreaInfo* Info() { return AAreaInfo::Find(W()); }
	static UTheLastShiftSettings* MutableSettings() { return GetMutableDefault<UTheLastShiftSettings>(); }

	static AAreaExit* Exit()
	{
		UWorld* World = W();
		for (TActorIterator<AAreaExit> It(World); World && It; ++It) { return *It; }
		return nullptr;
	}

	static ATwinAITerminal* Twin()
	{
		UWorld* World = W();
		for (TActorIterator<ATwinAITerminal> It(World); World && It; ++It) { return *It; }
		return nullptr;
	}

	static bool IsTransitioning()
	{
		const UWorld* World = W();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		return Flow && Flow->IsTransitioning();
	}

	static bool IsReady()
	{
		UWorld* World = W();
		if (!World || !Pawn() || !TS() || IsTransitioning())
		{
			return false;
		}
		const AAreaGenerator* G = Generator();
		return !G || G->IsReady();
	}

	static FString ShotFolder() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief26"))); }

	static void Shot(const FString& Name)
	{
		const FString Path = FPaths::Combine(ShotFolder(), GRun.Map + TEXT("_") + Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		Note(FString::Printf(TEXT("screenshot requested: %s"), *Path));
	}

	static void SetFlashlight(bool bOn)
	{
		if (const APawn* P = Pawn())
		{
			TArray<USpotLightComponent*> Lights;
			P->GetComponents(Lights);
			for (USpotLightComponent* Light : Lights) { Light->SetVisibility(bOn); }
		}
	}

	/** The pawn on the floor at (X, Y) (floor top at FloorZ), looking at Look. */
	static void StandAt(const FVector& At, const FVector& Look, float FloorZ = 0.f)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		if (!P || !C)
		{
			return;
		}
		const ACharacter* Character = Cast<ACharacter>(P);
		const float Half = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
		P->SetActorLocation(FVector(At.X, At.Y, FloorZ + Half + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		if (Character)
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		const UCameraComponent* Camera = P->FindComponentByClass<UCameraComponent>();
		const FVector Eye = Camera ? Camera->GetComponentLocation() : P->GetActorLocation() + FVector(0.f, 0.f, 64.f);
		C->SetControlRotation((Look - Eye).Rotation());
	}

	static float EyeAboveFloor()
	{
		const APawn* P = Pawn();
		const ACharacter* Character = Cast<ACharacter>(P);
		return (Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f) + (P ? P->BaseEyeHeight : 64.f);
	}

	// --- The corridor's frame ---

	struct FCorridor
	{
		bool bValid = false;
		FVector Mouth = FVector::ZeroVector;  // floor height
		FVector Dir = FVector(1.0, 0.0, 0.0);
		float Width = 400.f;
		float Height = 400.f;
		float Length = 0.f;  // from the mouth to its open end
		float ExitDepth = 0.f;
		float WantDepth = 0.f;
		float WantRun = 0.f;
		int32 Segment = -1;
	};

	struct FState
	{
		FCorridor Corridor;
		// The hash phase (areas).
		int64 OldHash = 0;
		int64 NewHash = 0;
		TArray<FString> OldLines, NewLines;
		TArray<TArray<FBox>> OldBoxes, NewBoxes;
		// Floor gap (areas): before (strips off) and after.
		float GapBefore = -1.f, GapAfter = -1.f;
		int32 GapSamplesBefore = 0, GapSamplesAfter = 0;
		FVector FloorShotAt = FVector::ZeroVector, FloorShotLook = FVector::ZeroVector;
		bool bGenerationDone = false;
		bool bGenerationPassed = false;
		FDelegateHandle GenerationHandle;
		int32 WalkTicks = 0;
		bool bWalking = false;
	};
	static FState G;

	static FVector Lat(const FVector& Dir) { return FVector(-Dir.Y, Dir.X, 0.0); }

	/** The exit corridor of the area (its record) or, in a hand-built map, from the exit and the traces around it. */
	static FCorridor FindCorridor()
	{
		FCorridor C;
		const AAreaInfo* I = Info();
		const AAreaGenerator* Gen = Generator();
		const AAreaExit* E = Exit();
		if (I && Gen)
		{
			for (int32 S = 0; S < I->Segments.Num(); ++S)
			{
				const FAreaSegmentRecord& R = I->Segments[S];
				if (!R.bExitCorridor)
				{
					continue;
				}
				C.bValid = true;
				C.Segment = S;
				C.Mouth = R.Mouth;
				C.Dir = R.Direction.GetSafeNormal2D();
				C.Width = R.Width;
				C.Height = Gen->WallHeight;
				C.Length = R.RouteEnd - R.RouteStart;
				const FAreaRecipe* Recipe = Gen->GetRecipe();
				C.WantDepth = Recipe ? FMath::Clamp(Recipe->ExitTriggerDepthM, 2.f, 50.f) * 100.f : 0.f;
				C.WantRun = Recipe ? FMath::Clamp(Recipe->ExitRunLengthM, 10.f, 200.f) * 100.f : 0.f;
			}
			if (E && C.bValid)
			{
				C.ExitDepth = static_cast<float>(FVector::DotProduct(E->GetActorLocation() - C.Mouth, C.Dir));
			}
			return C;
		}
		// A hand-built map (Prologue_Arrival): along the exit's forward; the mouth is the exit door's plane; the corridor's
		// section and length from traces; the run it must have: area 1's recipe (the act it leads to).
		UWorld* World = W();
		if (!E || !World)
		{
			return C;
		}
		C.Dir = E->GetActorForwardVector().GetSafeNormal2D();
		const FVector Axis = FVector(E->GetActorLocation().X, E->GetActorLocation().Y, 0.0);
		const ADoorDevice* Door = nullptr;
		for (TActorIterator<APrologueHatch> It(World); It; ++It) { Door = It->ExitDoor; break; }
		const double DoorAt = Door ? FVector::DotProduct(Door->GetActorLocation() - Axis, C.Dir) : -1000.0;
		C.Mouth = Axis + C.Dir * DoorAt;
		C.Mouth.Z = 0.0;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ExitCheck), false, Pawn());
		FHitResult Hit;
		const FVector Mid = Axis + FVector(0.0, 0.0, 100.0);
		float Left = 0.f, Right = 0.f, Up = 0.f, Floor = 0.f;
		if (World->LineTraceSingleByChannel(Hit, Mid, Mid + Lat(C.Dir) * 1000.0, ECC_Visibility, Params)) { Left = Hit.Distance; }
		if (World->LineTraceSingleByChannel(Hit, Mid, Mid - Lat(C.Dir) * 1000.0, ECC_Visibility, Params)) { Right = Hit.Distance; }
		if (World->LineTraceSingleByChannel(Hit, Mid, Mid + FVector(0, 0, 2000), ECC_Visibility, Params)) { Up = Hit.Distance; }
		if (World->LineTraceSingleByChannel(Hit, Mid, Mid - FVector(0, 0, 500), ECC_Visibility, Params)) { Floor = Hit.Distance; }
		C.Mouth.Z = Mid.Z - Floor;
		C.Width = Left + Right;
		C.Height = Up + Floor;
		// Centre the axis between the walls.
		C.Mouth += Lat(C.Dir) * (0.5 * (Left - Right));
		// Its end: the stop the player cannot pass, along the axis.
		if (World->LineTraceSingleByChannel(Hit, Mid, Mid + C.Dir * 50000.0, ECC_Visibility, Params))
		{
			C.Length = static_cast<float>(FVector::DotProduct(Hit.ImpactPoint - C.Mouth, C.Dir));
		}
		C.ExitDepth = static_cast<float>(FVector::DotProduct(E->GetActorLocation() - C.Mouth, C.Dir));
		const TArray<FAreaRecipe>& Recipes = UTheLastShiftSettings::Get()->AreaRecipes;
		C.WantRun = Recipes.Num() > 0 ? FMath::Clamp(Recipes[0].ExitRunLengthM, 10.f, 200.f) * 100.f : 6000.f;
		C.WantDepth = C.ExitDepth;
		C.bValid = C.Width > 0.f && C.Length > 0.f;
		return C;
	}

	/** World boxes of every rendered primitive (each instance of an instanced one) intersecting Region; the pawn's left out. */
	static TArray<FString> VisibleIn(const FBox& Region)
	{
		TArray<FString> Out;
		UWorld* World = W();
		const APawn* P = Pawn();
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			const UPrimitiveComponent* Prim = *It;
			if (!Prim || Prim->GetWorld() != World || !Prim->IsRegistered() || !Prim->IsVisible() || Prim->bHiddenInGame || !Prim->bRenderInMainPass
				|| (P && Prim->GetOwner() == P) || Prim->IsA<UBoxComponent>() || Prim->IsA<UCapsuleComponent>()
				// Debug drawing (line batches, the navmesh's and the gameplay debugger's rendering), not the level's geometry.
				|| !Prim->GetOwner() || Prim->IsA<ULineBatchComponent>() || Prim->IsA<UDebugDrawComponent>())
			{
				continue;
			}
			if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Prim))
			{
				const UStaticMesh* Mesh = ISM->GetStaticMesh();
				if (!Mesh)
				{
					continue;
				}
				const FBox Local = Mesh->GetBoundingBox();
				for (int32 K = 0; K < ISM->GetInstanceCount() && Out.Num() < 16; ++K)
				{
					FTransform T;
					ISM->GetInstanceTransform(K, T, true);
					const FBox B = Local.TransformBy(T);
					if (B.Intersect(Region)) { Out.Add(FString::Printf(TEXT("%s/%s[%d] %s"), *GetNameSafe(ISM->GetOwner()), *Mesh->GetName(), K, *B.GetCenter().ToCompactString())); }
				}
				continue;
			}
			if (Prim->Bounds.GetBox().Intersect(Region) && Out.Num() < 16)
			{
				Out.Add(FString::Printf(TEXT("%s/%s %s"), *GetNameSafe(Prim->GetOwner()), *Prim->GetName(), *Prim->Bounds.Origin.ToCompactString()));
			}
		}
		return Out;
	}

	static FBox CorridorRegion(const FCorridor& C, float From, float To, float LatHalf, float Z0, float Z1)
	{
		const FVector L = Lat(C.Dir);
		FBox B(ForceInit);
		for (const float A : { From, To })
		{
			for (const float S : { -LatHalf, LatHalf })
			{
				const FVector P = C.Mouth + C.Dir * A + L * S;
				B += FVector(P.X, P.Y, C.Mouth.Z + Z0);
				B += FVector(P.X, P.Y, C.Mouth.Z + Z1);
			}
		}
		return B;
	}

	// --- Criterion 1: the exit corridor ---

	static void CheckCorridor()
	{
		const FCorridor& C = G.Corridor;
		UWorld* World = W();
		const AAreaExit* E = Exit();
		const AAreaInfo* I = Info();
		const AAreaGenerator* Gen = Generator();
		Check(C.bValid && E, FString::Printf(TEXT("corridor: the map has an exit corridor and an exit (mouth %s, heading yaw %.0f, %.0f x %.0f cm, %.0f cm long)"),
			*C.Mouth.ToCompactString(), C.Dir.Rotation().Yaw, C.Width, C.Height, C.Length));
		if (!C.bValid || !E || !World)
		{
			return;
		}
		if (I && Gen)
		{
			int32 Count = 0;
			for (const FAreaSegmentRecord& R : I->Segments) { Count += R.bExitCorridor ? 1 : 0; }
			Check(Count == 1 && C.Segment == I->Segments.Num() - 1 && I->Segments[C.Segment].bClosed && I->Segments[C.Segment].Type == EAreaSegmentType::Corridor,
				FString::Printf(TEXT("corridor: exactly one exit corridor (%d), a closed corridor, the route's last segment (%d of %d)"), Count, C.Segment, I->Segments.Num()));
			const FAreaRuleResult* Rule9 = Gen->GetLastValidation().Rules.FindByPredicate([](const FAreaRuleResult& R) { return R.Rule == 9; });
			Check(Rule9 && Rule9->bPass, FString::Printf(TEXT("corridor: the validator's rule 9 passes (%s)"), Rule9 ? *Rule9->Summary : TEXT("no rule 9")));
		}
		// Straight and closed: every metre from the mouth to the end, the floor, both walls and the ceiling where they should be.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ExitCheck), false, Pawn());
		const FVector L = Lat(C.Dir);
		int32 Samples = 0, Bad = 0;
		FString FirstBad;
		for (float A = 50.f; A <= C.Length - 50.f; A += 100.f)
		{
			++Samples;
			const FVector Mid = C.Mouth + C.Dir * A + FVector(0.0, 0.0, 100.0);
			FHitResult Hit;
			const bool bFloor = World->LineTraceSingleByChannel(Hit, Mid, Mid - FVector(0, 0, 200), ECC_Visibility, Params) && FMath::Abs(Hit.ImpactPoint.Z - C.Mouth.Z) <= 1.0;
			// A wall: its face where the corridor's width puts it, or a wall panel's rib up to 10 cm in front of it (a
			// hand-built map's panels collide with their own shape; an area's walls are boxes).
			auto IsWall = [&](float Distance) { return Distance >= C.Width * 0.5f - 11.f && Distance <= C.Width * 0.5f + 1.f; };
			const bool bLeft = World->LineTraceSingleByChannel(Hit, Mid, Mid + L * (C.Width * 0.5f + 100.f), ECC_Visibility, Params) && IsWall(Hit.Distance);
			const bool bRight = World->LineTraceSingleByChannel(Hit, Mid, Mid - L * (C.Width * 0.5f + 100.f), ECC_Visibility, Params) && IsWall(Hit.Distance);
			const bool bUp = World->LineTraceSingleByChannel(Hit, Mid, Mid + FVector(0, 0, C.Height + 100.f), ECC_Visibility, Params) && FMath::Abs(Hit.ImpactPoint.Z - (C.Mouth.Z + C.Height)) <= 1.0;
			if (!(bFloor && bLeft && bRight && bUp))
			{
				++Bad;
				if (FirstBad.IsEmpty())
				{
					FirstBad = FString::Printf(TEXT(" (first at %.0f cm: floor %d, left %d, right %d, ceiling %d)"), A, bFloor, bLeft, bRight, bUp);
				}
			}
		}
		Check(Samples > 0 && Bad == 0, FString::Printf(TEXT("corridor: straight and closed, every metre (%d samples) the floor at its height, both walls %.0f cm from the axis and the ceiling at %.0f cm (%d off%s)"),
			Samples, C.Width * 0.5f, C.Height, Bad, *FirstBad));
		// The exit: its depth, the whole section.
		const FVector Extent = E->GetVolume()->GetScaledBoxExtent();
		const FVector P = E->GetActorLocation();
		const float Off = static_cast<float>(FMath::Abs(FVector::DotProduct(P - C.Mouth, L)));
		const bool bSection = Extent.Y >= 0.5f * C.Width - 1.f && P.Z - Extent.Z <= C.Mouth.Z + 1.f && P.Z + Extent.Z >= C.Mouth.Z + C.Height - 1.f && Off <= 1.f;
		Check(FMath::Abs(C.ExitDepth - C.WantDepth) <= 51.f, FString::Printf(TEXT("exit: %.0f cm in from the mouth (want %.0f)"), C.ExitDepth, C.WantDepth));
		Check(bSection, FString::Printf(TEXT("exit: across the whole section (extent %s at %s, %.0f cm off the axis; section %.0f x %.0f)"), *Extent.ToCompactString(), *P.ToCompactString(), Off, C.Width, C.Height));
		Check(FMath::Abs((C.Length - C.ExitDepth) - C.WantRun) <= 51.f, FString::Printf(TEXT("run: %.0f cm past the exit (want %.0f)"), C.Length - C.ExitDepth, C.WantRun));
		// No wall at the end: nothing rendered stands in the corridor's section beyond its end, out to 300 m; what stops the
		// player there is collision only.
		const TArray<FString> Beyond = VisibleIn(CorridorRegion(C, C.Length + 25.f, C.Length + 30000.f, C.Width * 0.5f, 0.f, C.Height));
		Check(Beyond.Num() == 0, FString::Printf(TEXT("end: nothing rendered closes the corridor beyond its end, to 300 m (%d found%s%s)"), Beyond.Num(), Beyond.Num() > 0 ? TEXT(": ") : TEXT(""), *FString::Join(Beyond, TEXT("; "))));
		{
			FHitResult Hit;
			const FVector From = C.Mouth + C.Dir * (C.Length - 200.f) + FVector(0, 0, 100);
			const bool bStop = World->LineTraceSingleByChannel(Hit, From, From + C.Dir * 400.f, ECC_Pawn, Params);
			const UPrimitiveComponent* Stop = Hit.GetComponent();
			const bool bHidden = Stop && (!Stop->bRenderInMainPass || !Stop->IsVisible() || Stop->bHiddenInGame);
			Check(bStop && bHidden && FMath::Abs(Hit.Distance - 200.f) <= 26.f,
				FString::Printf(TEXT("end: the player is stopped at the end by collision only (%s, %.0f cm from the end, rendered %s)"), Stop ? *Stop->GetName() : TEXT("nothing"),
					bStop ? Hit.Distance - 200.f : -1.f, bHidden ? TEXT("no") : TEXT("YES")));
		}
		// Nothing placed in it.
		const FBox Inside = CorridorRegion(C, 0.f, C.Length, C.Width * 0.5f, -10.f, C.Height);
		TArray<FString> Found;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const AActor* A = *It;
			const bool bKind = A->IsA<AResidue>() || A->IsA<APowerTerminal>() || A->IsA<ALightGroupDevice>() || A->IsA<ALight>() || A->IsA<ABeaconBody>()
				|| (A->IsA<ADoorDevice>() && FVector::DotProduct(A->GetActorLocation() - C.Mouth, C.Dir) > 50.0);
			if (bKind && Inside.IsInsideOrOn(A->GetActorLocation())) { Found.Add(A->GetName()); }
		}
		const bool bNoNiches = !I || !I->Segments.IsValidIndex(C.Segment) || (I->Segments[C.Segment].NichesPlaced == 0 && I->Segments[C.Segment].SpursPlaced == 0);
		Check(Found.Num() == 0 && bNoNiches, FString::Printf(TEXT("corridor: holds nothing (no terminal, residue, door, light, beacon: %s; niches and side passages: %s)"),
			Found.Num() > 0 ? *FString::Join(Found, TEXT(", ")) : TEXT("none"), bNoNiches ? TEXT("none") : TEXT("SOME")));
	}

	// --- Criterion 5: the floor reaches the walls ---

	/** The first closed corridor of the area (not the exit corridor), or -1. */
	static int32 FirstCorridor()
	{
		const AAreaInfo* I = Info();
		for (int32 S = 0; I && S < I->Segments.Num(); ++S)
		{
			if (I->Segments[S].Type == EAreaSegmentType::Corridor && I->Segments[S].bClosed && !I->Segments[S].bExitCorridor) { return S; }
		}
		return -1;
	}

	/**
	 * Every metre along the first closed corridor's route, on each side: the floor's reach toward the wall at floor level (the
	 * plates' boxes, strips included) against the wall's face there (a wall panel's slab, or its rib's face where the line
	 * meets a rib; a post's face). Samples where no wall stands within 1 m of the floor's edge (a niche's mouth, a doorway)
	 * are skipped. Returns the largest gap (cm) and the samples measured.
	 */
	static FString GWorstGap;

	static float MeasureFloorGap(int32& OutSamples)
	{
		OutSamples = 0;
		GWorstGap.Reset();
		const AAreaGenerator* Gen = Generator();
		const AAreaInfo* I = Info();
		const int32 S = FirstCorridor();
		if (!Gen || !I || S < 0)
		{
			return -1.f;
		}
		const FAreaSegmentRecord& Seg = I->Segments[S];
		const double T = Gen->WallThickness;
		const FVector PanelSize = Gen->DressWallPanelSize;
		const FVector PlateSize = Gen->DressFloorPlateSize;
		const FVector PostSize = Gen->DressCornerPostSize;
		// Only the pieces near this corridor (its bounds grown 3 m).
		const FBox Near = Seg.Bounds.ExpandBy(FVector(300.0, 300.0, 50.0));
		TArray<FBox> Plates, Posts;
		for (const FTransform& Plate : Gen->DressFloorPlates)
		{
			const FBox B = FBox(FVector(-0.5 * PlateSize.X, -0.5 * PlateSize.Y, 0.0), FVector(0.5 * PlateSize.X, 0.5 * PlateSize.Y, PlateSize.Z)).TransformBy(Plate);
			if (B.Intersect(Near) && B.Max.Z > -0.5 && B.Max.Z < 0.5) { Plates.Add(B); }
		}
		for (const FTransform& Post : Gen->DressCornerPosts)
		{
			const FBox B = FBox(FVector(-0.5 * PostSize.X, -0.5 * PostSize.Y, 0.0), FVector(0.5 * PostSize.X, 0.5 * PostSize.Y, PostSize.Z)).TransformBy(Post);
			if (B.Intersect(Near) && B.Min.Z <= 1.0 && B.Max.Z >= 0.0) { Posts.Add(B); }
		}
		TArray<FTransform> Panels;
		for (const FTransform& Panel : Gen->DressWallPanels)
		{
			if (Near.IsInsideOrOn(Panel.GetLocation())) { Panels.Add(Panel); }
		}
		float Worst = 0.f;
		for (float A = Seg.RouteStart + 50.f; A <= Seg.RouteEnd - 50.f; A += 100.f)
		{
			int32 Unused = -1;
			const FVector P = I->GetRoutePointAtDistance(A, Unused);
			const FVector D = I->GetRouteDirectionAtDistance(A);
			for (const double Side : { 1.0, -1.0 })
			{
				const FVector Out = Lat(D) * Side;
				// The floor's reach: step out while a plate's box holds the point at floor level.
				auto OnFloor = [&](const FVector& Q)
				{
					for (const FBox& B : Plates)
					{
						if (Q.X >= B.Min.X - 0.01 && Q.X <= B.Max.X + 0.01 && Q.Y >= B.Min.Y - 0.01 && Q.Y <= B.Max.Y + 0.01) { return true; }
					}
					return false;
				};
				double Reach = 0.0;
				while (Reach < 10000.0 && OnFloor(P + Out * (Reach + 0.25))) { Reach += 0.25; }
				// The wall's face along the same line: the nearest panel (slab or rib) or post face beyond the centreline.
				double Face = TNumericLimits<double>::Max();
				for (const FTransform& Panel : Panels)
				{
					// Panel space: +X toward the floor, Y along the wall.
					const FVector LP = Panel.InverseTransformPosition(P);
					const FVector LD = Panel.InverseTransformVectorNoScale(Out);
					if (LD.X > -0.9)
					{
						continue;  // not facing this line
					}
					const FVector Scale = Panel.GetScale3D();
					const double Y = LP.Y * Scale.Y;  // along, in cm of the placed panel
					if (FMath::Abs(Y) > 0.5 * PanelSize.Y * Scale.Y + 0.01)
					{
						continue;
					}
					// The slab's face at local X 0, a rib's at X +10, in the panel's own (unscaled along X) centimetres.
					const bool bRib = FMath::Abs(LP.Y) >= 42.5;
					const double FaceX = bRib ? 10.0 : 0.0;
					const double Dist = (LP.X - FaceX) * Scale.X;  // from P to the face, along -X
					if (Dist > 0.0) { Face = FMath::Min(Face, Dist); }
				}
				for (const FBox& B : Posts)
				{
					// Where the line enters the post's box.
					for (double R = 0.0; R < 10000.0; R += 0.25)
					{
						const FVector Q = P + Out * R;
						if (Q.X >= B.Min.X && Q.X <= B.Max.X && Q.Y >= B.Min.Y && Q.Y <= B.Max.Y) { Face = FMath::Min(Face, R); break; }
						if (R > Face) { break; }
					}
				}
				if (Face > Reach + 100.0)
				{
					continue;  // no wall here (a mouth or a doorway)
				}
				++OutSamples;
				const float Gap = static_cast<float>(FMath::Max(0.0, Face - Reach));
				if (Gap > Worst || GWorstGap.IsEmpty())
				{
					GWorstGap = FString::Printf(TEXT("at %s, %s side (yaw %.0f), the floor reaches %.1f cm, the wall's face %.1f cm"), *P.ToCompactString(),
						Side > 0.0 ? TEXT("left") : TEXT("right"), Out.Rotation().Yaw, Reach, Face);
				}
				Worst = FMath::Max(Worst, Gap);
			}
		}
		return Worst;
	}

	static void PlanFloorShot()
	{
		const AAreaInfo* I = Info();
		const int32 S = FirstCorridor();
		if (!I || S < 0)
		{
			return;
		}
		const FAreaSegmentRecord& Seg = I->Segments[S];
		const float A = FMath::Min(Seg.RouteStart + 500.f, 0.5f * (Seg.RouteStart + Seg.RouteEnd));
		int32 Unused = -1;
		const FVector P = I->GetRoutePointAtDistance(A, Unused);
		const FVector D = I->GetRouteDirectionAtDistance(A);
		// From 1 m off the centreline on one side, the other wall's foot 2.5 m ahead.
		G.FloorShotAt = P - Lat(D) * 100.f;
		const float Half = Generator() ? 0.5f * Generator()->ExitCorridorWidth : 200.f;
		G.FloorShotLook = P + D * 250.f + Lat(D) * Half + FVector(0, 0, 5);
	}

	// --- Generation (the hash phase) ---

	static void Regenerate(bool bWayOut, bool bStrips)
	{
		IConsoleManager::Get().FindConsoleVariable(TEXT("tls.Area.WayOut"))->Set(bWayOut ? 1 : 0, ECVF_SetByConsole);
		IConsoleManager::Get().FindConsoleVariable(TEXT("tls.Area.FloorStrips"))->Set(bStrips ? 1 : 0, ECVF_SetByConsole);
		AAreaGenerator* Gen = Generator();
		G.bGenerationDone = false;
		if (G.GenerationHandle.IsValid())
		{
			Gen->OnGenerationFinished.Remove(G.GenerationHandle);
		}
		G.GenerationHandle = Gen->OnGenerationFinished.AddLambda([](AAreaGenerator*, bool bPassed) { G.bGenerationDone = true; G.bGenerationPassed = bPassed; });
		const int32 Seed = Info() ? Info()->Seed : Gen->GetSeed();
		Gen->Regenerate(Seed, false);
	}

	static void Snapshot(int64& OutHash, TArray<FString>& OutLines, TArray<TArray<FBox>>& OutBoxes)
	{
		const AAreaGenerator* Gen = Generator();
		int32 Count = 0;
		OutHash = Gen->ComputeLayoutHash(Count, &OutLines);
		OutBoxes.Reset();
		for (int32 K = 0; K <= static_cast<int32>(EAreaPieceKind::Backdrop); ++K)
		{
			OutBoxes.Add(Gen->GetPublishedPieces(static_cast<EAreaPieceKind>(K)));
		}
	}

	/** The hash of the configured seed's layout before Brief 26 (the AREA READY lines of Brief 25's build). */
	static int64 HashBefore(int32 Act)
	{
		switch (Act)
		{
		case 1: return static_cast<int64>(0xc3f2c5cb15f24804ull);
		case 2: return static_cast<int64>(0x38505e2c94d2b8e8ull);
		case 3: return static_cast<int64>(0x9f42177dc9fa60b5ull);
		case 4: return static_cast<int64>(0x8ac592f2570a86b7ull);
		default: return 0;
		}
	}

	static FString HashText(int64 H) { return FString::Printf(TEXT("%016llx"), static_cast<uint64>(H)); }

	static void CompareLayouts()
	{
		// Rows: every row before kept as it is, but those that hold the plan's boxes, the exit, the navmesh bounds and the
		// area info (and, in area 3, the twin AI's terminal), which change because the route grows.
		TArray<FString> Gone, Added;
		for (const FString& L : G.OldLines) { if (!G.NewLines.Contains(L)) { Gone.Add(L); } }
		for (const FString& L : G.NewLines) { if (!G.OldLines.Contains(L)) { Added.Add(L); } }
		auto Head = [](const FString& L) { FString H; L.Split(TEXT("|"), &H, nullptr); return H.IsEmpty() ? L.Left(40) : H; };
		TArray<FString> GoneKinds, AddedKinds;
		for (const FString& L : Gone) { GoneKinds.Add(Head(L)); }
		for (const FString& L : Added) { AddedKinds.Add(Head(L)); }
		Note(FString::Printf(TEXT("hash rows: %d before, %d after; %d rows changed or gone (%s); %d rows new or changed (%s)"), G.OldLines.Num(), G.NewLines.Num(),
			Gone.Num(), *FString::Join(GoneKinds, TEXT(", ")), Added.Num(), *FString::Join(AddedKinds, TEXT(", "))));
		const TSet<FString> Allowed = { TEXT("AreaGeometry"), TEXT("AreaExit"), TEXT("NavMeshBoundsVolume"), TEXT("AreaInfo"), TEXT("TwinAITerminal"), TEXT("BP_TwinAITerminal_C") };
		bool bOnlyAllowed = true;
		for (const FString& K : GoneKinds) { bOnlyAllowed &= Allowed.Contains(K); }
		Check(bOnlyAllowed, TEXT("layout: every other row of the hash is as before (only the geometry, the exit, the navmesh bounds, the area info and the twin AI's terminal change)"));
		// The plan's boxes, kind by kind: those before, kept; the ones that changed at the joint; the ones added.
		const TCHAR* Names[] = { TEXT("floors"), TEXT("walls"), TEXT("curbs"), TEXT("pillars"), TEXT("ceilings"), TEXT("backdrop") };
		TArray<FString> Parts;
		int32 Lost = 0;
		for (int32 K = 0; K < G.OldBoxes.Num() && K < G.NewBoxes.Num(); ++K)
		{
			int32 Kept = 0;
			for (const FBox& B : G.OldBoxes[K])
			{
				bool bFound = false;
				for (const FBox& N : G.NewBoxes[K]) { if (N.Min.Equals(B.Min, 0.01) && N.Max.Equals(B.Max, 0.01)) { bFound = true; break; } }
				Kept += bFound ? 1 : 0;
			}
			Lost += G.OldBoxes[K].Num() - Kept;
			Parts.Add(FString::Printf(TEXT("%s %d -> %d (%d kept, %d changed or removed, %d new)"), Names[K], G.OldBoxes[K].Num(), G.NewBoxes[K].Num(), Kept, G.OldBoxes[K].Num() - Kept, G.NewBoxes[K].Num() - Kept));
		}
		Note(TEXT("plan boxes: ") + FString::Join(Parts, TEXT("; ")));
	}

	// --- Criterion 3: the twin AI's room ---

	static void AddTwinRoomSteps(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("twin AI's room"), []()
		{
			const AAreaInfo* I = Info();
			const FCorridor& C = G.Corridor;
			const FAreaSegmentRecord* Room = I && I->Segments.IsValidIndex(C.Segment - 1) ? &I->Segments[C.Segment - 1] : nullptr;
			Check(Room && Room->bTwinAIRoom && Room->bClosed, FString::Printf(TEXT("room: the segment right before the exit corridor (%d) is the twin AI's room"), C.Segment - 1));
			const ATwinAITerminal* T = Twin();
			if (!Room || !T)
			{
				Check(false, TEXT("room: no room or no twin AI terminal"));
				return -1.f;
			}
			const FVector In = Room->Direction.GetSafeNormal2D();
			const FVector Size = Room->Bounds.GetSize();
			Note(FString::Printf(TEXT("room: %.0f x %.0f cm (passage included), entered at %s heading yaw %.0f"), Size.X, Size.Y, *Room->Mouth.ToCompactString(), In.Rotation().Yaw));
			const bool bMouthInSide = Room->Bounds.ExpandBy(1.0).IsInsideOrOn(FVector(C.Mouth.X, C.Mouth.Y, 0.0)) && FMath::Abs(FVector::DotProduct(C.Dir, In)) < 0.01;
			Check(bMouthInSide, FString::Printf(TEXT("room: the exit corridor starts from a side wall of the room (mouth %s, corridor yaw %.0f)"), *C.Mouth.ToCompactString(), C.Dir.Rotation().Yaw));
			const double FarEdge = FVector::DotProduct(Room->Bounds.GetCenter() - Room->Mouth, In) + 0.5 * FMath::Abs(FVector::DotProduct(Size, In));
			const FBox Body = T->GetLocalBodyBox();
			const double Back = FVector::DotProduct(T->GetActorLocation() - Room->Mouth, In) - Body.Min.X;  // the body's back, along In
			const bool bFacing = FVector::DotProduct(T->GetActorForwardVector().GetSafeNormal2D(), -In) > 0.999;
			const double Centred = FVector::DotProduct(T->GetActorLocation() - Room->Bounds.GetCenter(), Lat(In));
			Check(Room->Bounds.ExpandBy(FVector(30.0, 30.0, 10.0)).IsInsideOrOn(T->GetActorLocation()) && bFacing && FMath::Abs(Back - FarEdge - 0.5 * Generator()->WallThickness) <= 1.0
				&& FMath::Abs(Centred) <= 1.0,
				FString::Printf(TEXT("room: the twin AI's terminal is in it, set into the wall facing the entrance (facing it %s, its back %.1f cm beyond the far wall's edge, %.1f cm off the middle; body %s)"),
					bFacing ? TEXT("yes") : TEXT("NO"), Back - FarEdge, Centred, *Body.GetSize().ToCompactString()));
			const UStaticMesh* Mesh = UTheLastShiftSettings::GetTwinTerminalBodyMesh();
			const UStaticMeshComponent* M = T->FindComponentByClass<UStaticMeshComponent>();
			// The conduits and cables: built by the structure graph from the generator's arrays.
			int32 Built = 0, Planned = 0;
			if (const AAreaGenerator* Gen = Generator())
			{
				Planned = Gen->DressTwinConduits.Num() + Gen->DressTwinBends.Num() + Gen->DressTwinCables.Num();
				TArray<UInstancedStaticMeshComponent*> ISMs;
				Gen->GetComponents(ISMs);
				for (const UInstancedStaticMeshComponent* ISM : ISMs)
				{
					for (const TCHAR* Tag : { TEXT("AreaDressTwinConduits"), TEXT("AreaDressTwinBends"), TEXT("AreaDressTwinCables") })
					{
						Built += ISM->ComponentHasTag(Tag) ? ISM->GetInstanceCount() : 0;
					}
				}
			}
			Check(Planned > 0 && Built == Planned, FString::Printf(TEXT("room: its conduits and cables are built (%d pieces of %d planned)"), Built, Planned));
			Check(Mesh && M && M->GetStaticMesh() == Mesh && M->bRenderCustomDepth && M->CustomDepthStencilValue == UTheLastShiftSettings::GetInteractableScanChannel(),
				FString::Printf(TEXT("room: the terminal shows SM_TwinTerminal (%s) with the interactable stencil value %d"), M ? *GetNameSafe(M->GetStaticMesh()) : TEXT("-"), M ? M->CustomDepthStencilValue : -1));
			// The Interact prompt from in front of it.
			const FVector Fwd = T->GetActorForwardVector().GetSafeNormal2D();
			const FVector Stand = T->GetActorLocation() + Fwd * (Body.Max.X + FMath::Clamp(Generator()->TerminalStandDistance, 50.f, 140.f));
			StandAt(Stand, T->GetActorLocation() + FVector(0, 0, 110));
			UEnergySubsystem::Get(W())->RestoreBankedEnergy(5);
			return 0.4f;
		}));
		S.Add(Do(TEXT("twin AI's room: the prompt"), []()
		{
			const ATwinAITerminal* T = Twin();
			UInteractPromptWidget* Prompt = nullptr;
			for (TObjectIterator<UInteractPromptWidget> It; It; ++It) { if (It->GetWorld() == W() && It->IsInViewport()) { Prompt = *It; break; } }
			const APowerTerminal* Nearest = APowerTerminal::FindNearestInRange(Pawn());
			Check(T && Nearest == T && Prompt && Prompt->GetMode() == EInteractPromptMode::Power && Prompt->GetShownText().Contains(FString::Printf(TEXT("— %d"), T->GetCost())),
				FString::Printf(TEXT("room: from in front of it the Interact prompt offers the twin AI's terminal (\"%s\", %.0f cm from it)"), Prompt ? *Prompt->GetShownText() : TEXT("-"),
					T && Pawn() ? FVector::Dist2D(Pawn()->GetActorLocation(), T->GetActorLocation()) : -1.f));
			// Area 3's energy, for the report.
			UWorld* World = W();
			int32 Residues = 0, Regen = 0, Gates = 0, GateCost = 0, Optional = 0, OptionalCost = 0;
			for (TActorIterator<AResidue> It(World); It; ++It) { ++Residues; Regen += It->bRegenerates ? 1 : 0; }
			for (TActorIterator<APowerTerminal> It(World); It; ++It)
			{
				if (It->IsA<ATwinAITerminal>()) { continue; }
				bool bDoor = false;
				for (const APoweredDevice* D : It->Targets) { bDoor |= D && D->IsA<ADoorDevice>(); }
				(bDoor ? Gates : Optional) += 1;
				(bDoor ? GateCost : OptionalCost) += It->GetCost();
			}
			const int32 Parts = TwinWriting::GetTwinPartCount(UTheLastShiftSettings::Get()->LoadTwinAITable());
			const int32 TwinCost = Parts > 0 ? UTheLastShiftSettings::GetTwinAIWakeCost() + (Parts - 1) * UTheLastShiftSettings::GetTwinAIPartCost() : 0;
			Note(FString::Printf(TEXT("ENERGY area 3: residues %d (%d regenerating, %d others: %d energy on one pass), mandatory payments %d (gates, %d energy), optional %d terminals (%d energy) and the twin AI's %d parts (%d + %d x %d = %d energy); paying everything: %d"),
				Residues, Regen, Residues - Regen, Residues, Gates, GateCost, Optional, OptionalCost, Parts, UTheLastShiftSettings::GetTwinAIWakeCost(), FMath::Max(0, Parts - 1),
				UTheLastShiftSettings::GetTwinAIPartCost(), TwinCost, GateCost + OptionalCost + TwinCost));
			// Shots: from the room's entrance with the flashlight, the same view in a scan, the terminal from 3 m.
			const AAreaInfo* I = Info();
			const FAreaSegmentRecord& Room = I->Segments[G.Corridor.Segment - 1];
			const FVector In = Room.Direction.GetSafeNormal2D();
			const float Passage = Generator()->TwinRoomPassageLength;
			SetFlashlight(true);
			StandAt(Room.Mouth + In * (Passage + 50.f), T->GetActorLocation() + FVector(0, 0, 150));
			return 0.6f;
		}));
		S.Add(Do(TEXT("twin AI's room: shot from the entrance"), []() { Shot(TEXT("room_entrance_flashlight")); return 0.4f; }));
		S.Add(Do(TEXT("twin AI's room: stop"), []() { TS()->Toggle(); return 0.f; }));
		S.Add(WaitFor(TEXT("room: the first scan of the Stop"), []()
		{
			const UScanDirectorComponent* D = TimeStateDebug::FindPlayerDirector(W());
			return IsStopped() && D && D->GetScansThisStop() >= 1;
		}, 6.f, []() { return 0.8f; }));
		S.Add(Do(TEXT("twin AI's room: shot in the scan"), []() { Shot(TEXT("room_entrance_scan")); return 0.4f; }));
		S.Add(Do(TEXT("twin AI's room: resume, the terminal from 3 m"), []()
		{
			if (IsStopped()) { TS()->Toggle(); }
			const ATwinAITerminal* T = Twin();
			StandAt(T->GetActorLocation() + T->GetActorForwardVector().GetSafeNormal2D() * 300.f, T->GetActorLocation() + FVector(0, 0, 150));
			return 0.8f;
		}));
		S.Add(Do(TEXT("twin AI's room: shot of the terminal"), []() { Shot(TEXT("room_terminal_3m")); return 0.4f; }));
	}

	// --- The run ---

	static void Finish(const TCHAR* Why)
	{
		if (AAreaExit* E = Exit()) { E->bTestHoldTravel = false; }
		IConsoleManager::Get().FindConsoleVariable(TEXT("tls.Area.WayOut"))->Set(1, ECVF_SetByConsole);
		IConsoleManager::Get().FindConsoleVariable(TEXT("tls.Area.FloorStrips"))->Set(1, ECVF_SetByConsole);
		if (AAreaGenerator* Gen = Generator(); Gen && G.GenerationHandle.IsValid())
		{
			Gen->OnGenerationFinished.Remove(G.GenerationHandle);
		}
		G.GenerationHandle.Reset();
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("ExitCheck summary (%s) in %s: %d checks passed, %d failed%s."), GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"),
			*GRun.Map, GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.ExitCheck"));
		}
	}

	static bool TickRun(float)
	{
		if (!GRun.bRunning)
		{
			return false;
		}
		if (G.bWalking)
		{
			if (APawn* P = Pawn()) { P->AddMovementInput(G.Corridor.Dir, 1.f); }
			++G.WalkTicks;
		}
		const double Now = FPlatformTime::Seconds();
		if (Now < GRun.WaitUntil)
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
			if (GRun.ConditionStart < 0.0) { GRun.ConditionStart = Now; }
			const bool bMet = Step.Until();
			const bool bTimedOut = Now - GRun.ConditionStart > Step.Timeout;
			if (!bMet && !bTimedOut)
			{
				return true;
			}
			Check(bMet, FString::Printf(TEXT("%s (waited %.2f s%s)"), *Step.Label, Now - GRun.ConditionStart, bTimedOut && !bMet ? TEXT(", TIMEOUT") : TEXT("")));
			GRun.ConditionStart = -1.0;
		}
		++GRun.Next;
		const float Delay = Step.Do ? Step.Do() : 0.f;
		if (Delay < 0.f)
		{
			Finish(*FString::Printf(TEXT(", aborted at step '%s'"), *Step.Label));
			return false;
		}
		GRun.WaitUntil = Now + Delay;
		return true;
	}

	/** At the mouth of the corridor, looking in along its axis (eye height). */
	static void StandAtMouth()
	{
		const FCorridor& C = G.Corridor;
		StandAt(C.Mouth + C.Dir * 50.f, C.Mouth + C.Dir * 5000.f + FVector(0, 0, EyeAboveFloor()), C.Mouth.Z);
	}

	static void AddViewSteps(TArray<FStep>& S)
	{
		// Criterion 2: from the mouth, one shot with the flashlight and one of a scan taken there.
		S.Add(Do(TEXT("view: stand at the mouth, flashlight on"), []()
		{
			if (IsStopped()) { TS()->Toggle(); }
			StandAtMouth();
			SetFlashlight(true);
			return 0.8f;
		}));
		S.Add(Do(TEXT("view: shot with the flashlight"), []() { Shot(TEXT("mouth_flashlight")); return 0.4f; }));
		S.Add(Do(TEXT("view: stop"), []() { StandAtMouth(); TS()->Toggle(); return 0.f; }));
		S.Add(WaitFor(TEXT("view: the first scan of the Stop, taken at the mouth"), []()
		{
			const UScanDirectorComponent* D = TimeStateDebug::FindPlayerDirector(W());
			return IsStopped() && D && D->GetScansThisStop() >= 1;
		}, 6.f, []() { return 0.8f; }));
		S.Add(Do(TEXT("view: shot of the scan"), []() { Shot(TEXT("mouth_scan")); return 0.4f; }));
		S.Add(Do(TEXT("view: resume"), []() { if (IsStopped()) { TS()->Toggle(); } return 0.6f; }));
	}

	/**
	 * Brief 26 follow-up: a player who walked past the exit while Stopped travels at once on the return to Normal time, as
	 * if they had walked into it; nothing while Stopped. The exit's travel is held (counted, not started) for this step, so
	 * the walk that follows still starts the real travel.
	 */
	static void AddPastExitSteps(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("past the exit: stop time at the mouth"), []()
		{
			if (IsStopped()) { TS()->Toggle(); }
			StandAtMouth();
			if (AAreaExit* E = Exit()) { E->bTestHoldTravel = true; E->TestTravelRequests = 0; }
			TS()->Toggle();
			return 0.3f;
		}));
		S.Add(Do(TEXT("past the exit: walk past it while Stopped"), []()
		{
			// Near the far end of the run: 1 m before the corridor's end, past the exit's far face by most of the run.
			const FCorridor& C = G.Corridor;
			StandAt(C.Mouth + C.Dir * (C.Length - 100.f), C.Mouth + C.Dir * (C.Length + 5000.f) + FVector(0, 0, EyeAboveFloor()), C.Mouth.Z);
			return 0.6f;
		}));
		S.Add(Do(TEXT("past the exit: resume"), []()
		{
			AAreaExit* E = Exit();
			const FCorridor& C = G.Corridor;
			const int32 WhileStopped = E ? E->TestTravelRequests : -1;
			const bool bWasStopped = IsStopped();
			const float Past = Pawn() ? static_cast<float>(FVector::DotProduct(Pawn()->GetActorLocation() - C.Mouth, C.Dir)) - C.ExitDepth : -1.f;
			TS()->Toggle();
			const int32 OnResume = E ? E->TestTravelRequests : -1;
			Check(E && bWasStopped && WhileStopped == 0 && OnResume == 1 && !IsStopped() && !E->HasTriggered(),
				FString::Printf(TEXT("past the exit: %.1f m past it, nothing while Stopped (%d travels asked), the travel at once on the return to Normal time (%d asked; run past the exit %.0f cm)"),
					Past / 100.f, WhileStopped, OnResume - FMath::Max(0, WhileStopped), E ? E->RunPastExit : -1.f));
			if (E) { E->bTestHoldTravel = false; E->TestTravelRequests = 0; }
			return 0.3f;
		}));
	}

	static void AddWalkSteps(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("walk: 3 m before the exit, walking in"), []()
		{
			const FCorridor& C = G.Corridor;
			if (IsStopped()) { TS()->Toggle(); }
			StandAt(C.Mouth + C.Dir * FMath::Max(50.f, C.ExitDepth - 300.f), C.Mouth + C.Dir * 5000.f + FVector(0, 0, EyeAboveFloor()), C.Mouth.Z);
			Check(Exit() && !Exit()->HasTriggered(), TEXT("walk: the exit has not triggered before the player reaches it"));
			G.bWalking = true;
			G.WalkTicks = 0;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("walk: walking into the trigger starts the travel"), []()
		{
			const AAreaExit* E = Exit();
			return (E && E->HasTriggered()) || IsTransitioning();
		}, 8.f, []()
		{
			G.bWalking = false;
			const AAreaExit* E = Exit();
			const URunSubsystem* Run = URunSubsystem::Get(W());
			Note(FString::Printf(TEXT("walk: after %d ticks, triggered %d, transition %d, act %d"), G.WalkTicks, E && E->HasTriggered() ? 1 : 0, IsTransitioning() ? 1 : 0,
				Run ? Run->GetActIndex() : -1));
			Check(E && E->HasTriggered(), TEXT("walk: the exit asked for the travel (to the next map; from area 4, the ending)"));
			return 0.f;
		}));
	}

	static void Start(bool bQuit)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ExitCheck is already running."));
			return;
		}
		GRun = FRun();
		G = FState();
		GRun.bQuit = bQuit;
		UWorld* World = W();
		GRun.Map = World ? World->GetMapName() : TEXT("none");
		GRun.Map.RemoveFromStart(World ? World->StreamingLevelsPrefix : FString());
		TArray<FStep>& S = GRun.Steps;
		S.Add(WaitFor(TEXT("the map is playable"), []() { return IsReady(); }, 90.f, []()
		{
			const AAreaGenerator* Gen = Generator();
			if (Gen)
			{
				const AAreaInfo* I = Info();
				Check(I && I->SeedsTried == 1 && Gen->bLastValidationPassed && Gen->GetSeed() == UTheLastShiftSettings::Get()->AreaSeeds[FMath::Clamp(Gen->GetAct() - 1, 0, 3)],
					FString::Printf(TEXT("load: act %d built from its configured seed %d at the first try (seeds tried %d), validation %s, hash %s"), Gen->GetAct(), Gen->GetSeed(),
						I ? I->SeedsTried : -1, Gen->bLastValidationPassed ? TEXT("passed") : TEXT("FAILED"), *HashText(Gen->GetLayoutHash())));
			}
			return 0.2f;
		}));
		const bool bArea = Generator() != nullptr;
		if (bArea)
		{
			// The layout without the way out (tls.Area.WayOut 0) and the floor without its strips: the hash before Brief 26, the gap.
			S.Add(Do(TEXT("before: regenerate without the way out and the floor strips"), []() { Regenerate(false, false); return 0.f; }));
			// Without the way out the validator's rule 9 fails by design: the build ends there, not ready.
			S.Add(WaitFor(TEXT("before: the area is built again"), []() { return G.bGenerationDone; }, 60.f, []()
			{
				Snapshot(G.OldHash, G.OldLines, G.OldBoxes);
				const int64 Want = HashBefore(Generator()->GetAct());
				Check(G.OldHash == Want, FString::Printf(TEXT("before: without the way out the layout is the one before Brief 26 (hash %s, want %s)"), *HashText(G.OldHash), *HashText(Want)));
				G.GapBefore = MeasureFloorGap(G.GapSamplesBefore);
				Note(FString::Printf(TEXT("floor before the fix: largest gap %.1f cm over %d samples of corridor segment %d (%s)"), G.GapBefore, G.GapSamplesBefore, FirstCorridor(), *GWorstGap));
				PlanFloorShot();
				SetFlashlight(true);
				StandAt(G.FloorShotAt, G.FloorShotLook);
				return 0.8f;
			}));
			S.Add(Do(TEXT("before: shot of the corridor's floor edge"), []() { Shot(TEXT("floor_edge_before")); return 0.4f; }));
			S.Add(Do(TEXT("after: regenerate with the way out and the strips"), []() { Regenerate(true, true); return 0.f; }));
			S.Add(WaitFor(TEXT("after: the area is built again"), []() { return G.bGenerationDone && Generator()->IsReady(); }, 60.f, []()
			{
				Check(G.bGenerationPassed && Generator()->bLastValidationPassed, TEXT("after: the area validates with the way out"));
				Generator()->GetLastValidation().Log(TEXT("ExitCheck"), true);
				Snapshot(G.NewHash, G.NewLines, G.NewBoxes);
				Note(FString::Printf(TEXT("HASH act %d: before %s, after %s"), Generator()->GetAct(), *HashText(G.OldHash), *HashText(G.NewHash)));
				CompareLayouts();
				G.GapAfter = MeasureFloorGap(G.GapSamplesAfter);
				Note(TEXT("floor after the fix: the largest gap ") + GWorstGap);
				Check(G.GapSamplesAfter > 0 && G.GapAfter <= 1.f, FString::Printf(TEXT("floor: in the first closed corridor the floor reaches both walls, every metre: largest gap %.2f cm over %d samples (before the fix %.1f cm over %d)"),
					G.GapAfter, G.GapSamplesAfter, G.GapBefore, G.GapSamplesBefore));
				SetFlashlight(true);
				StandAt(G.FloorShotAt, G.FloorShotLook);
				return 0.8f;
			}));
			S.Add(Do(TEXT("after: shot of the corridor's floor edge"), []() { Shot(TEXT("floor_edge_after")); return 0.4f; }));
		}
		else if (World)
		{
			// Prologue_Arrival: the exit door is opened by the Door lines; open it for the walk.
			S.Add(Do(TEXT("arrival: open the exit door"), []()
			{
				for (TActorIterator<APrologueHatch> It(W()); It; ++It)
				{
					if (It->ExitDoor) { It->ExitDoor->PowerOn(); }
				}
				return 1.5f;
			}));
		}
		S.Add(Do(TEXT("the corridor"), []()
		{
			G.Corridor = FindCorridor();
			CheckCorridor();
			return G.Corridor.bValid && Exit() ? 0.1f : -1.f;
		}));
		AddViewSteps(S);
		if (bArea && World && AAreaGenerator::Find(World)->GetRecipe() && AAreaGenerator::Find(World)->GetRecipe()->bTwinAIRoom)
		{
			AddTwinRoomSteps(S);
		}
		AddPastExitSteps(S);
		AddWalkSteps(S);
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("ExitCheck started in %s: %d steps."), *GRun.Map, S.Num());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsExitCheckCommand(
	TEXT("tls.ExitCheck"),
	TEXT("tls.ExitCheck [Quit=0]: (fresh PIE in an area map or Prologue_Arrival) Brief 26's checks of the way out. Areas: the layout regenerated without the way out "
		"and the floor strips (tls.Area.WayOut 0, tls.Area.FloorStrips 0: the hash before Brief 26, the floor's gap, a shot of the first closed corridor's floor edge), "
		"then with them (the hash, the rows and plan boxes that changed, the gap every metre of that corridor: none over 1 cm, the same shot). Every map: one exit "
		"corridor, the route's last, straight and closed (floor, walls and ceiling every metre), the exit at the recipe's depth across the whole section, the run past "
		"it, nothing rendered beyond its end and collision only stopping the player there, nothing placed in it; shots from its mouth with the flashlight and in a "
		"scan; area 3: the twin AI's room right before it, its terminal set into the wall facing the entrance with SM_TwinTerminal, the Interact prompt from in front "
		"of it, area 3's energy count, three shots; last, walking into the trigger starts the travel. Shots to Saved/Screenshots/Brief26."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		ExitDebug::Start(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsExitViewCommand(
	TEXT("tls.Exit.View"),
	TEXT("tls.Exit.View: (an area map or Prologue_Arrival) stands the player at the mouth of the map's exit corridor, looking in."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		using namespace ExitDebug;
		G.Corridor = FindCorridor();
		if (!G.Corridor.bValid)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Exit.View: this map has no exit corridor."));
			return;
		}
		StandAtMouth();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Exit.View: at the mouth %s, looking in (yaw %.0f), the exit %.0f cm in, the corridor %.0f cm long."),
			*G.Corridor.Mouth.ToCompactString(), G.Corridor.Dir.Rotation().Yaw, G.Corridor.ExitDepth, G.Corridor.Length);
	}));

#endif

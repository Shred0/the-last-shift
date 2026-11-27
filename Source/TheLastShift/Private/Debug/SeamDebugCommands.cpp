// SPDX-License-Identifier: MIT

// Brief 35, the seams the player must not see. tls.SeamCheck [Quit=0] [Tag=After] checks, in the map it runs in (an area,
// Prologue_Home, Prologue_Arrival or Epilogue_Chamber; PIE), what that map has of four things:
//   A. the floor under every door and sliding panel that opens: across the opening every 20 cm and through its depth, a sample
//      straight down meets a visible floor piece whose top is the opening's floor (collision-only boxes, which the area
//      generator never draws, do not count: the player sees the pieces, not the boxes); and no floor piece stands in a door's
//      travel (closed to open);
//   B. each hangar's hatch: the hangar's floor runs out past the hatch's own panel (open) and frame, all across the opening, and
//      a capsule still cannot pass the opening;
//   C. every door's and gate's frame: no face of it lies within 1 cm of the plane of another face facing the same way that it
//      overlaps (another piece's, or its own doubled); faces that touch back to back, and faces that face down onto a floor's
//      top, are listed (they cannot show); and on 30 frames while the camera moves 1 cm a frame, from 3 m and from 10 m, each
//      captured with the frame hidden and drawn, no pixel alternates between the frame and another surface;
// with shots (Tag in their names: Before, After) to Saved/Screenshots/Brief35. Development builds only.

#include "Debug/TlsDebugCommon.h"
#include "Debug/TestLidarCharacter.h"
#include "Area/AreaGenerator.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Energy/DoorDevice.h"
#include "Entity/EntityFigureShape.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Prologue/PrologueHatch.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING

namespace SeamDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner on the core ticker (the pattern of the Brief 29 checks) ---

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
		bool bQuit = false;
		FString Tag;
		TFunction<void()> Cleanup;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FRun GRun;
	static const TCHAR* Name = TEXT("tls.SeamCheck");

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), Name, bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), Name, *What);
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

	static void Finish(const TCHAR* Why)
	{
		if (GRun.Cleanup)
		{
			GRun.Cleanup();
			GRun.Cleanup = nullptr;
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.SeamCheck"));
		}
	}

	static UWorld* W() { return ResolveGameWorld(nullptr); }

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
		GRun.WaitUntilReal = FPlatformTime::Seconds() + Delay;
		return true;
	}

	// --- World access ---

	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static ACharacter* Pawn() { const APlayerController* C = PC(); return C ? Cast<ACharacter>(C->GetPawn()) : nullptr; }

	static FString MapShort()
	{
		const UWorld* World = W();
		return World ? UWorld::RemovePIEPrefix(FPaths::GetBaseFilename(World->GetOutermost()->GetName())) : FString();
	}

	static FVector EyeOffset()
	{
		const ACharacter* P = Pawn();
		const UCameraComponent* Camera = P ? P->FindComponentByClass<UCameraComponent>() : nullptr;
		return Camera ? Camera->GetComponentLocation() - P->GetActorLocation() : FVector(0.f, 0.f, 62.f);
	}

	/** Puts the pawn's eye at Eye (no sweep, no fall while the check holds it: movement stopped) looking at LookAt. */
	static void PlaceEye(const FVector& Eye, const FVector& LookAt)
	{
		ACharacter* P = Pawn();
		if (!P)
		{
			return;
		}
		P->SetActorLocation(Eye - EyeOffset(), false, nullptr, ETeleportType::TeleportPhysics);
		P->GetCharacterMovement()->StopMovementImmediately();
		P->GetCharacterMovement()->DisableMovement();
		if (APlayerController* C = PC())
		{
			C->SetControlRotation((LookAt - Eye).Rotation());
		}
	}

	static void FlashlightOn()
	{
		// Only where the flashlight works (not at home, which is lit).
		if (const ATestLidarCharacter* P = Cast<ATestLidarCharacter>(Pawn()); P && P->IsFlashlightAvailable())
		{
			if (USpotLightComponent* Light = P->FindComponentByClass<USpotLightComponent>())
			{
				Light->SetVisibility(true);
			}
		}
	}

	// --- Visible pieces ---

	/** A visible static mesh piece (a component, or one instance of an instanced one) with its world box and transform. */
	struct FPiece
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		int32 Instance = INDEX_NONE;
		FTransform Transform;
		FBox Box;
		FString Describe() const
		{
			const UStaticMeshComponent* C = Component.Get();
			const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
			return FString::Printf(TEXT("%s%s (%s of %s) at %s"), *GetNameSafe(Mesh), Instance != INDEX_NONE ? *FString::Printf(TEXT("[%d]"), Instance) : TEXT(""),
				*GetNameSafe(C), C ? *GetNameSafe(C->GetOwner()) : TEXT("-"), *Box.GetCenter().ToCompactString());
		}
	};

	static bool IsShown(const UStaticMeshComponent* C)
	{
		const AActor* Owner = C ? C->GetOwner() : nullptr;
		return C && Owner && C->IsRegistered() && C->GetStaticMesh() && C->IsVisible() && !C->bHiddenInGame && C->bRenderInMainPass && !Owner->IsHidden();
	}

	/** Every visible static mesh piece of the world. */
	static void GatherPieces(UWorld* World, TArray<FPiece>& Out)
	{
		Out.Reset();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UStaticMeshComponent*> Components;
			It->GetComponents(Components);
			for (UStaticMeshComponent* C : Components)
			{
				if (!IsShown(C))
				{
					continue;
				}
				const FBox Local = C->GetStaticMesh()->GetBoundingBox();
				if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(C))
				{
					for (int32 I = 0; I < ISM->GetInstanceCount(); ++I)
					{
						FPiece& P = Out.AddDefaulted_GetRef();
						P.Component = C;
						P.Instance = I;
						ISM->GetInstanceTransform(I, P.Transform, true);
						P.Box = Local.TransformBy(P.Transform);
					}
				}
				else
				{
					FPiece& P = Out.AddDefaulted_GetRef();
					P.Component = C;
					P.Transform = C->GetComponentTransform();
					P.Box = Local.TransformBy(P.Transform);
				}
			}
		}
	}

	struct FTri
	{
		FVector A, B, C, N;
		double D = 0.0;
	};

	static TMap<const UStaticMesh*, TPair<TArray<FVector>, TArray<uint32>>> GMeshCache;

	static void WorldTris(const FPiece& P, TArray<FTri>& Out)
	{
		Out.Reset();
		const UStaticMeshComponent* C = P.Component.Get();
		const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
		if (!Mesh)
		{
			return;
		}
		TPair<TArray<FVector>, TArray<uint32>>* Cached = GMeshCache.Find(Mesh);
		if (!Cached)
		{
			Cached = &GMeshCache.Add(Mesh);
			FEntityFigureShape::ReadMeshTriangles(Mesh, Cached->Key, Cached->Value);
		}
		const TArray<FVector>& V = Cached->Key;
		const TArray<uint32>& I = Cached->Value;
		Out.Reserve(I.Num() / 3);
		for (int32 K = 0; K + 2 < I.Num(); K += 3)
		{
			FTri T;
			T.A = P.Transform.TransformPosition(V[I[K]]);
			T.B = P.Transform.TransformPosition(V[I[K + 1]]);
			T.C = P.Transform.TransformPosition(V[I[K + 2]]);
			const FVector Cross = FVector::CrossProduct(T.B - T.A, T.C - T.A);
			const double Area2 = Cross.Size();
			if (Area2 < 0.02)
			{
				continue;
			}
			// The engine's front faces wind the other way: the face's outward normal is the opposite of this cross product.
			T.N = -Cross / Area2;
			T.D = FVector::DotProduct(T.N, T.A);
			Out.Add(T);
		}
	}

	/**
	 * In an area the samples test the rendered floor (follow-up of Brief 35): a sample is covered only where a ray straight
	 * down meets a face turned up of a shown piece's own triangles within 3 cm of the floor's height; never by a piece's box.
	 */
	static bool bRenderedFloorOnly = false;
	/** When set, IsFloorCovered counts here the piece (mesh and component) that covered each sample. */
	static TMap<FString, int32>* GHitLog = nullptr;

	/** A ray straight down from 50 cm above Point meets an upward face of P within 3 cm of Point's height (turned up only, when bUpOnly). */
	static bool RayMeetsFloor(const FPiece& P, const FVector& Point, bool bUpOnly = false)
	{
		TArray<FTri> Tris;
		WorldTris(P, Tris);
		const FVector Origin = Point + FVector(0.0, 0.0, 50.0);
		const FVector Dir(0.0, 0.0, -1.0);
		for (const FTri& T : Tris)
		{
			if (FMath::Abs(T.N.Z) < 0.5 || (bUpOnly && T.N.Z < 0.5))
			{
				continue;
			}
			const FVector E1 = T.B - T.A;
			const FVector E2 = T.C - T.A;
			const FVector H = FVector::CrossProduct(Dir, E2);
			const double Det = FVector::DotProduct(E1, H);
			if (FMath::Abs(Det) < 1e-9)
			{
				continue;
			}
			const double F = 1.0 / Det;
			const FVector S = Origin - T.A;
			const double U = F * FVector::DotProduct(S, H);
			if (U < -1e-4 || U > 1.0 + 1e-4)
			{
				continue;
			}
			const FVector Q = FVector::CrossProduct(S, E1);
			const double V = F * FVector::DotProduct(Dir, Q);
			if (V < -1e-4 || U + V > 1.0 + 1e-4)
			{
				continue;
			}
			const double Dist = F * FVector::DotProduct(E2, Q);
			if (Dist >= 47.0 && Dist <= 53.0)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * A shown floor covers Point: a piece at most 60 cm thick inside whose box Point lies in XY, its top within 3 cm of Point's
	 * height; or a thicker piece (a vehicle's deck: the ship) whose own upward faces a ray straight down meets within 3 cm of it.
	 */
	static bool IsFloorCovered(const TArray<FPiece>& Pieces, const FVector& Point, const AActor* Ignore)
	{
		for (const FPiece& P : Pieces)
		{
			if (Point.X < P.Box.Min.X - 0.5 || Point.X > P.Box.Max.X + 0.5 || Point.Y < P.Box.Min.Y - 0.5 || Point.Y > P.Box.Max.Y + 0.5)
			{
				continue;
			}
			const UStaticMeshComponent* Owned = P.Component.Get();
			if (Ignore && Owned && Owned->GetOwner() == Ignore)
			{
				continue;
			}
			if (bRenderedFloorOnly)
			{
				if (Point.Z >= P.Box.Min.Z - 3.0 && Point.Z <= P.Box.Max.Z + 3.0 && RayMeetsFloor(P, Point, true))
				{
					if (GHitLog)
					{
						GHitLog->FindOrAdd(FString::Printf(TEXT("%s (%s)"), *GetNameSafe(Owned ? Owned->GetStaticMesh() : nullptr), *GetNameSafe(Owned))) += 1;
					}
					return true;
				}
				continue;
			}
			if (P.Box.Max.Z - P.Box.Min.Z > 60.0)
			{
				if (Point.Z >= P.Box.Min.Z - 3.0 && Point.Z <= P.Box.Max.Z + 3.0 && RayMeetsFloor(P, Point))
				{
					return true;
				}
				continue;
			}
			if (FMath::Abs(P.Box.Max.Z - Point.Z) > 3.0)
			{
				continue;
			}
			if (Point.X < P.Box.Min.X - 0.5 || Point.X > P.Box.Max.X + 0.5 || Point.Y < P.Box.Min.Y - 0.5 || Point.Y > P.Box.Max.Y + 0.5)
			{
				continue;
			}
			const UStaticMeshComponent* C = P.Component.Get();
			if (Ignore && C && C->GetOwner() == Ignore)
			{
				continue;
			}
			return true;
		}
		return false;
	}

	// --- Openings ---

	struct FOpening
	{
		FString Label;
		TWeakObjectPtr<AActor> Actor;
		/** The opening's frame: origin at its bottom centre, X through it, Y across it. */
		FTransform Frame;
		double Width = 0.0;
		double Depth = 0.0;
		double Height = 0.0;
		/** A hangar's hatch: the void beyond it is never reachable (Part B). */
		bool bHangarHatch = false;
		/** A panel that slides down into its own sill (the ship's hatch): sampled through its own thickness only. */
		bool bSlidesDown = false;
		/** The panel's travel, closed to open (world box; invalid for an opening whose panel is not moved by this check). */
		FBox Travel = FBox(ForceInit);
	};

	static void GatherOpenings(UWorld* World, TArray<FOpening>& Out)
	{
		Out.Reset();
		for (TActorIterator<ADoorDevice> It(World); It; ++It)
		{
			FOpening& O = Out.AddDefaulted_GetRef();
			O.Label = It->GetName();
			O.Actor = *It;
			O.Frame = FTransform(It->GetActorRotation(), It->GetActorLocation());
			const FVector Size = It->GetDoorSize();
			O.Depth = Size.X;
			O.Width = Size.Y;
			O.Height = Size.Z;
		}
		for (TActorIterator<APrologueSlidingPanel> It(World); It; ++It)
		{
			FOpening& O = Out.AddDefaulted_GetRef();
			O.Label = It->GetName();
			O.Actor = *It;
			O.Frame = FTransform(It->GetActorRotation(), It->GetActorLocation());
			O.Depth = It->OpeningSize.X;
			O.Width = It->OpeningSize.Y;
			O.Height = It->OpeningSize.Z;
			O.bHangarHatch = It->bBlockWhenOpen && It->OpeningSize.Y >= 1000.f;
			O.bSlidesDown = It->Slide == EProloguePanelSlide::Down;
		}
		for (TActorIterator<APrologueHatch> It(World); It; ++It)
		{
			FOpening& O = Out.AddDefaulted_GetRef();
			O.Label = It->GetName();
			O.Actor = *It;
			O.Frame = FTransform(It->GetActorRotation(), It->GetActorLocation());
			O.Depth = It->OpeningSize.X;
			O.Width = It->OpeningSize.Y;
			O.Height = It->OpeningSize.Z;
			O.bHangarHatch = true;
		}
		Out.Sort([](const FOpening& A, const FOpening& B) { return A.Label < B.Label; });
	}

	/**
	 * The sign of the opening's local X on its inside (-1 or +1): the side whose floor runs farther from it (a hangar's floor
	 * runs tens of metres, the floor laid past its hatch two; a doorway's two rooms both have floor, and either side will do).
	 */
	static double InsideSign(const TArray<FPiece>& Pieces, const FOpening& O)
	{
		const AActor* Ignore = O.Actor.Get();
		auto Reach = [&](double Sign)
		{
			double Total = 0.0;
			for (const double Y : { -0.25 * O.Width, 0.0, 0.25 * O.Width })
			{
				double X = 0.5 * O.Depth;
				while (X < 3000.0 && IsFloorCovered(Pieces, O.Frame.TransformPosition(FVector(Sign * (X + 10.0), Y, 0.0)), Ignore))
				{
					X += 10.0;
				}
				Total += X;
			}
			return Total;
		};
		return Reach(-1.0) >= Reach(1.0) ? -1.0 : 1.0;
	}

	/** Opens (Open) or closes an opening's door or panel at once; returns the state to put back (1 open). */
	static void SetOpening(const FOpening& O, bool bOpen)
	{
		AActor* A = O.Actor.Get();
		if (ADoorDevice* Door = Cast<ADoorDevice>(A))
		{
			if (bOpen) { Door->PowerOn(); } else { Door->PowerOff(); }
		}
		else if (APrologueSlidingPanel* Panel = Cast<APrologueSlidingPanel>(A))
		{
			Panel->SetOpenAtOnce(bOpen ? 1.f : 0.f);
		}
	}

	static bool IsOpen(const FOpening& O)
	{
		const AActor* A = O.Actor.Get();
		if (const ADoorDevice* Door = Cast<ADoorDevice>(A)) { return Door->IsOpen(); }
		if (const APrologueSlidingPanel* Panel = Cast<APrologueSlidingPanel>(A)) { return Panel->GetOpenFraction() >= 1.f; }
		if (const APrologueHatch* Hatch = Cast<APrologueHatch>(A)) { return Hatch->GetClosedFraction() <= 0.f; }
		return true;
	}

	/** The world boxes of an opening's own shown pieces (its panel). */
	static FBox OwnBox(const FOpening& O)
	{
		FBox Box(ForceInit);
		if (const AActor* A = O.Actor.Get())
		{
			TArray<UStaticMeshComponent*> Components;
			A->GetComponents(Components);
			for (const UStaticMeshComponent* C : Components)
			{
				if (IsShown(C))
				{
					Box += C->Bounds.GetBox();
				}
			}
		}
		return Box;
	}

	// --- Part A ---

	/**
	 * Point's floor: covered as IsFloorCovered says, or, inside the footprint of the opening's panel travel, by a floor piece
	 * whose top stands between the travel's lowest point and the opening's floor (the floor stands under the travel, never in
	 * it: under a panel that sinks into its slot when it closes, the slot's floor is the travel's lowest point).
	 */
	static bool IsOpeningFloorCovered(const TArray<FPiece>& Pieces, const FOpening& O, const FVector& Point)
	{
		const AActor* Ignore = O.Actor.Get();
		if (IsFloorCovered(Pieces, Point, Ignore))
		{
			return true;
		}
		if (!O.Travel.IsValid || O.Travel.Min.Z >= Point.Z - 0.5 || Point.X < O.Travel.Min.X - 0.5 || Point.X > O.Travel.Max.X + 0.5 || Point.Y < O.Travel.Min.Y - 0.5
			|| Point.Y > O.Travel.Max.Y + 0.5)
		{
			return false;
		}
		for (const FPiece& P : Pieces)
		{
			const UStaticMeshComponent* C = P.Component.Get();
			if (C && C->GetOwner() != Ignore && P.Box.Max.Z - P.Box.Min.Z <= 60.0 && P.Box.Max.Z >= O.Travel.Min.Z - 3.0 && P.Box.Max.Z <= Point.Z + 3.0
				&& Point.X >= P.Box.Min.X - 0.5 && Point.X <= P.Box.Max.X + 0.5 && Point.Y >= P.Box.Min.Y - 0.5 && Point.Y <= P.Box.Max.Y + 0.5)
			{
				return true;
			}
		}
		return false;
	}

	/** The static mesh pieces under an opening's middle that the samples leave out (not shown), with their flags. */
	static void NoteUnshownUnder(UWorld* World, const FOpening& O)
	{
		const FVector At = O.Frame.GetLocation();
		FString List;
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UStaticMeshComponent*> Components;
			It->GetComponents(Components);
			for (const UStaticMeshComponent* C : Components)
			{
				if (!C || !C->GetStaticMesh() || IsShown(C) || C->GetOwner() == O.Actor.Get())
				{
					continue;
				}
				const FBox Local = C->GetStaticMesh()->GetBoundingBox();
				auto Under = [&](const FTransform& T)
				{
					const FBox Box = Local.TransformBy(T);
					return At.X >= Box.Min.X && At.X <= Box.Max.X && At.Y >= Box.Min.Y && At.Y <= Box.Max.Y && At.Z >= Box.Min.Z - 1.0 && At.Z <= Box.Max.Z + 1.0;
				};
				int32 Instances = 0;
				if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(C))
				{
					for (int32 I = 0; I < ISM->GetInstanceCount(); ++I)
					{
						FTransform T;
						ISM->GetInstanceTransform(I, T, true);
						Instances += Under(T) ? 1 : 0;
					}
				}
				else
				{
					Instances = Under(C->GetComponentTransform()) ? 1 : 0;
				}
				if (Instances > 0)
				{
					++Count;
					List += FString::Printf(TEXT("\n      %s (%s of %s), %d instance(s): visible %d, hidden in game %d, main pass %d, depth pass %d, collision %d"), *GetNameSafe(C->GetStaticMesh()),
						*GetNameSafe(C), *GetNameSafe(C->GetOwner()), Instances, C->IsVisible() ? 1 : 0, C->bHiddenInGame ? 1 : 0, C->bRenderInMainPass ? 1 : 0, C->bRenderInDepthPass ? 1 : 0,
						static_cast<int32>(C->GetCollisionEnabled()));
				}
			}
		}
		Note(FString::Printf(TEXT("A: %s: %d unshown piece(s) under its middle, left out of the samples:%s"), *O.Label, Count, *List));
	}

	/** The shown pieces lying at the opening's floor (top within 3 cm, at most 60 cm thick) within 7 m of it, in its frame. */
	static void NoteFloorAround(const TArray<FPiece>& Pieces, const FOpening& O)
	{
		const double Floor = O.Frame.GetLocation().Z;
		struct FRow { FBox Local; FString What; };
		TArray<FRow> Rows;
		for (const FPiece& P : Pieces)
		{
			if (P.Box.Max.Z - P.Box.Min.Z > 60.0 || FMath::Abs(P.Box.Max.Z - Floor) > 3.0)
			{
				continue;
			}
			const FBox Local = P.Box.InverseTransformBy(O.Frame);
			if (Local.Max.X < -700.0 || Local.Min.X > 700.0 || Local.Max.Y < -0.5 * O.Width - 100.0 || Local.Min.Y > 0.5 * O.Width + 100.0)
			{
				continue;
			}
			const UStaticMeshComponent* C = P.Component.Get();
			FVector Scale = P.Transform.GetScale3D();
			Rows.Add({ Local, FString::Printf(TEXT("%s%s (%s) scale %.3f x %.3f x %.3f, yaw %.1f"), *GetNameSafe(C ? C->GetStaticMesh() : nullptr),
				P.Instance != INDEX_NONE ? *FString::Printf(TEXT("[%d]"), P.Instance) : TEXT(""), *GetNameSafe(C), Scale.X, Scale.Y, Scale.Z, P.Transform.Rotator().Yaw) });
		}
		Rows.Sort([](const FRow& A, const FRow& B) { return A.Local.Min.X != B.Local.Min.X ? A.Local.Min.X < B.Local.Min.X : A.Local.Min.Y < B.Local.Min.Y; });
		FString List;
		for (const FRow& Row : Rows)
		{
			List += FString::Printf(TEXT("\n      X %.1f to %.1f, Y %.1f to %.1f, top %.1f, bottom %.1f: %s"), Row.Local.Min.X, Row.Local.Max.X, Row.Local.Min.Y, Row.Local.Max.Y,
				Row.Local.Max.Z, Row.Local.Min.Z, *Row.What);
		}
		Note(FString::Printf(TEXT("A: %s: %d shown floor piece(s) within 7 m, in its frame (X through, Y across, cm; boxes of their meshes' bounds):%s"), *O.Label, Rows.Num(), *List));
	}

	static void CheckFloorUnder(const TArray<FPiece>& Pieces, const FOpening& O)
	{
		const AActor* Ignore = O.Actor.Get();
		// Through the depth: the opening's own thickness and 10 cm beyond each of its faces (its threshold under the frame);
		// a panel that slides down into its own sill (the ship's hatch) through its own thickness only.
		const double Half = O.bSlidesDown ? 0.5 * O.Depth : 0.5 * O.Depth + 10.0;
		int32 Samples = 0, Missed = 0;
		FBox2D MissedAt(ForceInit);
		TMap<FString, int32> Hits;
		GHitLog = bRenderedFloorOnly ? &Hits : nullptr;
		ON_SCOPE_EXIT { GHitLog = nullptr; };
		for (double X = -Half; X <= Half + 0.01; X += 10.0)
		{
			for (double Y = -0.5 * O.Width + 10.0; Y <= 0.5 * O.Width - 10.0 + 0.01; Y += 20.0)
			{
				++Samples;
				if (!IsOpeningFloorCovered(Pieces, O, O.Frame.TransformPosition(FVector(X, Y, 0.0))))
				{
					++Missed;
					MissedAt += FVector2D(X, Y);
				}
			}
		}
		if (bRenderedFloorOnly)
		{
			FString HitList;
			for (const TPair<FString, int32>& Hit : Hits)
			{
				HitList += FString::Printf(TEXT("\n      %s: %d"), *Hit.Key, Hit.Value);
			}
			Note(FString::Printf(TEXT("A: %s: the samples' rays straight down met these rendered pieces' own faces turned up (no box counted):%s"), *O.Label, *HitList));
		}
		Check(Missed == 0, FString::Printf(TEXT("A: %s (%.0f cm wide, %.0f deep, floor at %.1f, sampled every 20 cm across and 10 cm through %.0f cm): %d of %d samples meet no shown floor piece%s"), *O.Label,
			O.Width, O.Depth, O.Frame.GetLocation().Z, 2.0 * Half, Missed, Samples, Missed == 0 ? TEXT("") : *FString::Printf(TEXT(", along the opening from %.0f to %.0f cm and across it from %.0f to %.0f cm (opening's frame)"),
				MissedAt.Min.X, MissedAt.Max.X, MissedAt.Min.Y, MissedAt.Max.Y)));
		if (Missed > 0)
		{
			// Where the floor stops on each side: the floor pieces at the opening's floor within 60 cm of it.
			const FBox Near = FBox(O.Frame.TransformPosition(FVector(-Half - 60.0, -0.5 * O.Width - 60.0, -5.0)), O.Frame.TransformPosition(FVector(Half + 60.0, 0.5 * O.Width + 60.0, 5.0)));
			const FBox NearAbs(Near.Min.ComponentMin(Near.Max), Near.Min.ComponentMax(Near.Max));
			FString Around;
			for (const FPiece& P : Pieces)
			{
				const UStaticMeshComponent* C = P.Component.Get();
				if (C && C->GetOwner() != Ignore && P.Box.Max.Z - P.Box.Min.Z <= 60.0 && FMath::Abs(P.Box.Max.Z - O.Frame.GetLocation().Z) <= 3.0 && P.Box.Intersect(NearAbs))
				{
					Around += FString::Printf(TEXT("\n      floor beside it: %s, box X %.1f..%.1f Y %.1f..%.1f top %.1f"), *P.Describe(), P.Box.Min.X, P.Box.Max.X, P.Box.Min.Y, P.Box.Max.Y, P.Box.Max.Z);
				}
			}
			Note(FString::Printf(TEXT("A: %s: the opening's frame at %s yaw %.1f%s"), *O.Label, *O.Frame.GetLocation().ToCompactString(), O.Frame.Rotator().Yaw, *Around));
		}
	}

	/** The door's travel (closed and open) holds no floor piece: the floor stands under it, never in it. */
	static void CheckTravel(const TArray<FPiece>& Pieces, FOpening& O)
	{
		AActor* A = O.Actor.Get();
		if (!A || Cast<APrologueHatch>(A))
		{
			return;
		}
		const bool bWasOpen = IsOpen(O);
		SetOpening(O, false);
		FBox Swept = OwnBox(O);
		SetOpening(O, true);
		Swept += OwnBox(O);
		SetOpening(O, bWasOpen);
		if (!Swept.IsValid)
		{
			return;
		}
		O.Travel = Swept;
		FString In;
		int32 Count = 0;
		for (const FPiece& P : Pieces)
		{
			const UStaticMeshComponent* C = P.Component.Get();
			const bool bFill = C && (C->ComponentHasTag(TEXT("SeamFill")) || (C->GetOwner() && C->GetOwner()->ActorHasTag(TEXT("SeamFill"))));
			if (!C || !bFill || C->GetOwner() == A || !P.Box.Intersect(Swept))
			{
				continue;
			}
			const FBox Over = P.Box.Overlap(Swept);
			if (FMath::Min3(Over.GetSize().X, Over.GetSize().Y, Over.GetSize().Z) > 0.5)
			{
				++Count;
				In += FString::Printf(TEXT("%s%s"), In.IsEmpty() ? TEXT("") : TEXT("; "), *P.Describe());
			}
		}
		Check(Count == 0, FString::Printf(TEXT("A: %s: its panel's travel, closed to open (%s), holds none of the floor pieces added under the openings (tag SeamFill) (%d%s)"), *O.Label, *Swept.ToString(), Count,
			In.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s"), *In)));
	}

	// --- Part B ---

	static void CheckHatch(UWorld* World, const TArray<FPiece>& Pieces, const FOpening& O)
	{
		const AActor* A = O.Actor.Get();
		const double S = InsideSign(Pieces, O);
		// The hatch's panel (open) and the frames around the opening: their farthest point outward, in the opening's frame.
		double Outer = 0.5 * O.Depth;
		FString OuterOf = TEXT("the opening");
		const FBox Around(O.Frame.TransformPosition(FVector(-400.0, -0.5 * O.Width - 300.0, -50.0)), O.Frame.TransformPosition(FVector(400.0, 0.5 * O.Width + 300.0, O.Height + 300.0)));
		const FBox Near(Around.Min.ComponentMin(Around.Max), Around.Min.ComponentMax(Around.Max));
		for (const FPiece& P : Pieces)
		{
			const UStaticMeshComponent* C = P.Component.Get();
			const bool bOwn = C && C->GetOwner() == A;
			const FString MeshName = GetNameSafe(C ? C->GetStaticMesh() : nullptr);
			const bool bFrame = MeshName == TEXT("SM_HatchFrame") || MeshName == TEXT("SM_HangarHatchFrame") || MeshName == TEXT("SM_KitDoorFrame") || MeshName == TEXT("SM_KitGateFrame");
			if (!C || !(bOwn || bFrame) || !P.Box.Intersect(Near))
			{
				continue;
			}
			for (int32 I = 0; I < 8; ++I)
			{
				const FVector Corner((I & 1) ? P.Box.Max.X : P.Box.Min.X, (I & 2) ? P.Box.Max.Y : P.Box.Min.Y, (I & 4) ? P.Box.Max.Z : P.Box.Min.Z);
				const FVector Local = O.Frame.InverseTransformPosition(Corner);
				if (FMath::Abs(Local.Y) <= 0.5 * O.Width + 200.0 && S * Local.X * -1.0 > Outer)
				{
					Outer = S * Local.X * -1.0;
					OuterOf = P.Describe();
				}
			}
		}
		// The floor's reach outward, all across the opening.
		double Reach = TNumericLimits<double>::Max();
		double ReachAt = 0.0;
		for (double Y = -0.5 * O.Width + 20.0; Y <= 0.5 * O.Width - 20.0 + 0.01; Y += 100.0)
		{
			double X = 0.0;
			while (X < 3000.0 && IsOpeningFloorCovered(Pieces, O, O.Frame.TransformPosition(FVector(-S * (X + 5.0), Y, 0.0))))
			{
				X += 5.0;
			}
			if (X < Reach)
			{
				Reach = X;
				ReachAt = Y;
			}
		}
		Check(Reach >= Outer + 20.0, FString::Printf(TEXT("B: %s: the floor runs %.0f cm out past the opening's middle all across it (least at %.0f cm across), past the hatch's panel and frame, which reach %.0f cm out (%s), by %.0f cm (20 needed)"),
			*O.Label, Reach, ReachAt, Outer, *OuterOf, Reach - Outer));
		// Still no way out: a capsule swept from 1.5 m inside to 3 m outside, through the middle of the opening.
		const UCapsuleComponent* Capsule = Pawn() ? Pawn()->GetCapsuleComponent() : nullptr;
		const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f;
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsSeamHatch), false, Pawn());
		FHitResult Hit;
		const FVector From = O.Frame.TransformPosition(FVector(S * 150.0, 0.0, HalfHeight + 5.0));
		const FVector To = O.Frame.TransformPosition(FVector(-S * 300.0, 0.0, HalfHeight + 5.0));
		const bool bBlocked = World->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params);
		Check(bBlocked, FString::Printf(TEXT("B: %s: the player still cannot walk out (a capsule from 1.5 m inside to 3 m outside stops on %s)"), *O.Label,
			bBlocked ? *GetNameSafe(Hit.GetComponent()) : TEXT("nothing")));
	}

	// --- Part C: coplanar faces ---

	/** Area of the overlap of two triangles in the plane with normal N (2D, after projection), by clipping one by the other. */
	static double OverlapArea(const FTri& X, const FTri& Y, const FVector& N)
	{
		FVector U, V;
		N.FindBestAxisVectors(U, V);
		auto To2 = [&](const FVector& P) { return FVector2D(FVector::DotProduct(P, U), FVector::DotProduct(P, V)); };
		TArray<FVector2D> Poly = { To2(X.A), To2(X.B), To2(X.C) };
		FVector2D Clip[3] = { To2(Y.A), To2(Y.B), To2(Y.C) };
		// The clipping triangle counter-clockwise.
		if (FVector2D::CrossProduct(Clip[1] - Clip[0], Clip[2] - Clip[0]) < 0.0)
		{
			Swap(Clip[1], Clip[2]);
		}
		for (int32 E = 0; E < 3 && Poly.Num() > 0; ++E)
		{
			const FVector2D P0 = Clip[E];
			const FVector2D P1 = Clip[(E + 1) % 3];
			auto Inside = [&](const FVector2D& Q) { return FVector2D::CrossProduct(P1 - P0, Q - P0) >= 0.0; };
			TArray<FVector2D> In = MoveTemp(Poly);
			Poly.Reset();
			for (int32 K = 0; K < In.Num(); ++K)
			{
				const FVector2D Cur = In[K];
				const FVector2D Prev = In[(K + In.Num() - 1) % In.Num()];
				const bool bCur = Inside(Cur);
				const bool bPrev = Inside(Prev);
				if (bCur != bPrev)
				{
					const double A1 = FVector2D::CrossProduct(P1 - P0, Prev - P0);
					const double A2 = FVector2D::CrossProduct(P1 - P0, Cur - P0);
					const double T = A1 / (A1 - A2);
					Poly.Add(Prev + (Cur - Prev) * T);
				}
				if (bCur)
				{
					Poly.Add(Cur);
				}
			}
		}
		double Area = 0.0;
		for (int32 K = 0; K < Poly.Num(); ++K)
		{
			Area += FVector2D::CrossProduct(Poly[K], Poly[(K + 1) % Poly.Num()]);
		}
		return FMath::Abs(Area) * 0.5;
	}

	struct FCoplanar
	{
		/** Faces facing the same way that face down onto a shown floor's top (resting on it): no eye can see them. */
		double OnFloor = 0.0;
		double SameFacing = 0.0;
		double Opposite = 0.0;
		double Nearest = 0.0;
		/** The first face facing the same way: its normal and its centre. */
		FVector SameNormal = FVector::ZeroVector;
		FVector SameAt = FVector::ZeroVector;
	};

	static const TArray<FPiece>* GFloorPieces = nullptr;

	/**
	 * A face facing down at the level of a floor's top (within 1 cm of the top of a shown floor piece within 3 m of it): it faces
	 * the floor it stands on, or the underside of the map; no eye inside the map is ever below it.
	 */
	static bool RestsOnFloor(const FTri& T)
	{
		if (T.N.Z > -0.9 || !GFloorPieces)
		{
			return false;
		}
		const FVector C = (T.A + T.B + T.C) / 3.0;
		for (const FPiece& P : *GFloorPieces)
		{
			if (P.Box.Max.Z - P.Box.Min.Z <= 60.0 && FMath::Abs(P.Box.Max.Z - C.Z) <= 1.0 && C.X >= P.Box.Min.X - 300.0 && C.X <= P.Box.Max.X + 300.0
				&& C.Y >= P.Box.Min.Y - 300.0 && C.Y <= P.Box.Max.Y + 300.0)
			{
				return true;
			}
		}
		return false;
	}

	/** Overlap of Frame's faces with Other's faces lying within 1 cm of the same plane (normals within 1 degree). */
	static FCoplanar Coplanar(const TArray<FTri>& Frame, const TArray<FTri>& Other, bool bSelf)
	{
		FCoplanar Out;
		const double CosTol = FMath::Cos(FMath::DegreesToRadians(1.0));
		for (int32 I = 0; I < Frame.Num(); ++I)
		{
			const FTri& F = Frame[I];
			const FBox FB = FBox(TArray<FVector>{ F.A, F.B, F.C }).ExpandBy(1.0);
			for (int32 J = bSelf ? I + 1 : 0; J < Other.Num(); ++J)
			{
				const FTri& T = Other[J];
				const double Dot = FVector::DotProduct(F.N, T.N);
				if (FMath::Abs(Dot) < CosTol)
				{
					continue;
				}
				const double DA = FMath::Abs(FVector::DotProduct(F.N, T.A) - F.D);
				const double DB = FMath::Abs(FVector::DotProduct(F.N, T.B) - F.D);
				const double DC = FMath::Abs(FVector::DotProduct(F.N, T.C) - F.D);
				if (FMath::Max3(DA, DB, DC) >= 1.0 || !FB.Intersect(FBox(TArray<FVector>{ T.A, T.B, T.C })))
				{
					continue;
				}
				const double Area = OverlapArea(F, T, F.N);
				if (Area < 1.0)
				{
					continue;
				}
				if (Dot > 0.0 && RestsOnFloor(F))
				{
					Out.OnFloor += Area;
					continue;
				}
				(Dot > 0.0 ? Out.SameFacing : Out.Opposite) += Area;
				if (Dot > 0.0 && Out.SameNormal.IsZero())
				{
					Out.SameNormal = F.N;
					Out.SameAt = (F.A + F.B + F.C) / 3.0;
				}
				Out.Nearest = FMath::Max(Out.Nearest, FMath::Max3(DA, DB, DC));
			}
		}
		return Out;
	}

	struct FFrameRef
	{
		FPiece Piece;
		FString Label;
	};

	/** The frames, for the void's classification (a copy of the check's frames, set at setup). */
	static TArray<FFrameRef> GFramesForVoid;

	static void GatherFrames(const TArray<FPiece>& Pieces, TArray<FFrameRef>& Out)
	{
		Out.Reset();
		for (const FPiece& P : Pieces)
		{
			const UStaticMeshComponent* C = P.Component.Get();
			const FString Mesh = GetNameSafe(C ? C->GetStaticMesh() : nullptr);
			// The frames of doors, gates and hatches (the kit's door and gate frames, the hangars' hatch frame).
			if (C && (C->ComponentHasTag(TEXT("AreaDressGateFrames")) || Mesh == TEXT("SM_KitDoorFrame") || Mesh == TEXT("SM_KitGateFrame") || Mesh == TEXT("SM_HatchFrame")
				|| Mesh == TEXT("SM_HangarHatchFrame")))
			{
				FFrameRef& F = Out.AddDefaulted_GetRef();
				F.Piece = P;
				F.Label = P.Describe();
			}
		}
	}

	static int32 CheckFrames(const TArray<FPiece>& Pieces, const TArray<FFrameRef>& Frames)
	{
		GFloorPieces = &Pieces;
		int32 Pairs = 0;
		TArray<FTri> FrameTris, OtherTris;
		for (const FFrameRef& F : Frames)
		{
			WorldTris(F.Piece, FrameTris);
			FString Found;
			int32 FramePairs = 0;
			const FCoplanar Self = Coplanar(FrameTris, FrameTris, true);
			// Faces of two pieces that touch back to back (one turned away from any eye, the material one-sided) cannot show
			// against each other: they are listed, not counted. Faces facing the same way within 1 cm of one plane are counted.
			FString Contacts;
			if (Self.SameFacing >= 1.0)
			{
				++FramePairs;
				Found += FString::Printf(TEXT("\n      its own faces doubled, facing the same way: %.0f cm2 within %.2f cm, normal %s at %s"), Self.SameFacing, Self.Nearest,
					*Self.SameNormal.ToCompactString(), *Self.SameAt.ToCompactString());
			}
			if (Self.Opposite >= 1.0)
			{
				Contacts += FString::Printf(TEXT("\n      (contact) its own faces back to back: %.0f cm2"), Self.Opposite);
			}
			const FBox Grown = F.Piece.Box.ExpandBy(2.0);
			for (const FPiece& P : Pieces)
			{
				if (P.Component == F.Piece.Component && P.Instance == F.Piece.Instance)
				{
					continue;
				}
				if (!P.Box.Intersect(Grown))
				{
					continue;
				}
				WorldTris(P, OtherTris);
				const FCoplanar Pair = Coplanar(FrameTris, OtherTris, false);
				if (Pair.SameFacing >= 1.0)
				{
					++FramePairs;
					Found += FString::Printf(TEXT("\n      with %s: %.0f cm2 facing the same way within %.2f cm, normal %s at %s"), *P.Describe(), Pair.SameFacing, Pair.Nearest,
						*Pair.SameNormal.ToCompactString(), *Pair.SameAt.ToCompactString());
				}
				if (Pair.Opposite >= 1.0)
				{
					Contacts += FString::Printf(TEXT("\n      (contact) with %s: %.0f cm2 back to back"), *P.Describe(), Pair.Opposite);
				}
				if (Pair.OnFloor >= 1.0)
				{
					Contacts += FString::Printf(TEXT("\n      (on the floor) with %s: %.0f cm2 of both facing down onto a floor's top, unseen"), *P.Describe(), Pair.OnFloor);
				}
			}
			Pairs += FramePairs;
			Check(FramePairs == 0, FString::Printf(TEXT("C: frame %s: no face within 1 cm of the plane of a face facing the same way that it overlaps (%d pieces with such faces)%s"), *F.Label, FramePairs, *Found));
			if (!Contacts.IsEmpty())
			{
				Note(FString::Printf(TEXT("C: frame %s: faces that cannot show against each other (touching back to back, or both facing down onto a floor's top):%s"), *F.Label, *Contacts));
			}
		}
		return Pairs;
	}

	// --- Shots ---

	struct FShot
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
	};
	static FShot GShot;
	static bool bShotReady = false;
	static FString GShotSave;
	static FDelegateHandle GShotHandle;

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		GShot.Width = Width;
		GShot.Height = Height;
		GShot.Pixels = Pixels;
		if (!GShotSave.IsEmpty())
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief35"), GShotSave + TEXT(".png")));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Png, *Path);
			Note(FString::Printf(TEXT("shot %s saved: %s"), *GShotSave, *Path));
		}
		bShotReady = true;
	}

	static void RequestShot(const FString& SaveAs)
	{
		if (!GShotHandle.IsValid())
		{
			GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
		}
		GShotSave = SaveAs;
		bShotReady = false;
		FScreenshotRequest::RequestScreenshot(FString(), false, false);
	}

	static void AddShot(TArray<FStep>& S, TFunction<FString()> Label, TFunction<void(const FShot&)> Then = nullptr)
	{
		S.Add(Do(TEXT("shot"), [Label]() { RequestShot(Label()); return 0.f; }));
		S.Add(WaitFor(TEXT("shot taken"), []() { return bShotReady; }, 5.f, [Then]() { if (Then) { Then(GShot); } return 0.f; }));
	}

	// --- Depth captures (follow-up of Brief 35: the void under the areas' gates, as rendered) ---

	/**
	 * Captures the scene's depth (along the view's axis, cm) from Eye along Rotation with a perspective of FovDeg across,
	 * Width x Height, the pawn left out. Empty when the capture fails. Nothing rendered reads as a very large depth.
	 */
	static bool CaptureDepth(const FVector& Eye, const FRotator& Rotation, float FovDeg, int32 Width, int32 Height, TArray<float>& Out)
	{
		Out.Reset();
		UWorld* World = W();
		ACharacter* P = Pawn();
		if (!World || !P)
		{
			return false;
		}
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->RenderTargetFormat = RTF_RGBA32f;
		Target->ClearColor = FLinearColor(1.0e9f, 0.f, 0.f, 0.f);
		Target->InitAutoFormat(Width, Height);
		Target->UpdateResourceImmediate(true);
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(P);
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->CaptureSource = SCS_SceneDepth;
		Capture->FOVAngle = FovDeg;
		Capture->TextureTarget = Target;
		Capture->HiddenActors.Add(P);
		Capture->RegisterComponentWithWorld(World);
		Capture->SetWorldLocationAndRotation(Eye, Rotation);
		Capture->CaptureScene();
		FlushRenderingCommands();
		TArray<FLinearColor> Pixels;
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		const bool bOk = Resource && Resource->ReadLinearColorPixels(Pixels) && Pixels.Num() == Width * Height;
		Capture->DestroyComponent();
		if (!bOk)
		{
			return false;
		}
		Out.SetNumUninitialized(Pixels.Num());
		for (int32 I = 0; I < Pixels.Num(); ++I)
		{
			Out[I] = Pixels[I].R;
		}
		return true;
	}

	/** The world direction of pixel (X, Y) of a perspective capture (FovDeg across) along Rotation. */
	static FVector PixelRay(const FRotator& Rotation, float FovDeg, int32 Width, int32 Height, int32 X, int32 Y)
	{
		const FRotationMatrix M(Rotation);
		const double Tan = FMath::Tan(FMath::DegreesToRadians(0.5 * FovDeg));
		const double U = (2.0 * (X + 0.5) / Width - 1.0) * Tan;
		const double V = (1.0 - 2.0 * (Y + 0.5) / Height) * Tan * Height / Width;
		return (M.GetUnitAxis(EAxis::X) + M.GetUnitAxis(EAxis::Y) * U + M.GetUnitAxis(EAxis::Z) * V).GetSafeNormal();
	}

	/**
	 * The void a depth capture shows: pixels that meet nothing, or meet something below the floor plates' underside (more than
	 * 11 cm below the opening's floor: the plates are 10 cm thick). A hit 5 to 11 cm below is the floor's own relief (the
	 * bevelled joints between the kit's plates): counted apart, not void.
	 */
	struct FVoidCount
	{
		int32 Pixels = 0;
		int32 Void = 0;
		TMap<FString, int32> Where;
		FBox2D Local = FBox2D(ForceInit);
		TArray<FColor> Mask;
		/** How far below the floor each void pixel's hit lies: within the floor's thickness (5-30 cm), 30 cm-2 m, deeper or nothing. */
		int32 Shallow = 0, Mid = 0, Deep = 0;
		/** Of the shallow ones: within the plates' own 10 cm (5-11 cm below), and the deepest hit of all (cm below the floor). */
		int32 InPlate = 0;
		double DeepestBelow = 0.0;
		/** Pixels on the floor's own relief (5 to 11 cm below its top: the joints between the plates), not void, and the deepest. */
		int32 Joint = 0;
		double DeepestJoint = 0.0;
		/** Void pixels by where their ray crosses the floor's level: across (Y, 10 cm bins) and through (X, 20 cm bins). */
		TMap<int32, int32> AcrossBins, ThroughBins;
	};

	/** The opening's gate frame's box in the opening's frame (local; invalid when no frame stands at it). */
	static FBox LocalFrameBox(const FOpening& O)
	{
		FBox Local(ForceInit);
		for (const FFrameRef& F : GFramesForVoid)
		{
			if (F.Piece.Box.IsInsideOrOnXY(O.Frame.GetLocation()))
			{
				Local += F.Piece.Box.InverseTransformBy(O.Frame);
			}
		}
		return Local;
	}

	/**
	 * Counts the void in a depth capture against the opening and says where each void pixel is, by where its ray crosses the
	 * opening's floor level (the opening's frame): under the door's travel (its panel's footprint; the gates' doors have no slot:
	 * they open by hiding their panel), at the frame's foot, at the sides between the frame and the wall, beyond; a ray that
	 * never comes down to the floor's level is above it. With OnlyFootprint, only the pixels whose ray crosses the floor inside
	 * the opening's footprint (the samples' area) are counted.
	 */
	static FVoidCount CountVoid(const FOpening& O, const FVector& Eye, const FRotator& Rotation, float FovDeg, int32 Width, int32 Height, const TArray<float>& Depth,
		bool bOnlyFootprint, double FootprintHalfDepth)
	{
		FVoidCount R;
		R.Mask.Init(FColor::Black, Width * Height);
		const double Floor = O.Frame.GetLocation().Z;
		const FVector Forward = Rotation.Vector();
		const FBox Frame = LocalFrameBox(O);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 I = Y * Width + X;
				const FVector Ray = PixelRay(Rotation, FovDeg, Width, Height, X, Y);
				bool bCrosses = false;
				FVector LocalAt = FVector::ZeroVector;
				if (Ray.Z < -1e-4 && Eye.Z > Floor)
				{
					const FVector Cross = Eye + Ray * ((Eye.Z - Floor) / -Ray.Z);
					LocalAt = O.Frame.InverseTransformPosition(Cross);
					bCrosses = true;
				}
				const bool bInFootprint = bCrosses && FMath::Abs(LocalAt.X) <= FootprintHalfDepth && FMath::Abs(LocalAt.Y) <= 0.5 * O.Width;
				if (bOnlyFootprint && !bInFootprint)
				{
					continue;
				}
				++R.Pixels;
				const double D = Depth[I];
				const double Along = FMath::Max(FVector::DotProduct(Ray, Forward), 1e-3);
				const bool bNothing = D > 1.0e7;
				const double HitZ = bNothing ? -1.0e9 : (Eye + Ray * (D / Along)).Z;
				const bool bVoid = bNothing || HitZ < Floor - 11.0;
				const bool bJoint = !bVoid && HitZ < Floor - 5.0;
				const uint8 Grey = bNothing ? 0 : static_cast<uint8>(FMath::Clamp(255.0 - D / 10.0, 30.0, 255.0));
				R.Mask[I] = bVoid ? FColor(255, 0, 0) : bJoint ? FColor(255, 160, 0) : FColor(Grey, Grey, Grey);
				if (bJoint)
				{
					++R.Joint;
					R.DeepestJoint = FMath::Max(R.DeepestJoint, Floor - HitZ);
				}
				if (!bVoid)
				{
					continue;
				}
				++R.Void;
				const double Below = Floor - HitZ;
				(bNothing || Below > 200.0 ? R.Deep : Below > 30.0 ? R.Mid : R.Shallow) += 1;
				R.InPlate += !bNothing && Below <= 11.0 ? 1 : 0;
				R.DeepestBelow = FMath::Max(R.DeepestBelow, bNothing ? 1.0e9 : Below);
				if (bCrosses)
				{
					R.AcrossBins.FindOrAdd(FMath::FloorToInt(LocalAt.Y / 10.0)) += 1;
					R.ThroughBins.FindOrAdd(FMath::FloorToInt(LocalAt.X / 20.0)) += 1;
				}
				FString Where;
				if (!bCrosses)
				{
					Where = TEXT("above the floor's level (nothing rendered there)");
				}
				else
				{
					R.Local += FVector2D(LocalAt.X, LocalAt.Y);
					const bool bInTravel = FMath::Abs(LocalAt.X) <= 0.5 * O.Depth && FMath::Abs(LocalAt.Y) <= 0.5 * O.Width;
					const bool bInFrame = Frame.IsValid && LocalAt.X >= Frame.Min.X && LocalAt.X <= Frame.Max.X && LocalAt.Y >= Frame.Min.Y && LocalAt.Y <= Frame.Max.Y;
					const double FrameHalf = Frame.IsValid ? FMath::Max(FMath::Abs(Frame.Min.Y), FMath::Abs(Frame.Max.Y)) : 0.5 * O.Width;
					const double FrameDepth = Frame.IsValid ? FMath::Max(FMath::Abs(Frame.Min.X), FMath::Abs(Frame.Max.X)) : 0.5 * O.Depth;
					if (bInTravel)
					{
						Where = TEXT("under the door's travel");
					}
					else if (bInFrame)
					{
						Where = TEXT("at the frame's foot");
					}
					else if (FMath::Abs(LocalAt.Y) > 0.5 * O.Width && FMath::Abs(LocalAt.Y) <= FrameHalf + 150.0 && FMath::Abs(LocalAt.X) <= FrameDepth + 60.0)
					{
						Where = TEXT("at the sides between the frame and the wall");
					}
					else if (FMath::Abs(LocalAt.Y) <= 0.5 * O.Width && FMath::Abs(LocalAt.X) <= FootprintHalfDepth)
					{
						Where = TEXT("in the opening, beside the panel's footprint");
					}
					else
					{
						Where = TEXT("beyond the opening");
					}
				}
				R.Where.FindOrAdd(Where) += 1;
			}
		}
		return R;
	}

	static void SaveMask(const FString& MaskName, int32 Width, int32 Height, const TArray<FColor>& Mask)
	{
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Mask.GetData(), Mask.Num()), Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief35"), MaskName + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		Note(FString::Printf(TEXT("mask %s saved (red: void; orange: the plates' joints; grey: depth): %s"), *MaskName, *Path));
	}

	static FString DescribeVoid(const FVoidCount& R)
	{
		FString S = FString::Printf(TEXT("%d void pixel(s) of %d (nothing rendered, or a hit more than 11 cm below the floor's top, under the plates); %d on the plates' joints (5 to 11 cm below, the deepest %.1f cm), not void"),
			R.Void, R.Pixels, R.Joint, R.DeepestJoint);
		for (const TPair<FString, int32>& Where : R.Where)
		{
			S += FString::Printf(TEXT("; %s: %d"), *Where.Key, Where.Value);
		}
		if (R.Local.bIsValid)
		{
			S += FString::Printf(TEXT(" (where the rays cross the floor's level, in the opening's frame: X %.0f to %.0f cm, Y %.0f to %.0f cm)"), R.Local.Min.X, R.Local.Max.X, R.Local.Min.Y, R.Local.Max.Y);
		}
		if (R.Void > 0)
		{
			S += FString::Printf(TEXT("; their hits below the floor: 5-30 cm %d (of which 5-11 cm, within the plates' own thickness, %d), 30 cm-2 m %d, deeper or nothing %d; the deepest %.1f cm"),
				R.Shallow, R.InPlate, R.Mid, R.Deep, R.DeepestBelow);
			TArray<int32> Keys;
			R.AcrossBins.GetKeys(Keys);
			Keys.Sort();
			S += TEXT("; across (Y, 10 cm bins):");
			for (const int32 K : Keys)
			{
				S += FString::Printf(TEXT(" [%d,%d) %d"), K * 10, K * 10 + 10, R.AcrossBins[K]);
			}
			Keys.Reset();
			R.ThroughBins.GetKeys(Keys);
			Keys.Sort();
			S += TEXT("; through (X, 20 cm bins):");
			for (const int32 K : Keys)
			{
				S += FString::Printf(TEXT(" [%d,%d) %d"), K * 20, K * 20 + 20, R.ThroughBins[K]);
			}
		}
		return S;
	}

	// --- Flicker (Part C, on screen) ---

	struct FFlicker
	{
		FIntRect Rect;
		TArray<int8> Last;
		TArray<int8> BeforeLast;
		TArray<uint8> Changes;
		/** The shot of this frame with the frame hidden, and with everything but the frame hidden. */
		TArray<FColor> Hidden;
		TArray<FColor> Alone;
		/** Pixels the frame alone does not cover in every frame (a silhouette sweeping across them): left out. */
		TArray<uint8> Uncovered;
		/** Every other shown piece's component, hidden for the capture of the frame alone. */
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Others;
		double Distance = 0.0;
		int32 SavedAA = -1;
		/** The screen-space effects switched off for the measure, with their values to put back. */
		TArray<TPair<FString, FString>> SavedCVars;
		int32 Frames = 0;
		FVector Eye = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		FVector Step = FVector::ZeroVector;
	};
	static FFlicker GFlicker;

	static FIntRect ProjectBox(const FBox& Box, int32 ShotWidth, int32 ShotHeight)
	{
		const APlayerController* C = PC();
		int32 VX = 0, VY = 0;
		if (!C)
		{
			return FIntRect();
		}
		C->GetViewportSize(VX, VY);
		FBox2D Out(ForceInit);
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector Corner((I & 1) ? Box.Max.X : Box.Min.X, (I & 2) ? Box.Max.Y : Box.Min.Y, (I & 4) ? Box.Max.Z : Box.Min.Z);
			FVector2D Screen;
			if (C->ProjectWorldLocationToScreen(Corner, Screen, false) && VX > 0 && VY > 0)
			{
				Out += FVector2D(Screen.X * ShotWidth / VX, Screen.Y * ShotHeight / VY);
			}
		}
		if (!Out.bIsValid)
		{
			return FIntRect(0, 0, ShotWidth, ShotHeight);
		}
		return FIntRect(FMath::Clamp(FMath::FloorToInt32(Out.Min.X), 0, ShotWidth), FMath::Clamp(FMath::FloorToInt32(Out.Min.Y), 0, ShotHeight),
			FMath::Clamp(FMath::CeilToInt32(Out.Max.X), 0, ShotWidth), FMath::Clamp(FMath::CeilToInt32(Out.Max.Y), 0, ShotHeight));
	}

	/** The frame's own component drawn (true) or hidden; for an instanced frame, its whole component (every gate frame). */
	static void ShowFrame(const FFrameRef* F, bool bShow)
	{
		if (UStaticMeshComponent* C = F ? F->Piece.Component.Get() : nullptr)
		{
			C->SetVisibility(bShow);
		}
	}

	/** Every other shown piece drawn (true) or hidden, for the capture of the frame alone. */
	static void ShowOthers(bool bShow)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& Weak : GFlicker.Others)
		{
			if (UStaticMeshComponent* C = Weak.Get())
			{
				C->SetVisibility(bShow);
			}
		}
	}

	/**
	 * 30 frames of a frame's jamb from Distance, the camera moving 1 cm a frame across. Each frame is captured twice, the frame
	 * hidden then drawn: a pixel shows the frame where the two differ. A pixel of two surfaces in one plane is the frame's in one
	 * frame, the other's in the next and the frame's again (A, B, A); the frame's own texture sweeping past as the camera moves
	 * never changes which surface a pixel shows. Pixels that do A-B-A 3 times or more flicker.
	 */
	static void AddFlicker(TArray<FStep>& S, TFunction<const FFrameRef*()> Frame, double Distance, const FString& Label)
	{
		S.Add(Do(FString::Printf(TEXT("C: %s: the pose"), *Label), [Frame, Distance, Label]()
		{
			const FFrameRef* F = Frame();
			if (!F)
			{
				Note(FString::Printf(TEXT("C: %s: no frame in this map"), *Label));
				return 0.f;
			}
			const FTransform& T = F->Piece.Transform;
			const FVector Normal = T.GetUnitAxis(EAxis::X);
			const FVector Across = T.GetUnitAxis(EAxis::Y);
			const FVector Base(F->Piece.Box.GetCenter().X, F->Piece.Box.GetCenter().Y, F->Piece.Box.Min.Z);
			const double HalfAcross = 0.5 * FMath::Abs(FVector::DotProduct(F->Piece.Box.GetSize(), Across.GetAbs()));
			const FVector Jamb = Base + Across * (HalfAcross - 25.0) + FVector(0.0, 0.0, 150.0);
			GFlicker = FFlicker();
			GFlicker.Look = Jamb;
			GFlicker.Step = Across;
			// In front of the frame along its +X, else along its -X, at Distance or the farthest point up to it (down to 3 m) from
			// which nothing stands between the eye and the jamb.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsSeamPose), false, Pawn());
			UWorld* World = W();
			bool bFound = false;
			for (double D = Distance; D >= 300.0 && !bFound; D -= 50.0)
			{
				for (const double Side : { 1.0, -1.0 })
				{
					const FVector Eye = Base + Normal * (Side * D) + Across * (HalfAcross - 80.0) + FVector(0.0, 0.0, 160.0);
					FHitResult Hit;
					if (World && !World->LineTraceSingleByChannel(Hit, GFlicker.Look - (GFlicker.Look - Eye).GetSafeNormal() * 5.0, Eye, ECC_Visibility, Params))
					{
						GFlicker.Eye = Eye;
						GFlicker.Distance = D;
						bFound = true;
						break;
					}
				}
			}
			if (!bFound)
			{
				GFlicker.Eye = Base + Normal * Distance + Across * (HalfAcross - 80.0) + FVector(0.0, 0.0, 160.0);
				GFlicker.Distance = Distance;
			}
			// Anti-aliasing off for the 30 frames: its history would carry the frame's image into the capture where it is hidden.
			// The screen-space effects off too (ambient occlusion, reflections, contact shadows), and the shadows: hiding the frame
			// changes them around it, and they move with the camera (the flashlight is the camera's); neither the frame nor the
			// surfaces in its planes depend on them.
			if (IConsoleVariable* AA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
			{
				GFlicker.SavedAA = AA->GetInt();
				AA->Set(0, ECVF_SetByConsole);
			}
			for (const TCHAR* CVarName : { TEXT("r.AmbientOcclusionLevels"), TEXT("r.SSR.Quality"), TEXT("r.ContactShadows"), TEXT("r.ShadowQuality") })
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(CVarName))
				{
					GFlicker.SavedCVars.Emplace(CVarName, Var->GetString());
					Var->Set(0, ECVF_SetByConsole);
				}
			}
			// Every other shown piece, for the capture of the frame alone.
			for (TActorIterator<AActor> It(World); World && It; ++It)
			{
				TArray<UStaticMeshComponent*> Components;
				It->GetComponents(Components);
				for (UStaticMeshComponent* C : Components)
				{
					if (IsShown(C) && C != F->Piece.Component.Get())
					{
						GFlicker.Others.Add(C);
					}
				}
			}
			PlaceEye(GFlicker.Eye, GFlicker.Look);
			FlashlightOn();
			Note(FString::Printf(TEXT("C: %s: the eye %.1f m from the frame%s; %d other pieces hidden for the capture of the frame alone"), *Label, GFlicker.Distance / 100.0,
				bFound ? TEXT("") : TEXT(" (no point in sight of the jamb found)"), GFlicker.Others.Num()));
			return 0.4f;
		}));
		for (int32 K = 0; K < 30; ++K)
		{
			S.Add(Do(TEXT("C: flicker frame, hidden"), [Frame, K]()
			{
				if (const FFrameRef* F = Frame())
				{
					PlaceEye(GFlicker.Eye + GFlicker.Step * K, GFlicker.Look + GFlicker.Step * K);
					ShowFrame(F, false);
				}
				return 0.1f;
			}));
			S.Add(Do(TEXT("C: flicker frame, hidden, shot"), [Frame]()
			{
				if (Frame()) { RequestShot(FString()); } else { bShotReady = true; }
				return 0.f;
			}));
			S.Add(WaitFor(TEXT("C: flicker frame taken"), []() { return bShotReady; }, 5.f, [Frame]()
			{
				GFlicker.Hidden = GShot.Pixels;
				ShowFrame(Frame(), true);
				ShowOthers(false);
				return 0.1f;
			}));
			S.Add(Do(TEXT("C: flicker frame, alone, shot"), [Frame]()
			{
				if (Frame()) { RequestShot(FString()); } else { bShotReady = true; }
				return 0.f;
			}));
			S.Add(WaitFor(TEXT("C: flicker frame taken"), []() { return bShotReady; }, 5.f, []()
			{
				GFlicker.Alone = GShot.Pixels;
				ShowOthers(true);
				return 0.1f;
			}));
			S.Add(Do(TEXT("C: flicker frame, drawn, shot"), [Frame, K, Label]()
			{
				if (Frame()) { RequestShot(K == 0 ? FString::Printf(TEXT("%s_%s_%s"), *MapShort(), *Label, *GRun.Tag) : FString()); } else { bShotReady = true; }
				return 0.f;
			}));
			S.Add(WaitFor(TEXT("C: flicker frame taken"), []() { return bShotReady; }, 5.f, [Frame, K]()
			{
				const FFrameRef* F = Frame();
				if (!F || GFlicker.Hidden.Num() != GShot.Pixels.Num() || GFlicker.Alone.Num() != GShot.Pixels.Num())
				{
					return 0.f;
				}
				if (K == 0)
				{
					GFlicker.Rect = ProjectBox(F->Piece.Box, GShot.Width, GShot.Height);
					GFlicker.Last.Init(-1, GShot.Width * GShot.Height);
					GFlicker.BeforeLast.Init(-1, GShot.Width * GShot.Height);
					GFlicker.Changes.Init(0, GShot.Width * GShot.Height);
					GFlicker.Uncovered.Init(0, GShot.Width * GShot.Height);
				}
				for (int32 Y = GFlicker.Rect.Min.Y; Y < GFlicker.Rect.Max.Y; ++Y)
				{
					for (int32 X = GFlicker.Rect.Min.X; X < GFlicker.Rect.Max.X; ++X)
					{
						const int32 I = Y * GShot.Width + X;
						if (!GShot.Pixels.IsValidIndex(I) || !GFlicker.Last.IsValidIndex(I))
						{
							continue;
						}
						const FColor& Drawn = GShot.Pixels[I];
						const FColor& Behind = GFlicker.Hidden[I];
						// The frame alone must cover the pixel in this frame (against the empty background): a pixel its silhouette
						// sweeps across as the camera moves is not one of a surface in its plane.
						const FColor& Alone = GFlicker.Alone[I];
						if (FMath::Max3(Alone.R, Alone.G, Alone.B) <= 10)
						{
							GFlicker.Uncovered[I] = 1;
							continue;
						}
						if (FMath::Max3(Drawn.R, Drawn.G, Drawn.B) < 12 && FMath::Max3(Behind.R, Behind.G, Behind.B) < 12)
						{
							continue;
						}
						const int32 Diff = FMath::Abs(Drawn.R - Behind.R) + FMath::Abs(Drawn.G - Behind.G) + FMath::Abs(Drawn.B - Behind.B);
						// The frame wins the pixel where drawn and hidden differ by 48 levels or more (summed over the three channels);
						// another surface wins it where they are the same (6 levels or less). In between is shading near the frame
						// (light and shadow filtering as the camera moves), which says nothing of which surface the pixel shows: left
						// out. Two surfaces in one plane trade a pixel between the two extremes.
						const int32 Class = Diff >= 48 ? 1 : (Diff <= 6 ? 0 : -1);
						if (Class < 0)
						{
							continue;
						}
						if (GFlicker.BeforeLast[I] >= 0 && GFlicker.Last[I] >= 0 && GFlicker.BeforeLast[I] == Class && GFlicker.Last[I] != Class && GFlicker.Changes[I] < 255)
						{
							++GFlicker.Changes[I];
						}
						GFlicker.BeforeLast[I] = GFlicker.Last[I];
						GFlicker.Last[I] = static_cast<int8>(Class);
					}
				}
				++GFlicker.Frames;
				return 0.f;
			}));
		}
		S.Add(Do(FString::Printf(TEXT("C: %s: counted"), *Label), [Frame, Label]()
		{
			if (GFlicker.SavedAA >= 0)
			{
				if (IConsoleVariable* AA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
				{
					AA->Set(GFlicker.SavedAA, ECVF_SetByConsole);
				}
				GFlicker.SavedAA = -1;
			}
			for (const TPair<FString, FString>& Saved : GFlicker.SavedCVars)
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
				{
					Var->Set(*Saved.Value, ECVF_SetByConsole);
				}
			}
			GFlicker.SavedCVars.Reset();
			if (!Frame())
			{
				return 0.f;
			}
			const double Distance = GFlicker.Distance;
			int32 Shown = 0, Flicker = 0;
			TArray<FColor> Mask;
			Mask.Init(FColor::Black, GFlicker.Changes.Num());
			// A surface in another's plane trades an area of pixels; an edge stepping across one pixel as the camera moves can make
			// that one pixel go back and forth: a flickering pixel counts when one of its eight neighbours flickers too, and the
			// isolated ones are reported apart.
			const int32 Width = GShot.Width;
			auto Flickers = [&](int32 I) { return GFlicker.Changes.IsValidIndex(I) && GFlicker.Changes[I] >= 3 && GFlicker.Uncovered.IsValidIndex(I) && GFlicker.Uncovered[I] == 0; };
			int32 Isolated = 0;
			for (int32 I = 0; I < GFlicker.Changes.Num(); ++I)
			{
				bool bFlicker = Flickers(I);
				if (bFlicker && Width > 0)
				{
					const int32 X = I % Width;
					bool bNeighbour = false;
					for (int32 DY = -1; DY <= 1 && !bNeighbour; ++DY)
					{
						for (int32 DX = -1; DX <= 1 && !bNeighbour; ++DX)
						{
							if ((DX != 0 || DY != 0) && X + DX >= 0 && X + DX < Width)
							{
								bNeighbour = Flickers(I + DY * Width + DX);
							}
						}
					}
					if (!bNeighbour)
					{
						++Isolated;
						bFlicker = false;
					}
				}
				Flicker += bFlicker ? 1 : 0;
				Shown += GFlicker.Last[I] == 1 ? 1 : 0;
				Mask[I] = bFlicker ? FColor::Red : (GFlicker.Last[I] == 1 ? FColor(60, 60, 0) : (GFlicker.Last[I] == 0 ? FColor(20, 20, 20) : FColor::Black));
			}
			if (Flicker > 0 && GShot.Width * GShot.Height == Mask.Num())
			{
				TArray64<uint8> Png;
				FImageUtils::PNGCompressImageArray(GShot.Width, GShot.Height, TArrayView64<const FColor>(Mask.GetData(), Mask.Num()), Png);
				const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief35"),
					FString::Printf(TEXT("%s_%s_FlickerMask_%s.png"), *MapShort(), *Label, *GRun.Tag)));
				FFileHelper::SaveArrayToFile(Png, *Path);
				Note(FString::Printf(TEXT("C: %s: the mask of the flickering pixels (red; the frame's pixels dim yellow): %s"), *Label, *Path));
			}
			Check(GFlicker.Frames == 30 && Flicker == 0 && Shown > 0, FString::Printf(TEXT("C: %s: on %d frames from %.0f m, the camera moving 1 cm a frame, each captured with the frame hidden, alone and drawn, no pixel the frame covers in every frame of the frame's rectangle (%d, %d)-(%d, %d) alternates between the frame and another surface: %d pixels went frame-other-frame 3 times or more next to another that did (%d isolated ones, on edges), %d pixels showing the frame at the last frame"),
				*Label, GFlicker.Frames, Distance / 100.0, GFlicker.Rect.Min.X, GFlicker.Rect.Min.Y, GFlicker.Rect.Max.X, GFlicker.Rect.Max.Y, Flicker, Isolated, Shown));
			return 0.f;
		}));
	}

	// --- The run ---

	struct FCheckState
	{
		TArray<FPiece> Pieces;
		TArray<FOpening> Openings;
		TArray<FFrameRef> Frames;
		TArray<TPair<TWeakObjectPtr<AActor>, bool>> OpenBefore;
		FTransform PawnBefore;
		FRotator ControlBefore;
	};
	static FCheckState G;

	static const FFrameRef* FirstFrame()
	{
		return G.Frames.Num() > 0 ? &G.Frames[0] : nullptr;
	}

	static void Cleanup()
	{
		for (const FOpening& O : G.Openings)
		{
			for (const TPair<TWeakObjectPtr<AActor>, bool>& Was : G.OpenBefore)
			{
				if (Was.Key == O.Actor && IsOpen(O) != Was.Value)
				{
					SetOpening(O, Was.Value);
				}
			}
		}
		if (ACharacter* P = Pawn())
		{
			P->SetActorTransform(G.PawnBefore, false, nullptr, ETeleportType::TeleportPhysics);
			P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			if (APlayerController* C = PC())
			{
				C->SetControlRotation(G.ControlBefore);
			}
		}
		if (GShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(GShotHandle);
			GShotHandle.Reset();
		}
		ShowOthers(true);
		GMeshCache.Reset();
		bRenderedFloorOnly = false;
		GFramesForVoid.Reset();
		GFloorPieces = nullptr;
		if (GFlicker.SavedAA >= 0)
		{
			if (IConsoleVariable* AA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
			{
				AA->Set(GFlicker.SavedAA, ECVF_SetByConsole);
			}
			GFlicker.SavedAA = -1;
		}
		for (const TPair<FString, FString>& Saved : GFlicker.SavedCVars)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
			{
				Var->Set(*Saved.Value, ECVF_SetByConsole);
			}
		}
		GFlicker.SavedCVars.Reset();
	}

	static void Start(bool bQuit, const FString& Tag)
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.bQuit = bQuit;
		GRun.Tag = Tag;
		G = FCheckState();
		TArray<FStep>& S = GRun.Steps;

		S.Add(WaitFor(TEXT("setup: a game world with the player, its area (if any) ready"), []()
		{
			const UWorld* World = W();
			const AAreaGenerator* Generator = World ? AAreaGenerator::Find(World) : nullptr;
			return Pawn() && (!Generator || Generator->IsReady());
		}, 60.f, []()
		{
			UWorld* World = W();
			GatherPieces(World, G.Pieces);
			GatherOpenings(World, G.Openings);
			GatherFrames(G.Pieces, G.Frames);
			GFramesForVoid = G.Frames;
			// In an area: the samples test the rendered floor only (follow-up of Brief 35).
			bRenderedFloorOnly = AAreaGenerator::Find(World) != nullptr;
			G.PawnBefore = Pawn()->GetActorTransform();
			G.ControlBefore = PC()->GetControlRotation();
			for (const FOpening& O : G.Openings)
			{
				G.OpenBefore.Emplace(O.Actor, IsOpen(O));
			}
			GRun.Cleanup = &Cleanup;
			Note(FString::Printf(TEXT("map %s (tag %s): %d shown pieces, %d openings, %d door and gate frames"), *MapShort(), *GRun.Tag, G.Pieces.Num(), G.Openings.Num(), G.Frames.Num()));
			if (const AAreaGenerator* Generator = AAreaGenerator::Find(World))
			{
				Note(FString::Printf(TEXT("area: seed %d, layout hash %016llx, %d passages published"), Generator->GetSeed(), static_cast<uint64>(Generator->GetLayoutHash()), Generator->GetPassages().Num()));
			}
			return 0.f;
		}));

		// Part A: the floor under the openings, every one, open, and the travel of each.
		S.Add(Do(TEXT("A: the floor under the openings"), []()
		{
			for (FOpening& O : G.Openings)
			{
				CheckTravel(G.Pieces, O);
				CheckFloorUnder(G.Pieces, O);
			}
			Check(G.Openings.Num() > 0, FString::Printf(TEXT("A: the map has openings to check (%d)"), G.Openings.Num()));
			return 0.f;
		}));
		// A's shot: the first opening that is not a hangar's hatch, open, looking down at its threshold from 1.5 m inside.
		S.Add(Do(TEXT("A: the threshold's pose"), []()
		{
			for (const FOpening& O : G.Openings)
			{
				if (O.bHangarHatch)
				{
					continue;
				}
				SetOpening(O, true);
				const double Side = InsideSign(G.Pieces, O);
				PlaceEye(O.Frame.TransformPosition(FVector(Side * 170.0, -60.0, 165.0)), O.Frame.TransformPosition(FVector(0.0, 0.0, 0.0)));
				FlashlightOn();
				Note(FString::Printf(TEXT("A: the threshold's shot: %s, open, from 1.7 m on its floor side, looking down"), *O.Label));
				return 0.4f;
			}
			return 0.f;
		}));
		AddShot(S, []() { return FString::Printf(TEXT("%s_Threshold_%s"), *MapShort(), *GRun.Tag); });

		// Follow-up of Brief 35, areas only: what lies unshown under each gate, a depth capture from straight above each open
		// gate (the opening's pixels where nothing is rendered, or something more than 5 cm below its floor), and the first
		// gate (the one nearest the player's start) as the player sees it, from 2 m at eye height and from inside the opening.
		S.Add(Do(TEXT("A, areas: the gates as rendered"), []()
		{
			UWorld* World = W();
			if (!World || !AAreaGenerator::Find(World))
			{
				return 0.f;
			}
			for (const FOpening& O : G.Openings)
			{
				SetOpening(O, false);
				const FBox Panel = OwnBox(O);
				if (Panel.IsValid)
				{
					const FBox Local = Panel.InverseTransformBy(O.Frame);
					Note(FString::Printf(TEXT("A: %s closed: its panel's box in its frame X %.1f to %.1f, Y %.1f to %.1f, Z %.1f to %.1f (its bottom %.1f cm %s the floor)"), *O.Label,
						Local.Min.X, Local.Max.X, Local.Min.Y, Local.Max.Y, Local.Min.Z, Local.Max.Z, FMath::Abs(Local.Min.Z), Local.Min.Z >= 0.0 ? TEXT("above") : TEXT("below")));
				}
				NoteUnshownUnder(World, O);
				NoteFloorAround(G.Pieces, O);
				SetOpening(O, true);
			}
			FlushRenderingCommands();
			for (const FOpening& O : G.Openings)
			{
				const double Half = 0.5 * O.Depth + 10.0;
				const double Up = FMath::Clamp(O.Height - 30.0, 150.0, 260.0);
				const FVector Eye = O.Frame.TransformPosition(FVector(0.0, 0.0, Up));
				const FRotator Rotation = FRotationMatrix::MakeFromXZ(FVector(0.0, 0.0, -1.0), O.Frame.GetUnitAxis(EAxis::X)).Rotator();
				TArray<float> Depth;
				const bool bCaptured = CaptureDepth(Eye, Rotation, 110.f, 512, 512, Depth);
				if (!bCaptured)
				{
					Check(false, FString::Printf(TEXT("A, areas: %s: the depth capture from straight above failed"), *O.Label));
					continue;
				}
				const FVoidCount R = CountVoid(O, Eye, Rotation, 110.f, 512, 512, Depth, true, Half);
				Check(R.Pixels > 1000 && R.Void == 0, FString::Printf(TEXT("A, areas: %s, open, a depth capture from %.0f cm straight above its middle (512 x 512, 110 degrees): over the opening (%.0f cm across, %.0f cm through), %s"),
					*O.Label, Up, O.Width, 2.0 * Half, *DescribeVoid(R)));
				if (&O == &G.Openings[0] || R.Void > 0)
				{
					SaveMask(FString::Printf(TEXT("%s_%s_Above_VoidMask_%s"), *MapShort(), *O.Label, *GRun.Tag), 512, 512, R.Mask);
				}
			}
			return 0.2f;
		}));
		// The first gate: the one nearest the player's start (the route starts there).
		auto FirstGate = []() -> const FOpening*
		{
			const FOpening* Best = nullptr;
			double BestDist = TNumericLimits<double>::Max();
			for (const FOpening& O : G.Openings)
			{
				const double D = FVector::Dist2D(O.Frame.GetLocation(), G.PawnBefore.GetLocation());
				if (Cast<ADoorDevice>(O.Actor.Get()) && D < BestDist)
				{
					Best = &O;
					BestDist = D;
				}
			}
			return Best;
		};
		// The side the player comes from: the side of the gate the start is on.
		auto ComeFrom = [](const FOpening& O)
		{
			return O.Frame.InverseTransformPosition(G.PawnBefore.GetLocation()).X >= 0.0 ? 1.0 : -1.0;
		};
		for (int32 View = 0; View < 2; ++View)
		{
			S.Add(Do(TEXT("A, areas: the first gate's view"), [FirstGate, ComeFrom, View]()
			{
				const FOpening* O = AAreaGenerator::Find(W()) ? FirstGate() : nullptr;
				if (!O)
				{
					return 0.f;
				}
				SetOpening(*O, true);
				const double Side = ComeFrom(*O);
				if (View == 0)
				{
					// From 2 m at eye height, looking down at the threshold.
					PlaceEye(O->Frame.TransformPosition(FVector(Side * 200.0, 0.0, 165.0)), O->Frame.TransformPosition(FVector(0.0, 0.0, 0.0)));
				}
				else
				{
					// From inside the opening, looking down (a little ahead, so that the view has a direction).
					PlaceEye(O->Frame.TransformPosition(FVector(0.0, 0.0, 165.0)), O->Frame.TransformPosition(FVector(-Side * 30.0, 0.0, 0.0)));
				}
				FlashlightOn();
				Note(FString::Printf(TEXT("A, areas: the first gate %s (nearest the player's start), open, %s"), *O->Label,
					View == 0 ? TEXT("from 2 m at eye height on the side the player comes from, looking down at its threshold") : TEXT("from inside the opening at eye height, looking down")));
				return 0.6f;
			}));
			AddShot(S, [View]()
			{
				return AAreaGenerator::Find(W()) ? FString::Printf(TEXT("%s_Gate%s_%s"), *MapShort(), View == 0 ? TEXT("Threshold2m") : TEXT("InsideDown"), *GRun.Tag) : FString();
			}, [FirstGate, View](const FShot& Shot)
			{
				const FOpening* O = AAreaGenerator::Find(W()) ? FirstGate() : nullptr;
				const APlayerController* C = PC();
				if (!O || !C || !C->PlayerCameraManager || Shot.Width <= 0)
				{
					return;
				}
				const FVector Eye = C->PlayerCameraManager->GetCameraLocation();
				const FRotator Rotation = C->PlayerCameraManager->GetCameraRotation();
				const float Fov = C->PlayerCameraManager->GetFOVAngle();
				const int32 Width = FMath::Min(Shot.Width, 960);
				const int32 Height = FMath::Max(1, FMath::RoundToInt(static_cast<double>(Width) * Shot.Height / Shot.Width));
				TArray<float> Depth;
				if (!CaptureDepth(Eye, Rotation, Fov, Width, Height, Depth))
				{
					Check(false, TEXT("A, areas: the first gate's view: the depth capture failed"));
					return;
				}
				const FVoidCount R = CountVoid(*O, Eye, Rotation, Fov, Width, Height, Depth, false, 0.5 * O->Depth + 10.0);
				Check(R.Void == 0, FString::Printf(TEXT("A, areas: the first gate %s %s, the player's view (%d x %d depth capture, %.0f degrees): %s"), *O->Label,
					View == 0 ? TEXT("from 2 m") : TEXT("from inside the opening"), Width, Height, Fov, *DescribeVoid(R)));
				SaveMask(FString::Printf(TEXT("%s_Gate%s_VoidMask_%s"), *MapShort(), View == 0 ? TEXT("Threshold2m") : TEXT("InsideDown"), *GRun.Tag), Width, Height, R.Mask);
			});
		}

		// Part B: each hangar's hatch.
		S.Add(Do(TEXT("B: the hangars' hatches"), []()
		{
			int32 Hatches = 0;
			for (const FOpening& O : G.Openings)
			{
				if (!O.bHangarHatch)
				{
					continue;
				}
				++Hatches;
				SetOpening(O, true);
				if (!IsOpen(O))
				{
					Note(FString::Printf(TEXT("B: %s is not open now (its own order closes it): measured with its panel where it stands"), *O.Label));
				}
				// The panel open, as the player sees it at the departure.
				TArray<FPiece> Pieces;
				GatherPieces(W(), Pieces);
				CheckHatch(W(), Pieces, O);
				const double Side = InsideSign(Pieces, O);
				PlaceEye(O.Frame.TransformPosition(FVector(Side * 600.0, 0.0, 165.0)), O.Frame.TransformPosition(FVector(-Side * 150.0, 0.0, 0.0)));
				FlashlightOn();
			}
			Note(FString::Printf(TEXT("B: %d hangar hatches in this map"), Hatches));
			return Hatches > 0 ? 0.4f : 0.f;
		}));
		S.Add(Do(TEXT("B: shot"), []()
		{
			for (const FOpening& O : G.Openings)
			{
				if (O.bHangarHatch)
				{
					RequestShot(FString::Printf(TEXT("%s_HatchView_%s"), *MapShort(), *GRun.Tag));
					return 0.f;
				}
			}
			bShotReady = true;
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("B: shot taken (or none)"), []() { return bShotReady; }, 5.f));

		// Part C: the frames' faces, then the flicker from 3 m and from 10 m.
		S.Add(Do(TEXT("C: the frames' faces"), []()
		{
			const int32 Pairs = CheckFrames(G.Pieces, G.Frames);
			Note(FString::Printf(TEXT("C: %d frames, %d pieces with faces in a frame's planes"), G.Frames.Num(), Pairs));
			return 0.f;
		}));
		AddFlicker(S, []() { return FirstFrame(); }, 300.0, TEXT("Frame3m"));
		AddFlicker(S, []() { return FirstFrame(); }, 1000.0, TEXT("Frame10m"));

		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started (%d steps)."), Name, GRun.Steps.Num());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsSeamCheckCommand(
	TEXT("tls.SeamCheck"),
	TEXT("tls.SeamCheck [Quit=0] [Tag=After]: Brief 35 (an area, Prologue_Home, Prologue_Arrival or Epilogue_Chamber; PIE): A, every door and sliding panel that opens has a shown floor piece under it every 20 cm across and through its depth, and no floor piece in its travel; B, each hangar's hatch has its floor running past its panel and frame, and still blocks the way out; C, no face of a door's or gate's frame within 1 cm of the plane of a face it overlaps, and no pixel of a frame alternating between the accent and another colour on 30 frames from 3 m and from 10 m (camera moving 1 cm a frame). Shots (Tag in their names) to Saved/Screenshots/Brief35."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		if (SeamDebug::GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.SeamCheck: already running."));
			return;
		}
		SeamDebug::Start(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0, Args.Num() > 1 ? Args[1] : FString(TEXT("After")));
	}));

#endif

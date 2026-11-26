// SPDX-License-Identifier: MIT

// Render measurements for the modelled maps (Brief 15). Development builds only.
//  tls.RenderStats [Frames] [Label]: draw calls and primitives (triangles) the RHI drew per frame, medians and maxima.
//  tls.StandAt <X> <Y> <LookX> <LookY> [LookZ]: the player's pawn stood on the floor under (X, Y), looking at the point
//    (LookZ left out: at eye height, level).
//  tls.Views <home|arrival|lights> [Label] [Quit]: fixed views of Prologue_Home or Prologue_Arrival, each measured (draw
//    calls, primitives, GPU time), then at the heaviest view (most primitives) the same while Stopped with a scan alive,
//    and the frame of a scan; "lights" shoots Prologue_Home's lit views from the entrance of each of its four spaces at eye
//    height (and Brief 13's lit view) and logs their mean level and share of pixels under 20 levels. Shots to
//    Saved/Screenshots/Brief15.
//  tls.Views <area|areashots|areacheck> [Label] [Quit] (Brief 16): views of the current area computed from its plan (the
//    same poses before and after the modelling): area measures each segment's views like home; areashots shoots each
//    segment from its entry with the flashlight and in a scan; areacheck shoots the first corridor, the first gate's
//    terminal and the first catwalk as the checkpoint asks. Shots to Saved/Screenshots/Brief16.
//  tls.Views <halls|hallbeacons|hallcheck> [Label] [Quit] (Brief 19): the halls' checkpoint views (entrance, a web's walkway
//    from 20 m further and its second platform from 5 m, a floor hall's edge from 5 m, the joints) and the beacons' views at
//    today's cone and intensity and at two others (tls.Beacon.Light). Shots to Saved/Screenshots/Brief19.
//  tls.Views areabackdrop [Label] [Quit] (Brief 28): the backdrop's views (the largest open segment from its entrance
//    ahead, up and down; a mass of each family from 15 m; a pillar's top from 4 m). Shots to Saved/Screenshots/Brief28.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Area/AreaGenerator.h"
#include "Area/AreaInfo.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Energy/BeaconBody.h"
#include "Energy/DoorDevice.h"
#include "Energy/PowerTerminal.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "LidarScanComponent.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIStats.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UnrealClient.h"

namespace RenderStatsDebug
{
	static UWorld* GameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld() && (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game))
			{
				return World;
			}
		}
		return nullptr;
	}

	static APlayerController* PC()
	{
		UWorld* World = GameWorld();
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	static APawn* Pawn()
	{
		APlayerController* C = PC();
		return C ? C->GetPawn() : nullptr;
	}

	static FString CameraPose()
	{
		UWorld* World = GameWorld();
		const APlayerController* C = PC();
		if (!World || !C)
		{
			return TEXT("no game world");
		}
		FVector Location;
		FRotator Rotation;
		C->GetPlayerViewPoint(Location, Rotation);
		return FString::Printf(TEXT("map %s, camera (%.0f, %.0f, %.0f) pitch %.1f yaw %.1f"), *World->GetMapName(), Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw);
	}

	template <typename T>
	static T Median(TArray<T> Values)
	{
		if (Values.IsEmpty())
		{
			return T(-1);
		}
		Values.Sort();
		return Values[Values.Num() / 2];
	}

	template <typename T>
	static T Max(const TArray<T>& Values)
	{
		T M = T(-1);
		for (const T V : Values)
		{
			M = FMath::Max(M, V);
		}
		return M;
	}

	/** Stands the pawn on whatever floor is under (X, Y) (a trace from 1.5 m above its height) and turns it to LookAt (Z < -1e5: at eye height). */
	static bool StandAt(double X, double Y, const FVector& LookAt)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		UWorld* World = GameWorld();
		if (!P || !C || !World)
		{
			return false;
		}
		const ACharacter* Character = Cast<ACharacter>(P);
		const double HalfHeight = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		const double FromZ = FMath::Max(P->GetActorLocation().Z, HalfHeight) + 150.0;
		double Z = P->GetActorLocation().Z;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("TlsStandAt"), false, P);
		if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, FromZ), FVector(X, Y, FromZ - 2000.0), ECC_Visibility, Params))
		{
			Z = Hit.ImpactPoint.Z + HalfHeight + 2.0;
		}
		P->SetActorLocation(FVector(X, Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
		if (Character)
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		const FVector Eye = P->GetPawnViewLocation();
		const FVector Target(LookAt.X, LookAt.Y, LookAt.Z < -1.e5 ? Eye.Z : LookAt.Z);
		C->SetControlRotation((Target - Eye).Rotation());
		return true;
	}

	// --- tls.RenderStats ---------------------------------------------------------------------------------------------

	struct FStatsRun
	{
		FTSTicker::FDelegateHandle Ticker;
		int32 Frames = 0;
		int32 Skip = 0;
		FString Label;
		TArray<int32> Draws;
		TArray<int32> Prims;
	};
	static FStatsRun GStats;

	static bool TickStats(float)
	{
		// The RHI's totals are those of a frame the GPU has finished: the first frames after the command still show the
		// frames before it (the view may just have moved), so a few are skipped.
		if (GStats.Skip > 0)
		{
			--GStats.Skip;
			return true;
		}
		GStats.Draws.Add(GNumDrawCallsRHI[0]);
		GStats.Prims.Add(GNumPrimitivesDrawnRHI[0]);
		if (GStats.Draws.Num() < GStats.Frames)
		{
			return true;
		}
		UE_LOG(LogTheLastShift, Display, TEXT("RenderStats: ===== %s: %d frames, %s. Draw calls median %d max %d; primitives drawn median %d max %d. ====="),
			*GStats.Label, GStats.Draws.Num(), *CameraPose(), Median(GStats.Draws), Max(GStats.Draws), Median(GStats.Prims), Max(GStats.Prims));
		GStats.Ticker.Reset();
		return false;
	}

	// --- tls.Views ---------------------------------------------------------------------------------------------------

	struct FPose
	{
		FString Name;
		double X, Y;
		FVector Look;
		/** When set (>= 0), the pawn is first put at this height, so the floor trace starts under any ceiling (areas). */
		double FromZ = -1.0;
	};

	static constexpr double EyeLevel = -1.e6;

	/** The views of each map, chosen to see the most geometry; the same poses before and after the modelling. */
	static TArray<FPose> HeavyPoses(bool bHome)
	{
		if (bHome)
		{
			return {
				{ TEXT("PrintRoom_FromStart"), 200.0, 0.0, FVector(800.0, 0.0, EyeLevel) },
				{ TEXT("Corridor_FromStart"), 870.0, 0.0, FVector(2810.0, 0.0, EyeLevel) },
				{ TEXT("Corridor_FromEnd"), 2750.0, 0.0, FVector(810.0, 0.0, EyeLevel) },
				{ TEXT("Monitor_FromDoorway"), 1810.0, 220.0, FVector(1810.0, 1570.0, EyeLevel) },
				{ TEXT("Monitor_FromCorner"), 1160.0, 1520.0, FVector(2510.0, 170.0, EyeLevel) },
				{ TEXT("Hangar_FromEntry"), 2900.0, 0.0, FVector(4330.0, 0.0, 250.0) },
				{ TEXT("Hangar_FromCorner"), 5750.0, 1900.0, FVector(2830.0, -2000.0, 300.0) },
				{ TEXT("Hangar_FromHatchSide"), 4330.0, -1900.0, FVector(4330.0, 0.0, 250.0) } };
		}
		return {
			{ TEXT("Ship_FromStart"), 800.0, 0.0, FVector(4000.0, 0.0, EyeLevel) },
			{ TEXT("Hangar_FromExitDoor"), 3900.0, 0.0, FVector(900.0, 0.0, 250.0) },
			{ TEXT("Hangar_FromCorner"), 3900.0, 1400.0, FVector(0.0, -1400.0, 300.0) },
			{ TEXT("Hangar_FromHatchCorner"), 200.0, 1400.0, FVector(4000.0, -1400.0, 300.0) },
			{ TEXT("Hangar_FromSide"), 2000.0, -1400.0, FVector(900.0, 0.0, 250.0) } };
	}

	/** Prologue_Home's lit views: from the entrance of each of its four spaces at eye height, looking in. */
	static TArray<FPose> LightPoses()
	{
		return {
			{ TEXT("PrintRoom"), 740.0, 0.0, FVector(0.0, 0.0, EyeLevel) },
			{ TEXT("Corridor"), 860.0, 0.0, FVector(2810.0, 0.0, EyeLevel) },
			{ TEXT("MonitorRoom"), 1810.0, 220.0, FVector(1810.0, 1570.0, EyeLevel) },
			{ TEXT("Hangar"), 2880.0, 0.0, FVector(5830.0, 0.0, EyeLevel) } };
	}

	/** Views of each space for the before and after shots, normal light and scan: the corridor from both ends first. */
	static TArray<FPose> ShotPoses(bool bHome)
	{
		if (bHome)
		{
			return {
				{ TEXT("Corridor_FromStart"), 860.0, 0.0, FVector(2810.0, 0.0, EyeLevel) },
				{ TEXT("Corridor_FromEnd"), 2760.0, 0.0, FVector(810.0, 0.0, EyeLevel) },
				{ TEXT("PrintRoom_FromDoor"), 740.0, 0.0, FVector(0.0, 0.0, EyeLevel) },
				{ TEXT("MonitorRoom_FromDoorway"), 1810.0, 220.0, FVector(1810.0, 1570.0, EyeLevel) },
				{ TEXT("Hangar_FromEntry"), 2880.0, 0.0, FVector(4330.0, 0.0, 250.0) } };
		}
		return {
			{ TEXT("Ship_FromStart"), 800.0, 0.0, FVector(4000.0, 0.0, EyeLevel) },
			{ TEXT("Hangar_FromExitDoor"), 3900.0, 0.0, FVector(900.0, 0.0, 250.0) },
			{ TEXT("Hangar_FromSide"), 2000.0, -1400.0, FVector(900.0, 0.0, 250.0) } };
	}

	struct FStep
	{
		FString Label;
		/** Runs once; returns the seconds to wait before the next step. */
		TFunction<float()> Run;
	};

	struct FViewsRun
	{
		FTSTicker::FDelegateHandle Ticker;
		TArray<FStep> Steps;
		int32 Index = 0;
		double WaitUntil = 0.0;
		FString Label;
		bool bQuit = false;
		// Sampling: frames still to take, and what was taken.
		int32 SampleLeft = 0;
		int32 SampleSkip = 0;
		FString SampleName;
		TArray<int32> Draws, Prims;
		TArray<double> Gpu, Frame;
		TMap<FString, int32> PrimsByPose;
		// Scan frames: scans still to take, frames to the next one, the frame times after each.
		int32 ScansLeft = 0;
		int32 FramesToScan = 0;
		int32 AfterScan = -1;
		TArray<double> ScanFrameMs, ScanGpuMs, IdleFrameMs, IdleGpuMs;
		double WorstFrame = 0.0, WorstGpu = 0.0;
		// Shots.
		bool bShotReady = false;
		FString ShotName;
		FString ShotFolder = TEXT("Brief15");
	};
	static FViewsRun GViews;
	static FDelegateHandle GShotHandle;

	static void Note(const FString& Text)
	{
		UE_LOG(LogTheLastShift, Display, TEXT("Views: %s: %s"), *GViews.Label, *Text);
	}

	static UTimeStateSubsystem* TS()
	{
		UWorld* World = GameWorld();
		return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	}

	static ULidarScanComponent* Lidar()
	{
		APawn* P = Pawn();
		return P ? P->FindComponentByClass<ULidarScanComponent>() : nullptr;
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		if (GViews.ShotName.IsEmpty() || Pixels.Num() < Width * Height)
		{
			return;
		}
		double Luma = 0.0, Rgb = 0.0;
		int64 Dark = 0;
		for (const FColor& C : Pixels)
		{
			const double L = 0.299 * C.R + 0.587 * C.G + 0.114 * C.B;
			Luma += L;
			Rgb += (C.R + C.G + C.B) / 3.0;
			Dark += L < 20.0 ? 1 : 0;
		}
		const double N = static_cast<double>(Width) * Height;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), GViews.ShotFolder, GViews.ShotName + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		Note(FString::Printf(TEXT("SHOT %s: %d x %d, mean level %.1f (luma; mean of R, G, B %.1f), %.1f%% of pixels under 20 levels; %s; saved %s"),
			*GViews.ShotName, Width, Height, Luma / N, Rgb / N, 100.0 * Dark / N, *CameraPose(), *Path));
		GViews.ShotName.Reset();
		GViews.bShotReady = true;
	}

	static void Finish()
	{
		Note(TEXT("===== done ====="));
		if (GShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(GShotHandle);
			GShotHandle.Reset();
		}
		GViews.Ticker.Reset();
		if (GViews.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.Views"));
		}
	}

	/**
	 * Triangles of every rendered mesh and instance whose bounds touch the view's cone (LOD 0, before occlusion): an upper
	 * bound of what the view can draw. The RHI's counter counts an instanced draw issued indirectly about once, so it
	 * misses most of the instances of the areas' boxes and dressing.
	 */
	static int64 TrianglesInView(int32& OutInstances)
	{
		OutInstances = 0;
		UWorld* World = GameWorld();
		APlayerController* C = PC();
		if (!World || !C || !C->PlayerCameraManager)
		{
			return -1;
		}
		FVector Eye;
		FRotator Rot;
		C->GetPlayerViewPoint(Eye, Rot);
		const FVector Fwd = Rot.Vector();
		const double HalfH = FMath::DegreesToRadians(0.5 * C->PlayerCameraManager->GetFOVAngle());
		FIntPoint Size(16, 9);
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) { Size = GEngine->GameViewport->Viewport->GetSizeXY(); }
		const double TanH = FMath::Tan(HalfH);
		const double TanV = TanH * Size.Y / FMath::Max(1, Size.X);
		const double HalfCone = FMath::Atan(FMath::Sqrt(TanH * TanH + TanV * TanV));
		auto InCone = [&](const FVector& Centre, double Radius)
		{
			const FVector D = Centre - Eye;
			const double Dist = D.Size();
			if (Dist <= Radius) { return true; }
			const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(D / Dist, Fwd), -1.0, 1.0));
			return Angle - FMath::Asin(FMath::Min(1.0, Radius / Dist)) <= HalfCone;
		};
		int64 Tris = 0;
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			const UStaticMeshComponent* M = *It;
			if (!M || M->GetWorld() != World || !M->IsRegistered() || !M->IsVisible() || M->bHiddenInGame || !M->bRenderInMainPass || !M->GetStaticMesh()
				|| (M->GetOwner() && M->GetOwner()->IsHidden()))
			{
				continue;
			}
			const FStaticMeshRenderData* Data = M->GetStaticMesh()->GetRenderData();
			if (!Data || Data->LODResources.Num() == 0)
			{
				continue;
			}
			const int64 MeshTris = Data->LODResources[0].GetNumTriangles();
			if (const UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(M))
			{
				const FBoxSphereBounds Local = M->GetStaticMesh()->GetBounds();
				for (int32 K = 0; K < I->GetInstanceCount(); ++K)
				{
					FTransform T;
					I->GetInstanceTransform(K, T, true);
					if (InCone(T.TransformPosition(Local.Origin), Local.SphereRadius * T.GetScale3D().GetAbsMax()))
					{
						Tris += MeshTris;
						++OutInstances;
					}
				}
			}
			else if (InCone(M->Bounds.Origin, M->Bounds.SphereRadius))
			{
				Tris += MeshTris;
				++OutInstances;
			}
		}
		return Tris;
	}

	static void StartSample(const FString& Name, int32 Frames)
	{
		GViews.SampleName = Name;
		GViews.SampleLeft = Frames;
		GViews.SampleSkip = 6;
		GViews.Draws.Reset();
		GViews.Prims.Reset();
		GViews.Gpu.Reset();
		GViews.Frame.Reset();
	}

	static void EndSample()
	{
		const int32 PrimsMedian = Median(GViews.Prims);
		GViews.PrimsByPose.Add(GViews.SampleName, PrimsMedian);
		int32 MeshesInView = 0;
		const int64 TrisInView = TrianglesInView(MeshesInView);
		Note(FString::Printf(TEXT("VIEW %s: %d frames, %s, %s; draw calls median %d max %d; primitives drawn median %d max %d; GPU median %.3f ms; frame median %.3f ms; triangles in the view's cone %lld (%d meshes and instances, LOD 0, before occlusion)"),
			*GViews.SampleName, GViews.Prims.Num(), TS() && TS()->IsStopped() ? TEXT("Stopped") : TEXT("Normal time"), *CameraPose(),
			Median(GViews.Draws), Max(GViews.Draws), PrimsMedian, Max(GViews.Prims), Median(GViews.Gpu), Median(GViews.Frame), TrisInView, MeshesInView));
	}

	static bool TickViews(float DeltaTime)
	{
		const double Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
		const double Frame = FApp::GetDeltaTime() * 1000.0;
		if (GViews.SampleLeft > 0)
		{
			if (GViews.SampleSkip > 0)
			{
				--GViews.SampleSkip;
				return true;
			}
			GViews.Draws.Add(GNumDrawCallsRHI[0]);
			GViews.Prims.Add(GNumPrimitivesDrawnRHI[0]);
			GViews.Gpu.Add(Gpu);
			GViews.Frame.Add(Frame);
			if (--GViews.SampleLeft == 0)
			{
				EndSample();
			}
			return true;
		}
		if (GViews.ScansLeft > 0 || GViews.AfterScan >= 0)
		{
			if (GViews.AfterScan >= 0)
			{
				// The scan's own frame and the 5 after it (its GPU work lands a frame or two later).
				GViews.WorstFrame = FMath::Max(GViews.WorstFrame, Frame);
				GViews.WorstGpu = FMath::Max(GViews.WorstGpu, Gpu);
				if (++GViews.AfterScan > 5)
				{
					GViews.ScanFrameMs.Add(GViews.WorstFrame);
					GViews.ScanGpuMs.Add(GViews.WorstGpu);
					GViews.AfterScan = -1;
					GViews.FramesToScan = 40;
				}
				return true;
			}
			if (GViews.FramesToScan > 0)
			{
				if (GViews.FramesToScan <= 20)
				{
					GViews.IdleFrameMs.Add(Frame);
					GViews.IdleGpuMs.Add(Gpu);
				}
				--GViews.FramesToScan;
				return true;
			}
			if (ULidarScanComponent* L = Lidar())
			{
				L->Scan();
			}
			--GViews.ScansLeft;
			GViews.AfterScan = 0;
			GViews.WorstFrame = GViews.WorstGpu = 0.0;
			return true;
		}
		if (!GViews.ShotName.IsEmpty() && !GViews.bShotReady)
		{
			return true;
		}
		if (FPlatformTime::Seconds() < GViews.WaitUntil)
		{
			return true;
		}
		if (GViews.Index >= GViews.Steps.Num())
		{
			Finish();
			return false;
		}
		// A copy: a step may append steps (the area's views are known only once it is ready).
		const FStep Step = GViews.Steps[GViews.Index++];
		const float Wait = Step.Run ? Step.Run() : 0.f;
		GViews.WaitUntil = FPlatformTime::Seconds() + Wait;
		return true;
	}

	static void AddStand(const FPose& Pose)
	{
		GViews.Steps.Add({ Pose.Name, [Pose]()
		{
			if (APawn* P = Pawn(); P && Pose.FromZ >= 0.0)
			{
				P->SetActorLocation(FVector(Pose.X, Pose.Y, Pose.FromZ), false, nullptr, ETeleportType::TeleportPhysics);
			}
			if (!StandAt(Pose.X, Pose.Y, Pose.Look))
			{
				Note(FString::Printf(TEXT("%s: no pawn to stand"), *Pose.Name));
			}
			return 1.0f;
		} });
	}

	// --- The area's views (Brief 16): computed from the plan, so the same poses before and after the modelling ---

	static void SetFlashlight(bool bOn)
	{
		if (const APawn* P = Pawn())
		{
			TArray<USpotLightComponent*> Lights;
			P->GetComponents(Lights);
			for (USpotLightComponent* Light : Lights)
			{
				Light->SetVisibility(bOn);
			}
		}
	}

	static void StopTime()
	{
		if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped())
		{
			// Each view's Stop starts from no stopped time, so the entity stays dormant through the run.
			T->ResetCumulativeStoppedSeconds();
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

	static FVector2D RouteAt(const AAreaInfo& Info, float Distance)
	{
		int32 Segment = -1;
		const FVector P = Info.GetRoutePointAtDistance(FMath::Clamp(Distance, 0.f, Info.GetRouteLength()), Segment);
		return FVector2D(P.X, P.Y);
	}

	/** Two views per segment: from its entry looking along the route, and from its middle looking back at the entry; open segments also look across their space. */
	static TArray<FPose> AreaPoses(bool bAll)
	{
		TArray<FPose> Out;
		const AAreaInfo* Info = AAreaInfo::Find(GameWorld());
		if (!Info)
		{
			return Out;
		}
		for (int32 I = 0; I < Info->Segments.Num(); ++I)
		{
			const FAreaSegmentRecord& Seg = Info->Segments[I];
			const float S0 = Seg.RouteStart;
			const float S1 = FMath::Max(Seg.RouteEnd, S0 + 1.f);
			const FString Base = FString::Printf(TEXT("S%d_%s"), I, LexToString(Seg.Type));
			const FVector2D In = RouteAt(*Info, FMath::Min(S0 + 150.f, S1));
			FVector2D Ahead = RouteAt(*Info, FMath::Min(S0 + 1500.f, S1));
			if (FVector2D::Distance(In, Ahead) < 100.0)
			{
				Ahead = RouteAt(*Info, S1 + 1000.f);
			}
			// Areas' floors are at Z 0: every trace starts at 1 m, under the ceilings and the doors.
			Out.Add({ Base + TEXT("_In"), In.X, In.Y, FVector(Ahead.X, Ahead.Y, EyeLevel), 100.0 });
			if (!bAll)
			{
				continue;
			}
			// A gate's door stands at its middle: its view back is taken a third of the way in.
			const FVector2D Mid = RouteAt(*Info, FMath::Lerp(S0, S1, Seg.Type == EAreaSegmentType::Gate ? 0.3f : 0.5f));
			Out.Add({ Base + TEXT("_Back"), Mid.X, Mid.Y, FVector(In.X, In.Y, EyeLevel), 100.0 });
			if (!Seg.bClosed)
			{
				const FVector C = Seg.Bounds.GetCenter();
				Out.Add({ Base + TEXT("_Across"), In.X, In.Y, FVector(C.X, C.Y, 0.0), 100.0 });
			}
		}
		return Out;
	}

	/** Normal time with the flashlight on, then Stopped with the first scan (the flashlight goes out on the Stop). */
	static void AddShotPair(TArray<FStep>& S, const FString& Label, const FString& Name, bool bResume = true)
	{
		auto AddShot = [&S, Label](const FString& ShotName)
		{
			S.Add({ ShotName, [ShotName, Label]()
			{
				GViews.ShotName = Label + TEXT("_") + ShotName;
				GViews.bShotReady = false;
				FScreenshotRequest::RequestScreenshot(FString(), false, false);
				return 0.2f;
			} });
		};
		S.Add({ TEXT("flashlight"), []() { SetFlashlight(true); return 0.5f; } });
		AddShot(Name + TEXT("_Flashlight"));
		S.Add({ TEXT("stop"), []() { StopTime(); return 2.5f; } });
		AddShot(Name + TEXT("_Scan"));
		if (bResume)
		{
			S.Add({ TEXT("resume"), []() { ResumeTime(); return 1.0f; } });
		}
	}

	static void AddShot(TArray<FStep>& S, const FString& Label, const FString& Name)
	{
		S.Add({ Name, [Name, Label]()
		{
			GViews.ShotName = Label + TEXT("_") + Name;
			GViews.bShotReady = false;
			FScreenshotRequest::RequestScreenshot(FString(), false, false);
			return 0.2f;
		} });
	}

	static void AddSample(TArray<FStep>& S, const FString& Name, int32 Frames)
	{
		S.Add({ Name + TEXT(" sample"), [Name, Frames]() { StartSample(Name, Frames); return 0.f; } });
	}

	/** Each pose sampled in Normal time, then the heaviest one Stopped with a scan alive, and the frame of 8 scans there. */
	static void AddMeasureSteps(TArray<FStep>& S, const TArray<FPose>& Poses)
	{
		for (const FPose& Pose : Poses)
		{
			AddStand(Pose);
			AddSample(S, Pose.Name, 240);
		}
		S.Add({ TEXT("heaviest"), [Poses]()
		{
			const FPose* Best = nullptr;
			int32 BestPrims = -1;
			for (const FPose& Pose : Poses)
			{
				const int32* P = GViews.PrimsByPose.Find(Pose.Name);
				if (P && *P > BestPrims)
				{
					BestPrims = *P;
					Best = &Pose;
				}
			}
			if (!Best)
			{
				return 0.f;
			}
			Note(FString::Printf(TEXT("HEAVIEST %s (%d primitives in Normal time)"), *Best->Name, BestPrims));
			StandAt(Best->X, Best->Y, Best->Look);
			GViews.SampleName = Best->Name;
			return 0.5f;
		} });
		S.Add({ TEXT("stop"), []() { StopTime(); return 2.5f; } });
		S.Add({ TEXT("stopped sample"), []() { StartSample(GViews.SampleName + TEXT("_Stopped_OneScan"), 240); return 0.f; } });
		S.Add({ TEXT("scan frames"), []()
		{
			GViews.ScansLeft = 8;
			GViews.FramesToScan = 40;
			return 0.f;
		} });
		S.Add({ TEXT("scan frames done"), []()
		{
			Note(FString::Printf(TEXT("SCANFRAME %s: %d scans while Stopped; worst of the scan's frame and the 5 after it: frame median %.3f ms (max %.3f), GPU median %.3f ms (max %.3f); idle before each: frame median %.3f ms, GPU median %.3f ms; %d scans alive"),
				*GViews.SampleName, GViews.ScanFrameMs.Num(), Median(GViews.ScanFrameMs), Max(GViews.ScanFrameMs), Median(GViews.ScanGpuMs), Max(GViews.ScanGpuMs),
				Median(GViews.IdleFrameMs), Median(GViews.IdleGpuMs), Lidar() ? Lidar()->GetAliveScanCount() : -1));
			ResumeTime();
			return 1.0f;
		} });
	}

	/** The checkpoint's views in the current area: the first corridor, the first gate's terminal, the first catwalk. */
	static void AddCheckSteps(TArray<FStep>& S, const FString& Label)
	{
		const AAreaInfo* Info = AAreaInfo::Find(GameWorld());
		if (!Info)
		{
			return;
		}
		const FAreaPawnMetrics Metrics = GetAreaPawnMetrics(GameWorld());
		for (int32 I = 0; I < Info->Segments.Num(); ++I)
		{
			const FAreaSegmentRecord& Seg = Info->Segments[I];
			if (Seg.Type != EAreaSegmentType::Corridor)
			{
				continue;
			}
			const float S0 = Seg.RouteStart;
			const FVector2D In = RouteAt(*Info, S0 + 150.f);
			const FVector2D Ahead = RouteAt(*Info, S0 + 1500.f);
			const FVector2D Dir = (Ahead - In).GetSafeNormal();
			const FPose Start{ TEXT("Corridor_FromStart"), In.X, In.Y, FVector(Ahead.X, Ahead.Y, EyeLevel), 100.0 };
			AddStand(Start);
			// The corridor's free width at the start pose: traces across it on the pawn's channel at 0.3, 1.0 and 1.7 m.
			S.Add({ TEXT("free width"), [In, Dir]()
			{
				UWorld* World = GameWorld();
				const FVector2D L(-Dir.Y, Dir.X);
				FString Text;
				double MinWidth = TNumericLimits<double>::Max();
				for (const double Z : { 30.0, 100.0, 170.0 })
				{
					double Width = 0.0;
					for (const double Side : { 1.0, -1.0 })
					{
						const FVector From(In.X, In.Y, Z);
						const FVector To = From + FVector(L.X, L.Y, 0.0) * Side * 2000.0;
						FHitResult Hit;
						FCollisionQueryParams Params(TEXT("TlsFreeWidth"), false, Pawn());
						Width += World && World->LineTraceSingleByChannel(Hit, From, To, ECC_Pawn, Params) ? Hit.Distance : 2000.0;
					}
					MinWidth = FMath::Min(MinWidth, Width);
					Text += FString::Printf(TEXT("%s%.1f cm at %.0f cm"), Text.IsEmpty() ? TEXT("") : TEXT(", "), Width, Z);
				}
				Note(FString::Printf(TEXT("FREEWIDTH corridor at (%.0f, %.0f): %s; least %.1f cm"), In.X, In.Y, *Text, MinWidth));
				// Along the first 20 m every 25 cm at 1 m: the narrowest is the corridor's own width (elsewhere a niche widens it).
				double Narrowest = TNumericLimits<double>::Max();
				FVector2D NarrowAt = In;
				int32 Samples = 0;
				for (double Along = 0.0; Along <= 2000.0; Along += 25.0)
				{
					const FVector2D P = In + Dir * Along;
					double Width = 0.0;
					for (const double Side : { 1.0, -1.0 })
					{
						const FVector From(P.X, P.Y, 100.0);
						FHitResult Hit;
						FCollisionQueryParams Params(TEXT("TlsFreeWidth"), false, Pawn());
						Width += World && World->LineTraceSingleByChannel(Hit, From, From + FVector(L.X, L.Y, 0.0) * Side * 2000.0, ECC_Pawn, Params) ? Hit.Distance : 2000.0;
					}
					++Samples;
					if (Width < Narrowest)
					{
						Narrowest = Width;
						NarrowAt = P;
					}
				}
				Note(FString::Printf(TEXT("FREEWIDTH corridor along 20 m from (%.0f, %.0f), %d samples at 1 m: narrowest %.1f cm at (%.0f, %.0f)"), In.X, In.Y, Samples, Narrowest, NarrowAt.X, NarrowAt.Y));
				return 0.f;
			} });
			AddSample(S, Start.Name, 120);
			AddShotPair(S, Label, Start.Name, false);
			// The same scan from 10 m further along, and a wall from 3 m (the corridor's width less the pawn's radius and a margin).
			const FVector2D Further = In + Dir * 1000.0;
			const FPose Ten{ TEXT("Corridor_ScanFrom10mFurther"), Further.X, Further.Y, FVector(Further.X + Dir.X * 1000.0, Further.Y + Dir.Y * 1000.0, EyeLevel), 100.0 };
			AddStand(Ten);
			AddShot(S, Label, Ten.Name);
			AddSample(S, Ten.Name + TEXT("_Stopped"), 120);
			const FVector2D Left(-Dir.Y, Dir.X);
			const double HalfWidth = 200.0;
			const double Gap = 300.0;
			const FVector2D Stand = In + Dir * 500.0 + Left * (Gap - HalfWidth);
			const FVector2D Wall = In + Dir * 500.0 - Left * HalfWidth;
			const FPose WallPose{ TEXT("Corridor_Wall3m"), Stand.X, Stand.Y, FVector(Wall.X, Wall.Y, Metrics.GetEyeAboveFloor()), 100.0 };
			AddStand(WallPose);
			AddShot(S, Label, WallPose.Name + TEXT("_Scan"));
			S.Add({ TEXT("resume"), []() { ResumeTime(); return 1.0f; } });
			AddStand(WallPose);
			S.Add({ TEXT("flashlight"), []() { SetFlashlight(true); return 0.5f; } });
			AddShot(S, Label, WallPose.Name + TEXT("_Flashlight"));
			break;
		}
		// The first gate's terminal: from its standing place, looking at it.
		for (TActorIterator<APowerTerminal> It(GameWorld()); It; ++It)
		{
			const APowerTerminal* T = *It;
			bool bGate = false;
			for (const APoweredDevice* Device : T->Targets)
			{
				bGate |= Device && Device->IsA<ADoorDevice>();
			}
			if (!bGate)
			{
				continue;
			}
			const FVector Front = T->GetActorLocation() + T->GetActorForwardVector() * 180.0 + T->GetActorRightVector() * 60.0;
			const FPose Pose{ TEXT("GateTerminal"), Front.X, Front.Y, T->GetActorLocation() + FVector(0.0, 0.0, 80.0), 100.0 };
			AddStand(Pose);
			AddSample(S, Pose.Name, 120);
			AddShotPair(S, Label, Pose.Name);
			// Further back (2.6 m), where the flashlight does not wash the body out.
			const FVector Back = T->GetActorLocation() + T->GetActorForwardVector() * 260.0 + T->GetActorRightVector() * 60.0;
			const FPose Far{ TEXT("GateTerminal_2m6"), Back.X, Back.Y, T->GetActorLocation() + FVector(0.0, 0.0, 80.0), 100.0 };
			AddStand(Far);
			AddShotPair(S, Label, Far.Name);
			break;
		}
		for (int32 I = 0; I < Info->Segments.Num(); ++I)
		{
			const FAreaSegmentRecord& Seg = Info->Segments[I];
			if (Seg.Type != EAreaSegmentType::Catwalk)
			{
				continue;
			}
			const FVector2D In = RouteAt(*Info, Seg.RouteStart + 150.f);
			const FVector2D Ahead = RouteAt(*Info, FMath::Min(Seg.RouteStart + 1500.f, Seg.RouteEnd));
			const FPose Pose{ TEXT("Catwalk_FromStart"), In.X, In.Y, FVector(Ahead.X, Ahead.Y, Metrics.GetEyeAboveFloor() - 60.0), 100.0 };
			AddStand(Pose);
			AddSample(S, Pose.Name, 120);
			AddShotPair(S, Label, Pose.Name);
			break;
		}
	}

	// --- The halls' views (Brief 19) ---

	static void ExecCommand(const FString& Command)
	{
		if (GEngine && GameWorld())
		{
			GEngine->Exec(GameWorld(), *Command);
		}
	}

	/** The beacons' views: each powered, from its entrance and from under it with the flashlight, its body and rod in a scan; then the same two views at a wider cone and at a lower intensity, and the values put back. */
	static void AddBeaconSteps(TArray<FStep>& S, const FString& Label)
	{
		const AAreaGenerator* Gen = AAreaGenerator::Find(GameWorld());
		int32 Beacons = 0;
		for (TActorIterator<ABeaconBody> It(GameWorld()); It; ++It)
		{
			++Beacons;
		}
		if (!Gen || Beacons == 0)
		{
			Note(TEXT("BEACONS none in this area"));
			return;
		}
		const float Cone = Gen->BeaconConeAngle;
		const float Intensity = Gen->BeaconIntensity;
		// One wider cone and one lower intensity to compare with today's values.
		const float WideCone = FMath::Min(45.f, Cone * 2.f);
		const float LowIntensity = Intensity * 0.3f;
		struct FVariant { FString Name; float Cone; float Intensity; bool bScan; };
		const TArray<FVariant> Variants = {
			{ FString::Printf(TEXT("Today_%.0fdeg_%.0fcd"), Cone, Intensity), Cone, Intensity, true },
			{ FString::Printf(TEXT("WideCone_%.0fdeg_%.0fcd"), WideCone, Intensity), WideCone, Intensity, false },
			{ FString::Printf(TEXT("LowIntensity_%.0fdeg_%.0fcd"), Cone, LowIntensity), Cone, LowIntensity, false } };
		for (const FVariant& V : Variants)
		{
			S.Add({ TEXT("beacon light"), [V]() { ExecCommand(FString::Printf(TEXT("tls.Beacon.Light %.2f %.0f"), V.Cone, V.Intensity)); return 0.3f; } });
			for (int32 B = 0; B < Beacons; ++B)
			{
				const FString Base = FString::Printf(TEXT("Beacon%d_%s"), B, *V.Name);
				S.Add({ TEXT("beacon on"), [B]() { ExecCommand(FString::Printf(TEXT("tls.Beacon.Power %d on"), B)); return 0.3f; } });
				for (const TCHAR* Where : { TEXT("entrance"), TEXT("under") })
				{
					const FString Name = FString::Printf(TEXT("%s_%s"), *Base, Where);
					const FString W(Where);
					S.Add({ Name, [B, W]()
					{
						if (W == TEXT("entrance"))
						{
							ExecCommand(FString::Printf(TEXT("tls.Beacon.View %d entrance"), B));
						}
						else
						{
							// Under it: on the floor nearest the point under its mount, 3 m along that floor from it, looking at the head.
							TArray<ABeaconBody*> Bodies;
							for (TActorIterator<ABeaconBody> It(GameWorld()); It; ++It) { Bodies.Add(*It); }
							Bodies.Sort([](const ABeaconBody& X, const ABeaconBody& Y) { return X.Segment < Y.Segment; });
							const AAreaInfo* Info = AAreaInfo::Find(GameWorld());
							if (Bodies.IsValidIndex(B) && Info)
							{
								const FVector Mount = Bodies[B]->GetActorLocation();
								const FVector2D M(Mount);
								double Best = TNumericLimits<double>::Max();
								FVector2D Stand = M;
								for (const FAreaFloorRect& R : Info->FloorRects)
								{
									const FVector2D Lo(R.Box.Min), Hi(R.Box.Max);
									const FVector2D C(FMath::Clamp(M.X, Lo.X + 60.0, Hi.X - 60.0), FMath::Clamp(M.Y, Lo.Y + 60.0, Hi.Y - 60.0));
									const double D = FVector2D::Distance(C, M);
									if (D < Best)
									{
										Best = D;
										// 3 m along the floor's longer side, so the head is not straight above the eye.
										const bool bAlongX = Hi.X - Lo.X >= Hi.Y - Lo.Y;
										const FVector2D Step = bAlongX ? FVector2D(C.X + 300.0 <= Hi.X - 60.0 ? 300.0 : -300.0, 0.0) : FVector2D(0.0, C.Y + 300.0 <= Hi.Y - 60.0 ? 300.0 : -300.0);
										Stand = C + Step;
									}
								}
								Note(FString::Printf(TEXT("BEACON %d below: mount (%.0f, %.0f, %.0f), standing at (%.0f, %.0f), %.1f m from the point under the mount"), B, Mount.X, Mount.Y, Mount.Z, Stand.X, Stand.Y, FVector2D::Distance(Stand, M) / 100.0));
								if (APawn* P = Pawn())
								{
									P->SetActorLocation(FVector(Stand.X, Stand.Y, 100.0), false, nullptr, ETeleportType::TeleportPhysics);
								}
								StandAt(Stand.X, Stand.Y, Mount - FVector(0.0, 0.0, 30.0));
							}
						}
						SetFlashlight(true);
						return 1.0f;
					} });
					AddSample(S, Name, 120);
					AddShot(S, Label, Name + TEXT("_Flashlight"));
					if (V.bScan && W == TEXT("under"))
					{
						// The body and the rod in a scan (the beacon's light goes out with time, as every light).
						S.Add({ TEXT("stop"), []() { StopTime(); return 2.5f; } });
						AddShot(S, Label, Name + TEXT("_Scan"));
						S.Add({ TEXT("resume"), []() { ResumeTime(); return 1.0f; } });
						S.Add({ TEXT("beacon on again"), [B]() { ExecCommand(FString::Printf(TEXT("tls.Beacon.Power %d on"), B)); return 0.3f; } });
					}
				}
			}
		}
		S.Add({ TEXT("beacon light back"), [Cone, Intensity]() { ExecCommand(FString::Printf(TEXT("tls.Beacon.Light %.2f %.0f"), Cone, Intensity)); return 0.3f; } });
	}

	/** The checkpoint's views of each hall: from its entrance, a web's walkway from 20 m further and a platform from 5 m, a floor hall's edge from 5 m, the joints with the segments before and after it. */
	static void AddHallSteps(TArray<FStep>& S, const FString& Label)
	{
		const AAreaInfo* Info = AAreaInfo::Find(GameWorld());
		if (!Info)
		{
			return;
		}
		const double Eye = GetAreaPawnMetrics(GameWorld()).GetEyeAboveFloor();
		int32 K = 0;
		for (int32 I = 0; I < Info->Segments.Num(); ++I)
		{
			const FAreaSegmentRecord& Seg = Info->Segments[I];
			if (Seg.Type != EAreaSegmentType::Hall)
			{
				continue;
			}
			// The route's points in the hall: the joint, then (a web) its platforms' centres in route order.
			TArray<FVector2D> Pts;
			for (int32 P = 0; P < Info->RoutePoints.Num() && P < Info->RoutePointSegments.Num(); ++P)
			{
				if (Info->RoutePointSegments[P] == I) { Pts.Add(FVector2D(Info->RoutePoints[P])); }
			}
			// The plan has one segment per recipe entry, in order.
			const AAreaGenerator* Gen = AAreaGenerator::Find(GameWorld());
			const FAreaRecipe* Recipe = Gen ? Gen->GetRecipe() : nullptr;
			const bool bWeb = Recipe && Recipe->Segments.IsValidIndex(I) && Recipe->Segments[I].bWeb;
			const FString Base = FString::Printf(TEXT("Hall%d_%s"), K++, bWeb ? TEXT("Web") : TEXT("Floor"));
			const float S0 = Seg.RouteStart;
			const float S1 = FMath::Max(Seg.RouteEnd, S0 + 1.f);
			Note(FString::Printf(TEXT("HALL %s: segment %d, %.0f x %.0f m, route %.1f-%.1f m, %d route points"), *Base, I,
				Seg.Bounds.GetSize().X / 100.0, Seg.Bounds.GetSize().Y / 100.0, S0 / 100.0, S1 / 100.0, Pts.Num()));
			const FVector2D In = RouteAt(*Info, S0 + 150.f);
			const FVector2D Ahead = RouteAt(*Info, FMath::Min(S0 + 1500.f, S1));
			const FPose Entrance{ Base + TEXT("_FromEntrance"), In.X, In.Y, FVector(Ahead.X, Ahead.Y, Eye), 100.0 };
			AddStand(Entrance);
			AddSample(S, Entrance.Name, 120);
			AddShotPair(S, Label, Entrance.Name, !bWeb);
			if (bWeb)
			{
				// The same scan from 20 m further along the walkway.
				const FVector2D Far = RouteAt(*Info, FMath::Min(S0 + 2150.f, S1));
				const FVector2D FarAhead = RouteAt(*Info, FMath::Min(S0 + 3500.f, S1));
				const FPose Further{ Base + TEXT("_ScanFrom20mFurther"), Far.X, Far.Y, FVector(FarAhead.X, FarAhead.Y, Eye), 100.0 };
				AddStand(Further);
				AddShot(S, Label, Further.Name);
				S.Add({ TEXT("resume"), []() { ResumeTime(); return 1.0f; } });
				// The second platform on the route, from 5 m off its edge, on the walkway that reaches it.
				if (Pts.Num() >= 3)
				{
					const FVector2D Node = Pts[2];
					const FVector2D Back = (Pts[1] - Pts[2]).GetSafeNormal();
					const double HalfNode = 0.5 * (Gen ? Gen->HallNodeSize : 600.f);
					const FVector2D From = Node + Back * (HalfNode + 500.0);
					const FPose Platform{ Base + TEXT("_Platform5m"), From.X, From.Y, FVector(Node.X, Node.Y, 0.0), 100.0 };
					AddStand(Platform);
					AddSample(S, Platform.Name, 120);
					AddShotPair(S, Label, Platform.Name);
				}
			}
			else
			{
				// The floor's nearest edge to the middle of the route through it, from 5 m inside, looking at the edge.
				const FVector2D Mid = RouteAt(*Info, 0.5f * (S0 + S1));
				const FBox2D B(FVector2D(Seg.Bounds.Min), FVector2D(Seg.Bounds.Max));
				const double D[4] = { Mid.X - B.Min.X, B.Max.X - Mid.X, Mid.Y - B.Min.Y, B.Max.Y - Mid.Y };
				int32 Side = 0;
				for (int32 J = 1; J < 4; ++J) { Side = D[J] < D[Side] ? J : Side; }
				const FVector2D Edge = Side == 0 ? FVector2D(B.Min.X, Mid.Y) : Side == 1 ? FVector2D(B.Max.X, Mid.Y) : Side == 2 ? FVector2D(Mid.X, B.Min.Y) : FVector2D(Mid.X, B.Max.Y);
				const FVector2D Inward = Side == 0 ? FVector2D(1, 0) : Side == 1 ? FVector2D(-1, 0) : Side == 2 ? FVector2D(0, 1) : FVector2D(0, -1);
				const FVector2D From = Edge + Inward * 500.0;
				const FPose EdgePose{ Base + TEXT("_Edge5m"), From.X, From.Y, FVector(Edge.X, Edge.Y, 0.0), 100.0 };
				AddStand(EdgePose);
				AddSample(S, EdgePose.Name, 120);
				AddShotPair(S, Label, EdgePose.Name);
			}
			// Where the segment before meets the hall, seen from 5 m inside it, and where the hall meets the one after.
			const FVector2D EntryFrom = RouteAt(*Info, FMath::Min(S0 + 500.f, S1));
			const FVector2D EntryAt = RouteAt(*Info, FMath::Max(S0 - 300.f, 0.f));
			AddStand({ Base + TEXT("_JointIn"), EntryFrom.X, EntryFrom.Y, FVector(EntryAt.X, EntryAt.Y, Eye), 100.0 });
			AddShotPair(S, Label, Base + TEXT("_JointIn"));
			const FVector2D ExitFrom = RouteAt(*Info, FMath::Max(S1 - 500.f, S0));
			const FVector2D ExitAt = RouteAt(*Info, S1 + 300.f);
			AddStand({ Base + TEXT("_JointOut"), ExitFrom.X, ExitFrom.Y, FVector(ExitAt.X, ExitAt.Y, Eye), 100.0 });
			AddShotPair(S, Label, Base + TEXT("_JointOut"));
		}
	}

	// --- The backdrop's views (Brief 28) ---

	/** The pawn flying, its eye at Eye, looking at Look (views off the walkable floors: a mass from 15 m, a pillar's top from 4 m). */
	static void HoverAt(const FVector& Eye, const FVector& Look)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		if (!P || !C)
		{
			return;
		}
		ACharacter* Character = Cast<ACharacter>(P);
		if (Character)
		{
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		}
		const FVector Offset = P->GetPawnViewLocation() - P->GetActorLocation();
		P->SetActorLocation(Eye - Offset, false, nullptr, ETeleportType::TeleportPhysics);
		if (Character)
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		C->SetControlRotation((Look - Eye).Rotation());
	}

	/**
	 * Brief 28's views: from the entrance of the area's largest open segment looking ahead, up and down; a mass of each
	 * family the area has from 15 m; a pillar's top under its frame from 4 m. Each with the flashlight and in a scan.
	 */
	static void AddBackdropSteps(TArray<FStep>& S, const FString& Label)
	{
		const AAreaInfo* Info = AAreaInfo::Find(GameWorld());
		const AAreaGenerator* Gen = AAreaGenerator::Find(GameWorld());
		if (!Info || !Gen)
		{
			return;
		}
		int32 Largest = -1;
		double Area = 0.0;
		for (int32 I = 0; I < Info->Segments.Num(); ++I)
		{
			const FAreaSegmentRecord& Seg = Info->Segments[I];
			const double A = Seg.Bounds.GetSize().X * Seg.Bounds.GetSize().Y;
			if (!Seg.bClosed && A > Area)
			{
				Area = A;
				Largest = I;
			}
		}
		if (Largest >= 0)
		{
			const FAreaSegmentRecord& Seg = Info->Segments[Largest];
			const float S0 = Seg.RouteStart;
			const float S1 = FMath::Max(Seg.RouteEnd, S0 + 1.f);
			const FVector2D In = RouteAt(*Info, S0 + 150.f);
			const FVector2D Ahead = RouteAt(*Info, FMath::Min(S0 + 1500.f, S1));
			const FString Base = FString::Printf(TEXT("S%d_%s"), Largest, LexToString(Seg.Type));
			Note(FString::Printf(TEXT("LARGEST open segment %d %s, %.0f x %.0f m, entrance (%.0f, %.0f)"), Largest, LexToString(Seg.Type), Seg.Bounds.GetSize().X / 100.0, Seg.Bounds.GetSize().Y / 100.0, In.X, In.Y));
			for (const TPair<const TCHAR*, double>& Look : { TPair<const TCHAR*, double>(TEXT("Ahead"), 0.0), TPair<const TCHAR*, double>(TEXT("Up"), 55.0), TPair<const TCHAR*, double>(TEXT("Down"), -55.0) })
			{
				const FString Name = Base + TEXT("_Entrance_") + Look.Key;
				AddStand({ Name, In.X, In.Y, FVector(Ahead.X, Ahead.Y, EyeLevel), 100.0 });
				const double Pitch = Look.Value;
				S.Add({ TEXT("pitch"), [Pitch]()
				{
					if (APlayerController* C = PC())
					{
						FRotator R = C->GetControlRotation();
						R.Pitch = Pitch;
						C->SetControlRotation(R);
					}
					return 0.3f;
				} });
				AddShotPair(S, Label, Name);
			}
		}
		for (const AAreaGenerator::FDecorShotPick& Pick : Gen->DecorMassPicks)
		{
			const FVector Eye = Pick.At + Pick.Normal * 1500.0;
			const FString Name = FString::Printf(TEXT("Mass_%s_15m"), *Pick.Family);
			Note(FString::Printf(TEXT("MASS %s: module at %s facing %s, seen from 15 m"), *Pick.Family, *Pick.At.ToCompactString(), *Pick.Normal.ToCompactString()));
			S.Add({ Name, [Eye, Pick]() { HoverAt(Eye, Pick.At); return 0.5f; } });
			AddShotPair(S, Label, Name);
		}
		if (!Gen->DecorPillarPick.Family.IsEmpty())
		{
			const FVector Tip = Gen->DecorPillarPick.At;
			const FVector Along = Gen->DecorPillarPick.Normal;
			const FVector Across(-Along.Y, Along.X, 0.0);
			const FVector Eye = Tip + (Along + Across).GetSafeNormal() * 400.0 - FVector(0.0, 0.0, 100.0);
			Note(FString::Printf(TEXT("PILLAR top at %s, seen from 4 m"), *Tip.ToCompactString()));
			S.Add({ TEXT("Pillar_Top_4m"), [Eye, Tip]() { HoverAt(Eye, Tip); return 0.5f; } });
			AddShotPair(S, Label, TEXT("Pillar_Top_4m"));
		}
		S.Add({ TEXT("walk again"), []()
		{
			if (ACharacter* Character = Cast<ACharacter>(Pawn()))
			{
				Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			}
			if (const AAreaInfo* I = AAreaInfo::Find(GameWorld()))
			{
				const FVector2D Start = RouteAt(*I, 150.f);
				const FVector2D Ahead = RouteAt(*I, 1500.f);
				if (APawn* P = Pawn())
				{
					P->SetActorLocation(FVector(Start.X, Start.Y, 100.0), false, nullptr, ETeleportType::TeleportPhysics);
				}
				StandAt(Start.X, Start.Y, FVector(Ahead.X, Ahead.Y, EyeLevel));
			}
			return 0.5f;
		} });
	}

	/** Waits for the area to be ready (repeating itself), then appends the set's steps. */
	static void AddAreaSteps(const FString& Set, const FString& Label)
	{
		GViews.ShotFolder = Set == TEXT("areabackdrop") ? TEXT("Brief28") : Set.StartsWith(TEXT("hall")) ? TEXT("Brief19") : TEXT("Brief16");
		GViews.Steps.Add({ TEXT("wait for the area"), [Set, Label]()
		{
			UWorld* World = GameWorld();
			if (!World || !AAreaGenerator::Find(World) || !AAreaGenerator::IsAreaReady(World) || !Pawn())
			{
				--GViews.Index;
				return 0.5f;
			}
			if (!GShotHandle.IsValid())
			{
				GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
			}
			const AAreaGenerator* Gen = AAreaGenerator::Find(World);
			Note(FString::Printf(TEXT("area ready: act %d, seed %d, hash %016llx, %d PCG instances"), Gen->GetAct(), Gen->GetSeed(), static_cast<uint64>(Gen->GetLayoutHash()), Gen->GetPCGInstanceCount()));
			SetFlashlight(true);
			if (Set == TEXT("area"))
			{
				AddMeasureSteps(GViews.Steps, AreaPoses(true));
			}
			else if (Set == TEXT("areashots"))
			{
				for (const FPose& Pose : AreaPoses(false))
				{
					AddStand(Pose);
					AddShotPair(GViews.Steps, Label, Pose.Name);
				}
			}
			else if (Set == TEXT("areawalls"))
			{
				// The most squeezed and the most stretched wall panel, seen square from 2.5 m in front of it.
				for (const TPair<const TCHAR*, AAreaGenerator::FDressPanelPick>& Pick : { TPair<const TCHAR*, AAreaGenerator::FDressPanelPick>(TEXT("MostSqueezed"), Gen->DressMostSqueezedPanel),
					TPair<const TCHAR*, AAreaGenerator::FDressPanelPick>(TEXT("MostStretched"), Gen->DressMostStretchedPanel) })
				{
					const AAreaGenerator::FDressPanelPick P = Pick.Value;
					if (P.Facing.IsNearlyZero())
					{
						continue;
					}
					const FVector2D Stand = P.At + P.Facing * 250.0;
					const FString Name = FString::Printf(TEXT("Wall%s_x%.3f"), Pick.Key, P.Factor);
					Note(FString::Printf(TEXT("WALL %s: panel at (%.0f, %.0f) facing (%.0f, %.0f), factor along x%.3f"), *Name, P.At.X, P.At.Y, P.Facing.X, P.Facing.Y, P.Factor));
					AddStand({ Name, Stand.X, Stand.Y, FVector(P.At.X, P.At.Y, 180.0), 100.0 });
					AddShotPair(GViews.Steps, Label, Name);
				}
			}
			else if (Set == TEXT("areamouth"))
			{
				// The first niche or dead-end mouth a conduit run crosses: from 4.5 m along the corridor on the far side,
				// looking at the run across the mouth, and square on from the corridor's far side.
				const AAreaGenerator::FDressPanelPick M = Gen->DressFirstConduitMouth;
				if (!M.Facing.IsNearlyZero())
				{
					const FVector2D Along(-M.Facing.Y, M.Facing.X);
					Note(FString::Printf(TEXT("MOUTH conduit across a mouth %.0f cm wide at (%.0f, %.0f), facing (%.0f, %.0f)"), M.Factor, M.At.X, M.At.Y, M.Facing.X, M.Facing.Y));
					const FVector2D Oblique = M.At + M.Facing * 300.0 + Along * 450.0;
					AddStand({ TEXT("Mouth_Along"), Oblique.X, Oblique.Y, FVector(M.At.X, M.At.Y, 300.0), 100.0 });
					AddShotPair(GViews.Steps, Label, TEXT("Mouth_Along"));
					const FVector2D Square = M.At + M.Facing * 320.0;
					AddStand({ TEXT("Mouth_Square"), Square.X, Square.Y, FVector(M.At.X, M.At.Y, 260.0), 100.0 });
					AddShotPair(GViews.Steps, Label, TEXT("Mouth_Square"));
				}
				else
				{
					Note(TEXT("MOUTH none: no conduit run crosses a mouth in this area"));
				}
			}
			else if (Set == TEXT("hallcheck"))
			{
				AddHallSteps(GViews.Steps, Label);
				AddBeaconSteps(GViews.Steps, Label);
			}
			else if (Set == TEXT("halls"))
			{
				AddHallSteps(GViews.Steps, Label);
			}
			else if (Set == TEXT("hallbeacons"))
			{
				AddBeaconSteps(GViews.Steps, Label);
			}
			else if (Set == TEXT("areabackdrop"))
			{
				AddBackdropSteps(GViews.Steps, Label);
			}
			else
			{
				AddCheckSteps(GViews.Steps, Label);
			}
			return 0.5f;
		} });
	}

	static void StartViews(const FString& Set, const FString& Label, bool bQuit)
	{
		if (GViews.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GViews.Ticker);
		}
		GViews = FViewsRun();
		GViews.Label = Label;
		GViews.bQuit = bQuit;
		TArray<FStep>& S = GViews.Steps;
		if (Set.StartsWith(TEXT("area")) || Set.StartsWith(TEXT("hall")))
		{
			// Its steps are appended once the area is ready, after the two below.
			AddAreaSteps(Set, Label);
		}
		S.Add({ TEXT("wait for the pawn"), []()
		{
			return Pawn() && TS() ? 1.0f : 0.5f;
		} });
		S.Add({ TEXT("pawn ready"), []()
		{
			if (!Pawn())
			{
				Note(TEXT("no pawn"));
				GViews.Index = GViews.Steps.Num();
				return 0.f;
			}
			if (GEngine)
			{
				// Uncapped, so the GPU time is that of the view and not of the cap.
				GEngine->Exec(GameWorld(), TEXT("t.MaxFPS 0"));
				GEngine->Exec(GameWorld(), TEXT("r.VSync 0"));
			}
			Note(FString::Printf(TEXT("start, %s, viewport %s"), *CameraPose(),
				GEngine && GEngine->GameViewport ? *GEngine->GameViewport->Viewport->GetSizeXY().ToString() : TEXT("-")));
			return 0.5f;
		} });
		if (Set == TEXT("shots") || Set == TEXT("arrivalshots"))
		{
			if (!GShotHandle.IsValid())
			{
				GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
			}
			for (const FPose& Pose : ShotPoses(Set == TEXT("shots")))
			{
				AddStand(Pose);
				auto AddShotStep = [&S, Label](const FString& Name)
				{
					S.Add({ Name, [Name, Label]()
					{
						GViews.ShotName = Label + TEXT("_") + Name;
						GViews.bShotReady = false;
						FScreenshotRequest::RequestScreenshot(FString(), false, false);
						return 0.2f;
					} });
				};
				AddShotStep(Pose.Name + TEXT("_Normal"));
				// Stopped: the first scan fires about 1 s after the Stop, its lines grow over the link time.
				S.Add({ TEXT("stop"), []()
				{
					if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped())
					{
						T->Stop();
					}
					return 2.5f;
				} });
				AddShotStep(Pose.Name + TEXT("_Scan"));
				S.Add({ TEXT("resume"), []()
				{
					if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
					{
						T->Resume();
					}
					return 1.0f;
				} });
			}
		}
		else if (Set == TEXT("lights"))
		{
			if (!GShotHandle.IsValid())
			{
				GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
			}
			TArray<FPose> Poses = LightPoses();
			for (const FPose& Pose : Poses)
			{
				AddStand(Pose);
				S.Add({ Pose.Name + TEXT(" shot"), [Pose, Label]()
				{
					GViews.ShotName = Label + TEXT("_Light_") + Pose.Name;
					GViews.bShotReady = false;
					FScreenshotRequest::RequestScreenshot(FString(), false, false);
					return 0.2f;
				} });
			}
			// Brief 13's lit view of the home (BugItGo 80 -250 150 -8 25 0: pitch -8, yaw 25), measured the same way; BugItGo
			// does nothing in a package, so the pawn stands there and looks 10 m along that direction.
			S.Add({ TEXT("Brief13 view"), []()
			{
				APawn* P = Pawn();
				if (P && StandAt(80.0, -250.0, FVector(0.0, 0.0, EyeLevel)))
				{
					const FVector Eye = P->GetPawnViewLocation();
					StandAt(80.0, -250.0, Eye + FRotator(-8.0, 25.0, 0.0).Vector() * 1000.0);
				}
				return 1.0f;
			} });
			S.Add({ TEXT("Brief13 view shot"), [Label]()
			{
				GViews.ShotName = Label + TEXT("_Light_Brief13View");
				GViews.bShotReady = false;
				FScreenshotRequest::RequestScreenshot(FString(), false, false);
				return 0.2f;
			} });
		}
		else if (Set == TEXT("terminals"))
		{
			// Every terminal showing the modelled body, from 1.8 m in front of it and 0.6 m to its side, in the map's own
			// light and with the flashlight (switched on by the test even where the player has none, as at home).
			GViews.ShotFolder = TEXT("Brief16");
			if (!GShotHandle.IsValid())
			{
				GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
			}
			S.Add({ TEXT("terminals"), [Label]()
			{
				int32 N = 0;
				for (TActorIterator<APowerTerminal> It(GameWorld()); It; ++It)
				{
					const APowerTerminal* T = *It;
					if (!T->bModelledBody)
					{
						continue;
					}
					const FString Name = FString::Printf(TEXT("Terminal%d_%s"), N++, *T->GetActorNameOrLabel());
					Note(FString::Printf(TEXT("TERMINAL %s at %s yaw %.0f"), *Name, *T->GetActorLocation().ToCompactString(), T->GetActorRotation().Yaw));
					const FVector Head = T->GetActorLocation() + FVector(0.0, 0.0, 80.0);
					for (const double Out : { 180.0, 260.0 })
					{
						// To its right, or to its left when something stands between that place and the terminal.
						FVector Front = T->GetActorLocation() + T->GetActorForwardVector() * Out + T->GetActorRightVector() * 60.0;
						FHitResult Hit;
						FCollisionQueryParams Params(TEXT("TlsTerminalView"), false, Pawn());
						Params.AddIgnoredActor(T);
						if (GameWorld()->LineTraceSingleByChannel(Hit, Front + FVector(0.0, 0.0, 150.0), Head, ECC_Visibility, Params))
						{
							Front = T->GetActorLocation() + T->GetActorForwardVector() * Out - T->GetActorRightVector() * 60.0;
						}
						const FString Shot = FString::Printf(TEXT("%s_%dcm"), *Name, FMath::RoundToInt(Out));
						AddStand({ Shot, Front.X, Front.Y, Head, 100.0 });
						GViews.Steps.Add({ TEXT("light off"), []() { SetFlashlight(false); return 0.5f; } });
						AddShot(GViews.Steps, Label, Shot + TEXT("_Normal"));
						GViews.Steps.Add({ TEXT("flashlight"), []() { SetFlashlight(true); return 0.5f; } });
						AddShot(GViews.Steps, Label, Shot + TEXT("_Flashlight"));
					}
				}
				return 0.5f;
			} });
		}
		else if (!Set.StartsWith(TEXT("area")) && !Set.StartsWith(TEXT("hall")))
		{
			AddMeasureSteps(S, HeavyPoses(Set == TEXT("home")));
		}
		Note(FString::Printf(TEXT("===== %s: %d steps ====="), *Set, S.Num()));
		GViews.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickViews));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsRenderStatsCommand(
	TEXT("tls.RenderStats"),
	TEXT("tls.RenderStats [Frames=120] [Label]: logs the median and the maximum of the draw calls and of the primitives (triangles) the RHI drew per frame over the next Frames frames (after skipping 4), with the camera's pose."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace RenderStatsDebug;
		if (GStats.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GStats.Ticker);
		}
		GStats = FStatsRun();
		GStats.Frames = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 5000) : 120;
		GStats.Label = Args.Num() > 1 ? Args[1] : TEXT("view");
		GStats.Skip = 4;
		GStats.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&RenderStatsDebug::TickStats));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsStandAtCommand(
	TEXT("tls.StandAt"),
	TEXT("tls.StandAt <X> <Y> <LookX> <LookY> [LookZ]: stands the player's pawn on the floor under (X, Y) and turns it to look at the point (without LookZ: level, at eye height)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace RenderStatsDebug;
		if (Args.Num() < 4)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.StandAt <X> <Y> <LookX> <LookY> [LookZ]"));
			return;
		}
		const FVector Look(FCString::Atod(*Args[2]), FCString::Atod(*Args[3]), Args.Num() > 4 ? FCString::Atod(*Args[4]) : EyeLevel);
		const bool bDone = StandAt(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), Look);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.StandAt: %s, %s"), bDone ? TEXT("done") : TEXT("no pawn"), *CameraPose());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsViewsCommand(
	TEXT("tls.Views"),
	TEXT("tls.Views <home|arrival|lights|shots|arrivalshots|area|areashots|areacheck|areawalls|areamouth|terminals|halls|hallbeacons|hallcheck|areabackdrop> [Label=Run] [Quit=0]: (PIE or a package) fixed views of Prologue_Home (home, lights, shots) or Prologue_Arrival (arrival, arrivalshots), or of the current area computed from its plan (area: two or three views per segment measured like home; areashots: each segment from its entry with the flashlight and in a scan; areacheck: Brief 16's checkpoint views of the first corridor, gate terminal and catwalk; areawalls: the most squeezed and the most stretched wall panel from 2.5 m, with the flashlight and in a scan; areamouth: the first niche or dead-end mouth a conduit run crosses, along the corridor and square on, with the flashlight and in a scan; terminals (any map): every terminal with the modelled body from 1.8 and 2.6 m, in the map's light and with the flashlight; shots to Saved/Screenshots/Brief16); halls (Brief 19, any area): each hall from its entrance with the flashlight and in a scan (a web hall's scan also from 20 m further along the walkway), a web hall's second platform on the route from 5 m, a floor hall's edge from 5 m, and the joints with the segments before and after it; hallbeacons: each beacon powered, from its entrance and from under it with the flashlight and its body and rod in a scan, at today's cone and intensity, at twice the cone and at 0.3 times the intensity, then the values put back; hallcheck: both; shots to Saved/Screenshots/Brief19; areabackdrop (Brief 28, any area): the largest open segment from its entrance looking ahead, up and down, a mass of each family from 15 m and a pillar's top under its frame from 4 m (the pawn flying), each with the flashlight and in a scan, shots to Saved/Screenshots/Brief28. shots/arrivalshots: each space's view in normal light and with a scan. home/arrival: each view's draw calls, primitives and GPU time over 240 frames, uncapped; at the heaviest view the same while Stopped with a scan alive, and the frame of 8 scans. lights: Prologue_Home from the entrance of each of its four spaces at eye height, and Brief 13's lit view: mean level and share of pixels under 20 levels. Shots to Saved/Screenshots/Brief15."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString Set = Args.Num() > 0 ? Args[0].ToLower() : TEXT("home");
		RenderStatsDebug::StartViews(Set, Args.Num() > 1 ? Args[1] : TEXT("Run"), Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0);
	}));

#endif

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

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Components/CapsuleComponent.h"
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
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief15"), GViews.ShotName + TEXT(".png")));
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
		Note(FString::Printf(TEXT("VIEW %s: %d frames, %s, %s; draw calls median %d max %d; primitives drawn median %d max %d; GPU median %.3f ms; frame median %.3f ms"),
			*GViews.SampleName, GViews.Prims.Num(), TS() && TS()->IsStopped() ? TEXT("Stopped") : TEXT("Normal time"), *CameraPose(),
			Median(GViews.Draws), Max(GViews.Draws), PrimsMedian, Max(GViews.Prims), Median(GViews.Gpu), Median(GViews.Frame)));
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
		const FStep& Step = GViews.Steps[GViews.Index++];
		const float Wait = Step.Run ? Step.Run() : 0.f;
		GViews.WaitUntil = FPlatformTime::Seconds() + Wait;
		return true;
	}

	static void AddStand(const FPose& Pose)
	{
		GViews.Steps.Add({ Pose.Name, [Pose]()
		{
			if (!StandAt(Pose.X, Pose.Y, Pose.Look))
			{
				Note(FString::Printf(TEXT("%s: no pawn to stand"), *Pose.Name));
			}
			return 1.0f;
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
		else
		{
			const bool bHome = Set == TEXT("home");
			const TArray<FPose> Poses = HeavyPoses(bHome);
			for (const FPose& Pose : Poses)
			{
				AddStand(Pose);
				S.Add({ Pose.Name + TEXT(" sample"), [Pose]() { StartSample(Pose.Name, 240); return 0.f; } });
			}
			// The heaviest view, Stopped with the first scan alive, then the frame of a scan there.
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
			S.Add({ TEXT("stop"), []()
			{
				if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped())
				{
					T->Stop();
				}
				return 2.5f;
			} });
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
				if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
				{
					T->Resume();
				}
				return 1.0f;
			} });
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
	TEXT("tls.Views <home|arrival|lights|shots|arrivalshots> [Label=Run] [Quit=0]: (PIE or a package) fixed views of Prologue_Home (home, lights, shots) or Prologue_Arrival (arrival, arrivalshots). shots/arrivalshots: each space's view in normal light and with a scan. home/arrival: each view's draw calls, primitives and GPU time over 240 frames, uncapped; at the heaviest view the same while Stopped with a scan alive, and the frame of 8 scans. lights: Prologue_Home from the entrance of each of its four spaces at eye height, and Brief 13's lit view: mean level and share of pixels under 20 levels. Shots to Saved/Screenshots/Brief15."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString Set = Args.Num() > 0 ? Args[0].ToLower() : TEXT("home");
		RenderStatsDebug::StartViews(Set, Args.Num() > 1 ? Args[1] : TEXT("Run"), Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0);
	}));

#endif

// SPDX-License-Identifier: MIT

// The hologram's look (Brief 18). Development builds only.
//  tls.Hologram.Report: logs the hologram's turn, route (progress, nodes shown, the nodes in world space), beams (start,
//    end, level, and whether a downward trace onto its base meets the rod's tip), and the drift of a sample of its points.
//  tls.Hologram.Progress <Share|-1>: draws the route at a share of the briefing (shots), -1 follows the briefing again.
//  tls.Hologram.ShowStopped <0|1>: shows the hologram while Stopped too (to prove the scan's capture does not see it).
//  tls.Hologram.HideBeams <0|1>: hides the beams (to measure what they add on a shot).
//  tls.Hologram.BeamLength <Share|centre>: sets the beams' length for the session (centre: the length that ends at the
//    hologram's centre) and rebuilds the hologram.
//  tls.Shot <Folder> <Name>: a screenshot of the game's viewport to Saved/Screenshots/<Folder>/<Name>.png.
//  tls.Ship.Opening: (any map with the ship, actors tagged Ship) traces on the ship's mesh alone, visible (complex) and
//    collision (simple): the free opening's width at heights 15 to 305 cm and its height across it, at four depths of its
//    band, its centre; the step from the hangar's floor outside the opening to the ship's floor; what stands in front of
//    the opening at 5 cm (a skid).
//  tls.Look18 <home|arrival> [Label] [Quit]: Brief 18's checkpoint shots. home: the hologram from the monitor room's
//    doorway before the briefing, at a third and two thirds of it and after it (the route's share set directly), the beams
//    and rods from close by, the ship from two corners of the hangar and its opening side; arrival: the ship from inside,
//    with the flashlight and in a scan; beams: the beams at length 1 and at the length that ends at the hologram's centre,
//    from the doorway after the briefing (with and without the beams) and from close by, and from the doorway before the
//    briefing at length 1. Shots to Saved/Screenshots/Brief18.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Time/TimeStateSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Prologue/PrologueHologram.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "TlsDebugCommon.h"
#include "UnrealClient.h"

namespace HologramDebug
{
	static APrologueHologram* FindHologram(UWorld* World)
	{
		World = TimeStateDebug::ResolveGameWorld(World);
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<APrologueHologram> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	/** The view of the first player now. */
	static bool PlayerView(UWorld* W, FVector& OutEye, FRotator& OutRotation)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return false;
		}
		PC->GetPlayerViewPoint(OutEye, OutRotation);
		return true;
	}

	/**
	 * What the beams add along the ray from Eye through Target, before the first opaque surface: each crossing of a beam's
	 * cone (two-sided, so the near and the far wall of each cone) adds its level at the tip times its fade there. Returns
	 * the sum, in shares of the hologram's colour, and the number of crossings.
	 */
	static float BeamsAlongRay(const APrologueHologram* Holo, const FVector& Eye, const FVector& Target, int32& OutCrossings)
	{
		OutCrossings = 0;
		UWorld* W = Holo->GetWorld();
		const FVector D = (Target - Eye).GetSafeNormal();
		double MaxT = 1.e6;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("TlsHologramRay"), true);
		if (W)
		{
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				Params.AddIgnoredActor(PC->GetPawn());
			}
			if (W->LineTraceSingleByChannel(Hit, Eye, Eye + D * 1.e5, ECC_Visibility, Params))
			{
				MaxT = Hit.Distance;
			}
		}
		float Sum = 0.f;
		for (int32 I = 0; I < Holo->GetBeamCount(); ++I)
		{
			FVector S, E;
			float Spread = 0.f, Far = 0.f, Level = 0.f;
			Holo->GetBeam(I, S, E, Spread, Far, Level);
			const double Length = FVector::Dist(S, E);
			const FVector A = (E - S).GetSafeNormal();
			const double C2 = FMath::Square(FMath::Cos(FMath::DegreesToRadians(Spread)));
			const FVector Wv = Eye - S;
			const double DA = FVector::DotProduct(D, A);
			const double WA = FVector::DotProduct(Wv, A);
			const double QA = DA * DA - C2;
			const double QB = 2.0 * (DA * WA - C2 * FVector::DotProduct(Wv, D));
			const double QC = WA * WA - C2 * Wv.SizeSquared();
			double Roots[2];
			int32 NumRoots = 0;
			if (FMath::Abs(QA) < 1.e-9)
			{
				if (FMath::Abs(QB) > 1.e-9)
				{
					Roots[NumRoots++] = -QC / QB;
				}
			}
			else
			{
				const double Disc = QB * QB - 4.0 * QA * QC;
				if (Disc >= 0.0)
				{
					Roots[NumRoots++] = (-QB - FMath::Sqrt(Disc)) / (2.0 * QA);
					Roots[NumRoots++] = (-QB + FMath::Sqrt(Disc)) / (2.0 * QA);
				}
			}
			for (int32 R = 0; R < NumRoots; ++R)
			{
				const double T = Roots[R];
				const double H = WA + T * DA;
				if (T > 0.0 && T < MaxT && H >= 0.0 && H <= Length)
				{
					Sum += Level * APrologueHologram::BeamFade(static_cast<float>(H / Length), UTheLastShiftSettings::Get()->GetHologramTuning().BeamFadePower);
					++OutCrossings;
				}
			}
		}
		return Sum;
	}

	static void ReportBeams(APrologueHologram* Holo)
	{
		UWorld* W = Holo->GetWorld();
		const FVector Centre = Holo->GetActorLocation();
		const FTransform PointsToWorld = Holo->GetPointsComponent()->GetComponentTransform();
		APawn* Pawn = W && W->GetFirstPlayerController() ? W->GetFirstPlayerController()->GetPawn() : nullptr;
		for (int32 I = 0; I < Holo->GetBeamCount(); ++I)
		{
			FVector Start, End;
			float Spread = 0.f, Far = 0.f, Level = 0.f;
			Holo->GetBeam(I, Start, End, Spread, Far, Level);
			const float Length = FVector::Dist(Start, End);
			const FVector Axis = (End - Start).GetSafeNormal();
			// Every point of the hologram now, seen from the tip, against the spread.
			float Worst = 0.f;
			for (int32 P = 0; P < Holo->GetPointCount(); ++P)
			{
				FVector Rest, Now;
				if (Holo->GetPointLocal(P, Rest, Now))
				{
					const FVector World = PointsToWorld.TransformPosition(Now);
					Worst = FMath::Max(Worst, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Axis, (World - Start).GetSafeNormal()), -1.f, 1.f))));
				}
			}
			for (int32 N = 0; N < 4; ++N)
			{
				const FVector Node = Holo->GetRouteNodeWorld(N);
				Worst = FMath::Max(Worst, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Axis, (Node - Start).GetSafeNormal()), -1.f, 1.f))));
			}
			// Where it starts: a complex trace down onto the base.
			FHitResult Hit;
			FCollisionQueryParams Params(TEXT("TlsHologramRod"), true);
			const bool bHit = W && W->LineTraceSingleByChannel(Hit, Start + FVector(0, 0, 30), Start - FVector(0, 0, 30), ECC_Visibility, Params);
			// What its cone cuts: 24 lines along its wall, from the tip to its end, traced against everything (complex).
			const FVector B1 = FVector::CrossProduct(Axis, FMath::Abs(Axis.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
			const FVector B2 = FVector::CrossProduct(Axis, B1);
			const float SR = FMath::DegreesToRadians(Spread);
			TMap<FString, FVector2f> Cuts;  // actor -> (nearest share along the beam, generators that meet it)
			FCollisionQueryParams GenParams(TEXT("TlsHologramCut"), true, Pawn);
			for (int32 K = 0; K < 24; ++K)
			{
				const float A = 2.f * PI * K / 24.f;
				const FVector Dir = Axis * FMath::Cos(SR) + (B1 * FMath::Cos(A) + B2 * FMath::Sin(A)) * FMath::Sin(SR);
				const FVector To = Start + Dir * (Length / FMath::Max(FMath::Cos(SR), 0.05f));
				FHitResult G;
				if (W && W->LineTraceSingleByChannel(G, Start + Dir * 3.f, To, ECC_Visibility, GenParams))
				{
					const FString Name = G.GetActor() ? G.GetActor()->GetActorNameOrLabel() : FString(TEXT("?"));
					const float Share = FVector::DotProduct(G.ImpactPoint - Start, Axis) / Length;
					FVector2f& Entry = Cuts.FindOrAdd(Name, FVector2f(2.f, 0.f));
					Entry.X = FMath::Min(Entry.X, Share);
					Entry.Y += 1.f;
				}
			}
			FString CutText;
			for (const TPair<FString, FVector2f>& Cut : Cuts)
			{
				CutText += FString::Printf(TEXT(" %s (%d of 24 lines, from %.2f of the length, fade there %.3f);"), *Cut.Key, FMath::RoundToInt(Cut.Value.Y), Cut.Value.X,
					APrologueHologram::BeamFade(Cut.Value.X, UTheLastShiftSettings::Get()->GetHologramTuning().BeamFadePower));
			}
			UE_LOG(LogTheLastShift, Display, TEXT("  beam %d from %s, axis on the centre, spread %.2f deg (the cone that holds the hologram: %.2f deg), length %.0f cm (far side %.0f cm, the centre at %.4f of it), level at the tip %.3f; widest angle of the hologram's points and nodes from the tip %.2f deg (%s); a complex trace down meets %s %.1f cm from the start; its wall cuts:%s"),
				I, *Start.ToCompactString(), Spread, Holo->GetBeamHoldingSpread(I), Length, Far, FVector::Dist(Start, Centre) / Far, Level, Worst,
				Worst <= Holo->GetBeamHoldingSpread(I) ? (Worst <= Spread ? TEXT("inside the beam") : TEXT("inside the holding cone, outside the beam")) : TEXT("OUTSIDE the holding cone"),
				bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("nothing"), bHit ? FVector::Dist(Hit.ImpactPoint, Start) : -1.f, CutText.IsEmpty() ? TEXT(" nothing") : *CutText);
		}
		FVector Eye;
		FRotator ViewRotation;
		if (PlayerView(W, Eye, ViewRotation))
		{
			int32 Crossings = 0;
			const float Sum = BeamsAlongRay(Holo, Eye, Centre, Crossings);
			const FLinearColor C = Holo->GetPointColor();
			UE_LOG(LogTheLastShift, Display, TEXT("  from the eye at %s: the beams add %.3f of the hologram's colour along the ray through the centre (%d crossings), (%.3f, %.3f, %.3f) linear"),
				*Eye.ToCompactString(), Sum, Crossings, C.R * Sum, C.G * Sum, C.B * Sum);
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				FString Screen;
				FVector2D Px;
				if (PC->ProjectWorldLocationToScreen(Centre, Px, false))
				{
					Screen += FString::Printf(TEXT(" centre (%.0f, %.0f)"), Px.X, Px.Y);
				}
				for (int32 N = 0; N < 4; ++N)
				{
					const FVector Node = Holo->GetRouteNodeWorld(N);
					if (PC->ProjectWorldLocationToScreen(Node, Px, false))
					{
						int32 NodeCrossings = 0;
						const float NodeSum = BeamsAlongRay(Holo, Eye, Node, NodeCrossings);
						Screen += FString::Printf(TEXT(" node%d (%.0f, %.0f) beams %.3f;"), N + 1, Px.X, Px.Y, NodeSum);
					}
				}
				UE_LOG(LogTheLastShift, Display, TEXT("  on screen:%s"), *Screen);
			}
		}
	}

	static float CentreShare(const APrologueHologram* Holo)
	{
		FVector S, E;
		float Spread = 0.f, Far = 0.f, Level = 0.f;
		return Holo && Holo->GetBeam(0, S, E, Spread, Far, Level) && Far > 0.f ? FVector::Dist(S, Holo->GetActorLocation()) / Far : 1.f;
	}

	static void SetBeamLength(APrologueHologram* Holo, float Share)
	{
		if (!Holo)
		{
			return;
		}
		GetMutableDefault<UTheLastShiftSettings>()->Hologram.BeamLength = Share;
		Holo->Rebuild();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Hologram.BeamLength: %.3f (the centre at %.3f)"), UTheLastShiftSettings::Get()->GetHologramTuning().BeamLength, CentreShare(Holo));
	}

	static void Report(UWorld* World)
	{
		APrologueHologram* Holo = FindHologram(World);
		if (!Holo)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Hologram.Report: no hologram in this world."));
			return;
		}
		const FHologramTuning Tuning = UTheLastShiftSettings::Get()->GetHologramTuning();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Hologram.Report: %s, shown %d, %d points, turn %.2f deg (one turn in %.1f s), route %.3f drawn, %d of %d nodes shown, colour points %s route %s, %d beams."),
			*Holo->GetName(), Holo->IsShown() ? 1 : 0, Holo->GetPointCount(), Holo->GetTurnDegrees(), Tuning.TurnSeconds, Holo->GetRouteProgress01(),
			Holo->GetRouteNodesShown(), 4, *Holo->GetPointColor().ToString(), *Holo->GetRouteColor().ToString(), Holo->GetBeamCount());
		for (int32 I = 0; I < 4; ++I)
		{
			UE_LOG(LogTheLastShift, Display, TEXT("  node %d at %s"), I + 1, *Holo->GetRouteNodeWorld(I).ToCompactString());
		}
		ReportBeams(Holo);
		double MaxDrift = 0.0, MaxOffFace = 0.0;
		for (int32 I = 0; I < Holo->GetPointCount(); I += 37)
		{
			FVector Rest, Now;
			if (Holo->GetPointLocal(I, Rest, Now))
			{
				MaxDrift = FMath::Max(MaxDrift, FVector::Dist(Rest, Now));
				MaxOffFace = FMath::Max(MaxOffFace, FMath::Abs(FMath::Abs(Now.X) + FMath::Abs(Now.Y) + FMath::Abs(Now.Z) - Holo->GetHalfSize()));
			}
		}
		UE_LOG(LogTheLastShift, Display, TEXT("  points sampled every 37th: drift now at most %.2f cm (DriftDistance %.1f), off the octahedron's surface by at most %.4f cm"),
			MaxDrift, Tuning.DriftDistance, MaxOffFace);
	}

	// --- tls.Look18: the checkpoint's shots ---

	struct FLookStep
	{
		FString Label;
		TFunction<float()> Run;
	};

	struct FLookRun
	{
		TArray<FLookStep> Steps;
		int32 Next = 0;
		double WaitUntil = 0.0;
		FString Label;
		bool bQuit = false;
		float SavedLength = 1.f;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FLookRun GLook;

	static UWorld* LookWorld()
	{
		return TimeStateDebug::ResolveGameWorld(nullptr);
	}

	static APawn* LookPawn()
	{
		UWorld* W = LookWorld();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	/** Stands the pawn on the floor under (X, Y) and turns its view to LookAt. */
	static void LookStandAt(double X, double Y, const FVector& LookAt)
	{
		APawn* P = LookPawn();
		UWorld* W = LookWorld();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!P || !PC)
		{
			return;
		}
		const ACharacter* C = Cast<ACharacter>(P);
		const double Half = C && C->GetCapsuleComponent() ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		double Z = P->GetActorLocation().Z;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("TlsLook18"), false, P);
		const double From = FMath::Max(P->GetActorLocation().Z, Half) + 150.0;
		if (W->LineTraceSingleByChannel(Hit, FVector(X, Y, From), FVector(X, Y, From - 2000.0), ECC_Visibility, Params))
		{
			Z = Hit.ImpactPoint.Z + Half + 2.0;
		}
		P->SetActorLocation(FVector(X, Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
		FVector Eye;
		FRotator Ignored;
		P->GetActorEyesViewPoint(Eye, Ignored);
		PC->SetControlRotation((LookAt - Eye).Rotation());
	}

	static void LookFlashlight(bool bOn)
	{
		if (const APawn* P = LookPawn())
		{
			TArray<USpotLightComponent*> Lights;
			P->GetComponents(Lights);
			for (USpotLightComponent* Light : Lights)
			{
				Light->SetVisibility(bOn);
			}
		}
	}

	static UTimeStateSubsystem* LookTime()
	{
		UWorld* W = LookWorld();
		return W ? W->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	}

	static void LookShot(const FString& Name)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief18"), GLook.Label + TEXT("_") + Name + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogTheLastShift, Display, TEXT("Look18: %s: shot %s"), *GLook.Label, *Path);
	}

	static bool TickLook(float)
	{
		if (FPlatformTime::Seconds() < GLook.WaitUntil)
		{
			return true;
		}
		if (!GLook.Steps.IsValidIndex(GLook.Next))
		{
			UE_LOG(LogTheLastShift, Display, TEXT("Look18: %s: ===== done ====="), *GLook.Label);
			if (GLook.bQuit)
			{
				FPlatformMisc::RequestExit(false, TEXT("tls.Look18"));
			}
			GLook.Ticker.Reset();
			return false;
		}
		const FLookStep& Step = GLook.Steps[GLook.Next++];
		const float Wait = Step.Run();
		UE_LOG(LogTheLastShift, Display, TEXT("Look18: %s: %s"), *GLook.Label, *Step.Label);
		GLook.WaitUntil = FPlatformTime::Seconds() + Wait;
		return true;
	}

	static void AddShot(TArray<FLookStep>& S, const FString& Name, TFunction<void()> Pose)
	{
		S.Add({ Name + TEXT(": pose"), [Pose]() { Pose(); return 1.0f; } });
		S.Add({ Name + TEXT(": shot"), [Name]() { LookShot(Name); return 0.5f; } });
	}

	static void StartLook(const FString& Which, const FString& Label, bool bQuit)
	{
		if (GLook.Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GLook.Ticker);
		}
		GLook = FLookRun();
		GLook.Label = Label;
		GLook.bQuit = bQuit;
		TArray<FLookStep>& S = GLook.Steps;
		S.Add({ TEXT("settle"), []() { return 2.f; } });
		if (Which == TEXT("home"))
		{
			const FVector Holo(1810.f, 870.f, 250.f);
			const TCHAR* Names[4] = { TEXT("Holo_0_BeforeBriefing"), TEXT("Holo_1_ThirdOfBriefing"), TEXT("Holo_2_TwoThirdsOfBriefing"), TEXT("Holo_3_AfterBriefing") };
			const float Shares[4] = { 0.f, 1.f / 3.f, 2.f / 3.f, 1.f };
			for (int32 I = 0; I < 4; ++I)
			{
				const float Share = Shares[I];
				AddShot(S, Names[I], [Holo, Share]()
				{
					LookStandAt(1810.0, 230.0, Holo);
					if (APrologueHologram* H = FindHologram(nullptr))
					{
						H->SetRouteProgressOverride(Share);
					}
				});
			}
			AddShot(S, TEXT("Holo_4_BeamsAndRods"), []() { LookStandAt(2160.0, 560.0, FVector(1880.0, 820.0, 110.0)); });
			S.Add({ TEXT("follow the briefing again"), []()
			{
				if (APrologueHologram* H = FindHologram(nullptr))
				{
					H->SetRouteProgressOverride(-1.f);
				}
				return 0.f;
			} });
			const FVector Ship(4330.f, 0.f, 250.f);
			AddShot(S, TEXT("Ship_Home_CornerEntry"), [Ship]() { LookStandAt(3150.0, -1750.0, Ship); });
			AddShot(S, TEXT("Ship_Home_CornerFar"), [Ship]() { LookStandAt(5550.0, 1750.0, Ship); });
			AddShot(S, TEXT("Ship_Home_OpeningSide"), [Ship]() { LookStandAt(3150.0, 600.0, Ship); });
		}
		else if (Which == TEXT("beams"))
		{
			const FVector Holo(1810.f, 870.f, 250.f);
			GLook.SavedLength = UTheLastShiftSettings::Get()->GetHologramTuning().BeamLength;
			const TArray<TPair<FString, float>> Lengths = { { TEXT("L1"), 1.f }, { TEXT("LCentre"), -1.f } };
			for (const TPair<FString, float>& L : Lengths)
			{
				const FString Tag = L.Key;
				const float Share = L.Value;
				S.Add({ Tag + TEXT(": the length"), [Share]()
				{
					APrologueHologram* H = FindHologram(nullptr);
					SetBeamLength(H, Share >= 0.f ? Share : CentreShare(H));
					if (H)
					{
						H->SetRouteProgressOverride(1.f);
					}
					return 0.5f;
				} });
				S.Add({ Tag + TEXT(": the doorway"), [Holo]() { LookStandAt(1810.0, 230.0, Holo); return 1.0f; } });
				S.Add({ Tag + TEXT(": report, motion held"), []() { Report(nullptr); if (APrologueHologram* H = FindHologram(nullptr)) { H->SetMotionFrozenForTest(true); } return 0.3f; } });
				S.Add({ Tag + TEXT(": shot"), [Tag]() { LookShot(TEXT("Beams_") + Tag + TEXT("_Doorway_AfterBriefing")); return 0.5f; } });
				S.Add({ Tag + TEXT(": the beams hidden"), []() { if (APrologueHologram* H = FindHologram(nullptr)) { H->SetBeamsHiddenForTest(true); } return 0.5f; } });
				S.Add({ Tag + TEXT(": shot without the beams"), [Tag]() { LookShot(TEXT("Beams_") + Tag + TEXT("_Doorway_AfterBriefing_NoBeams")); return 0.5f; } });
				S.Add({ Tag + TEXT(": the beams back, motion again"), []() { if (APrologueHologram* H = FindHologram(nullptr)) { H->SetBeamsHiddenForTest(false); H->SetMotionFrozenForTest(false); } return 0.5f; } });
				S.Add({ Tag + TEXT(": close by"), []() { LookStandAt(2160.0, 560.0, FVector(1880.0, 820.0, 110.0)); return 1.0f; } });
				S.Add({ Tag + TEXT(": report"), []() { Report(nullptr); return 0.3f; } });
				S.Add({ Tag + TEXT(": close shot"), [Tag]() { LookShot(TEXT("Beams_") + Tag + TEXT("_Close")); return 0.5f; } });
			}
			S.Add({ TEXT("length 1, before the briefing"), []()
			{
				APrologueHologram* H = FindHologram(nullptr);
				SetBeamLength(H, 1.f);
				if (H)
				{
					H->SetRouteProgressOverride(0.f);
				}
				return 0.5f;
			} });
			S.Add({ TEXT("before the briefing: the doorway"), [Holo]() { LookStandAt(1810.0, 230.0, Holo); return 1.0f; } });
			S.Add({ TEXT("before the briefing: report"), []() { Report(nullptr); return 0.3f; } });
			S.Add({ TEXT("Beams_L1_Doorway_BeforeBriefing: shot"), []() { LookShot(TEXT("Beams_L1_Doorway_BeforeBriefing")); return 0.5f; } });
			S.Add({ TEXT("the settings' length and the briefing again"), []()
			{
				APrologueHologram* H = FindHologram(nullptr);
				SetBeamLength(H, GLook.SavedLength);
				if (H)
				{
					H->SetRouteProgressOverride(-1.f);
				}
				return 0.f;
			} });
		}
		else
		{
			// Prologue_Arrival: the ship at (900, 0) turned 180 degrees, its opening at X = 1200.
			AddShot(S, TEXT("Ship_Arrival_Inside_Fore"), []() { LookStandAt(1080.0, 280.0, FVector(650.0, -420.0, 230.0)); });
			AddShot(S, TEXT("Ship_Arrival_Inside_Aft"), []() { LookStandAt(1000.0, -250.0, FVector(850.0, 520.0, 160.0)); });
			AddShot(S, TEXT("Ship_Arrival_Flashlight"), []() { LookStandAt(2050.0, -950.0, FVector(900.0, 0.0, 230.0)); LookFlashlight(true); });
			AddShot(S, TEXT("Ship_Arrival_Flashlight_2"), []() { LookStandAt(1900.0, 1100.0, FVector(900.0, 0.0, 230.0)); });
			S.Add({ TEXT("stop time (the first scan)"), []()
			{
				LookStandAt(2050.0, -950.0, FVector(900.0, 0.0, 230.0));
				if (UTimeStateSubsystem* T = LookTime(); T && !T->IsStopped())
				{
					T->Stop();
				}
				return 2.5f;
			} });
			S.Add({ TEXT("Ship_Arrival_Scan: shot"), []() { LookShot(TEXT("Ship_Arrival_Scan")); return 0.5f; } });
			S.Add({ TEXT("resume"), []()
			{
				if (UTimeStateSubsystem* T = LookTime(); T && T->IsStopped())
				{
					T->Resume();
				}
				LookFlashlight(false);
				return 1.0f;
			} });
		}
		UE_LOG(LogTheLastShift, Display, TEXT("Look18: %s: ===== %s: %d steps ====="), *Label, *Which, S.Num());
		GLook.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickLook));
	}

	// --- tls.Ship.Opening ---

	static void ShipOpening(UWorld* World)
	{
		World = TimeStateDebug::ResolveGameWorld(World);
		AActor* Ship = nullptr;
		for (TActorIterator<AActor> It(World); World && It; ++It)
		{
			if (It->ActorHasTag(TEXT("Ship")))
			{
				Ship = *It;
				break;
			}
		}
		UStaticMeshComponent* Mesh = Ship ? Ship->FindComponentByClass<UStaticMeshComponent>() : nullptr;
		if (!Mesh)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Ship.Opening: no actor tagged Ship with a mesh in this world."));
			return;
		}
		const FTransform T = Ship->GetActorTransform();
		// The opening is on the mesh's -X side: its band from X -300 to -265 (local, cm).
		const auto Trace = [&](const FVector& LocalFrom, const FVector& LocalTo, bool bComplex, FVector& OutLocal) -> bool
		{
			FHitResult Hit;
			FCollisionQueryParams Params(TEXT("TlsShipOpening"), bComplex);
			if (Mesh->LineTraceComponent(Hit, T.TransformPosition(LocalFrom), T.TransformPosition(LocalTo), Params))
			{
				OutLocal = T.InverseTransformPosition(Hit.ImpactPoint);
				return true;
			}
			return false;
		};
		for (const bool bComplex : { true, false })
		{
			float MinWidth = 1.e6f, MaxWidth = 0.f, CentreY = 0.f, MinHeight = 1.e6f, MaxHeight = 0.f, Sill = 0.f, Lintel = 0.f;
			FString WidthAt, HeightAt;
			for (const float X : { -295.f, -290.f, -280.f, -270.f })
			{
				for (float Z = 15.f; Z <= 305.f; Z += 10.f)
				{
					FVector A, B;
					if (Trace(FVector(X, 0, Z), FVector(X, 500, Z), bComplex, A) && Trace(FVector(X, 0, Z), FVector(X, -500, Z), bComplex, B))
					{
						const float W = A.Y - B.Y;
						if (W < MinWidth)
						{
							MinWidth = W;
							CentreY = (A.Y + B.Y) * 0.5f;
							WidthAt = FString::Printf(TEXT("X %.0f, Z %.0f"), X, Z);
						}
						MaxWidth = FMath::Max(MaxWidth, W);
					}
				}
				for (float Y = -195.f; Y <= 195.f; Y += 15.f)
				{
					FVector Up, Down;
					const bool bUp = Trace(FVector(X, Y, 150), FVector(X, Y, 650), bComplex, Up);
					const float Floor = Trace(FVector(X, Y, 150), FVector(X, Y, -50), bComplex, Down) ? Down.Z : 0.f;  // nothing: the hangar's floor at the ship's base
					if (bUp)
					{
						const float H = Up.Z - Floor;
						if (H < MinHeight)
						{
							MinHeight = H;
							Sill = Floor;
							Lintel = Up.Z;
							HeightAt = FString::Printf(TEXT("X %.0f, Y %.0f"), X, Y);
						}
						MaxHeight = FMath::Max(MaxHeight, H);
					}
				}
			}
			UE_LOG(LogTheLastShift, Display, TEXT("tls.Ship.Opening: %s, %s traces: free width %.1f cm (largest %.1f; narrowest at %s), centre at local Y %.1f; free height %.1f cm (largest %.1f; lowest at %s, from %.1f to %.1f)"),
				*Ship->GetActorNameOrLabel(), bComplex ? TEXT("visible mesh (complex)") : TEXT("collision (simple)"), MinWidth, MaxWidth, *WidthAt, CentreY, MinHeight, MaxHeight, *HeightAt, Sill, Lintel);
		}
		// The step: the ship's floor inside against the hangar's floor outside the opening (a world trace, the ship ignored).
		FVector Inside;
		const bool bInside = Trace(FVector(-150, 0, 150), FVector(-150, 0, -50), true, Inside);
		FHitResult Out;
		FCollisionQueryParams OutParams(TEXT("TlsShipOpeningOut"), true, Ship);
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			OutParams.AddIgnoredActor(PC->GetPawn());
		}
		const FVector OutsideTop = T.TransformPosition(FVector(-380, 0, 150));
		// The floor the player walks on: Visibility first, then Pawn (the home's hangar floor answers only the latter).
		const bool bOutside = World->LineTraceSingleByChannel(Out, OutsideTop, OutsideTop - FVector(0, 0, 400), ECC_Visibility, OutParams)
			|| World->LineTraceSingleByChannel(Out, OutsideTop, OutsideTop - FVector(0, 0, 400), ECC_Pawn, OutParams);
		const float OutsideZ = bOutside ? T.InverseTransformPosition(Out.ImpactPoint).Z : 0.f;
		FVector Sill;
		const bool bSill = Trace(FVector(-298, 0, 150), FVector(-298, 0, -50), true, Sill);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Ship.Opening: the ship's floor at %.1f cm, the hangar's floor outside the opening at %.1f cm (%s): a step of %.1f cm; at the opening's outer face (X -298) the mesh's top is at %s"),
			bInside ? Inside.Z : -1.f, OutsideZ, bOutside ? *GetNameSafe(Out.GetActor()) : TEXT("nothing"), (bInside ? Inside.Z : 0.f) - OutsideZ,
			bSill ? *FString::Printf(TEXT("%.1f cm"), Sill.Z) : TEXT("nothing"));
		// In front of the opening, 5 cm above the base: the first part of the ship met coming in along +X (a skid would stand out).
		FString Front;
		for (float Y = -190.f; Y <= 190.f; Y += 38.f)
		{
			FVector Hit;
			Front += Trace(FVector(-420, Y, 5), FVector(-200, Y, 5), true, Hit) ? FString::Printf(TEXT(" Y %.0f: X %.1f;"), Y, Hit.X) : FString::Printf(TEXT(" Y %.0f: nothing;"), Y);
		}
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Ship.Opening: at 5 cm, coming in across the opening, the mesh starts at:%s"), *Front);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsHologramReportCommand(
	TEXT("tls.Hologram.Report"),
	TEXT("tls.Hologram.Report: logs the hologram's turn, route, beams (and whether each starts on its rod's tip) and the drift of a sample of its points."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		HologramDebug::Report(World);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsHologramProgressCommand(
	TEXT("tls.Hologram.Progress"),
	TEXT("tls.Hologram.Progress <Share|-1>: draws the route at a share (0..1) of the briefing; -1 follows the briefing again."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (APrologueHologram* Holo = HologramDebug::FindHologram(World))
		{
			Holo->SetRouteProgressOverride(Args.Num() > 0 ? FCString::Atof(*Args[0]) : -1.f);
			UE_LOG(LogTheLastShift, Display, TEXT("tls.Hologram.Progress: route at %.3f"), Holo->GetRouteProgress01());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsHologramShowStoppedCommand(
	TEXT("tls.Hologram.ShowStopped"),
	TEXT("tls.Hologram.ShowStopped <0|1>: shows the hologram while Stopped too (measurement only)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (APrologueHologram* Holo = HologramDebug::FindHologram(World))
		{
			Holo->SetShownWhileStoppedForTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsHologramHideBeamsCommand(
	TEXT("tls.Hologram.HideBeams"),
	TEXT("tls.Hologram.HideBeams <0|1>: hides the hologram's beams (measurement only)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (APrologueHologram* Holo = HologramDebug::FindHologram(World))
		{
			Holo->SetBeamsHiddenForTest(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsHologramBeamLengthCommand(
	TEXT("tls.Hologram.BeamLength"),
	TEXT("tls.Hologram.BeamLength <Share|centre>: sets the beams' length for the session (a share of the way to the hologram's far side; centre: the length that ends at its centre) and rebuilds the hologram."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APrologueHologram* Holo = HologramDebug::FindHologram(World);
		const bool bCentre = Args.Num() > 0 && Args[0].Equals(TEXT("centre"), ESearchCase::IgnoreCase);
		HologramDebug::SetBeamLength(Holo, bCentre ? HologramDebug::CentreShare(Holo) : (Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.f));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsShipOpeningCommand(
	TEXT("tls.Ship.Opening"),
	TEXT("tls.Ship.Opening: traces on the ship's mesh: the free opening (width, height, centre), the step from the hangar's floor to the ship's, and what stands in front of the opening at 5 cm."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		HologramDebug::ShipOpening(World);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsShotCommand(
	TEXT("tls.Shot"),
	TEXT("tls.Shot <Folder> <Name>: a screenshot of the game's viewport to Saved/Screenshots/<Folder>/<Name>.png."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString Folder = Args.Num() > 0 ? Args[0] : FString(TEXT("Shots"));
		const FString Name = Args.Num() > 1 ? Args[1] : FString(TEXT("Shot"));
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Folder, Name + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Shot: %s"), *Path);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLook18Command(
	TEXT("tls.Look18"),
	TEXT("tls.Look18 <home|arrival> [Label=Run] [Quit=0]: Brief 18's checkpoint shots (the hologram through the briefing, its beams and rods, the ship at home and on arrival). Shots to Saved/Screenshots/Brief18."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		HologramDebug::StartLook(Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("home")), Args.Num() > 1 ? Args[1] : FString(TEXT("Run")),
			Args.Num() > 2 && FCString::Atoi(*Args[2]) != 0);
	}));

#endif // !UE_BUILD_SHIPPING

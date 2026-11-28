// SPDX-License-Identifier: MIT

// tls.HologramTest [Quit] (Brief 18): the hologram of Prologue_Home, in PIE (start in Prologue_Home) and in a package
// (<exe> /Game/TheLastShift/Maps/Prologue_Home -ExecCmds="tls.HologramTest 1"). Development builds only.
//  - its parts and colours: the points and the beams in ScanColors.Scan, the route in ScanColors.Entity, one beam per rod;
//  - the turn at its set speed, the route turning with it, the beams not turning;
//  - the points' drift: within DriftDistance of their rest, on the octahedron's faces, smooth, out of step;
//  - the beams: each from its rod's tip (a trace down meets the base), axis on the centre, spread BeamSpreadShare of the
//    cone that holds the whole hologram (measured on the points), length BeamLength of the way to the far side, the
//    flicker changing their level only;
//  - everything still behind the pause menu (turn, drift, flicker, the route's share);
//  - the route drawn by the briefing terminal typing fixture rows: none before the Hologram group, the share of the text
//    typed (rows weighted by their length), still between two rows and behind the pause menu, whole after the briefing and
//    still whole later; with two rows (45 and 140 characters), four rows (20, 90, 35 and 60) and none;
//  - the route's colour measured on a shot from the doorway, with and without the beams, and the beams seen there (Brief
//    48): over each beam's lower half, from its rod's tip to half its length, the median difference with the beams shown
//    and hidden at least 20 levels, and no pixel of a beam pushed above 200 levels by it;
//  - nothing of it while Stopped (each part's component hidden; a shot before the first scan), and none of its parts in the
//    scan's capture: from two poses, the scan's depth with each part shown while Stopped (test only) is the depth without
//    the hologram, texel by texel.
// tls.HologramTest 0 tune: the beams' measure alone, for pairs of BeamLevel and BeamFadePower set for the session (TUNE lines).
// Shots to Saved/Screenshots/Brief48 (Brief44, Brief18 before). The fixture rows replace the prologue table for the run and are put back at the end.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "LidarScanComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueBriefingTerminal.h"
#include "Prologue/PrologueHologram.h"
#include "Prologue/PrologueLineTrigger.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "TlsDebugCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

namespace HologramTest
{
	// ---------------------------------------------------------------------------------------------------------------
	// Runner
	// ---------------------------------------------------------------------------------------------------------------

	struct FStep
	{
		FString Label;
		TFunction<float()> Run;               // returns seconds to wait after it
		TFunction<bool()> Until;              // when set, the step waits for it (each tick) up to Timeout
		float Timeout = 0.f;
	};

	struct FShotPixels
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
	};

	struct FRun
	{
		TArray<FStep> Steps;
		int32 Next = 0;
		double WaitUntil = 0.0;
		double StepStarted = -1.0;
		int32 Passed = 0;
		int32 Failed = 0;
		bool bQuit = false;
		FTSTicker::FDelegateHandle Ticker;
		FTSTicker::FDelegateHandle Sampler;
		TStrongObjectPtr<UDataTable> Fixture;
		TSoftObjectPtr<UDataTable> SavedPrologue;
		bool bSavedTable = false;
		// shots
		FDelegateHandle ShotHandle;
		FString ShotName;
		bool bShotReady = false;
		FShotPixels Shot;
		TMap<FString, FShotPixels> Shots;
		// depth captures
		TMap<FString, TArray<float>> Depths;
		// samples
		TArray<double> TurnTimes, TurnDegrees;
		TArray<FVector> Rest, PrevNow;
		double MaxDrift = 0.0, MaxOffFace = 0.0, MaxStep = 0.0, MinMove = 1.e9, MaxMove = 0.0;
		TArray<FTransform> BeamTransforms;
		float LevelMin = 1.e9f, LevelMax = -1.f;
		bool bBeamsMoved = false;
		// route by the briefing
		TArray<FString> RowNames;
		TArray<int32> RowLengths;
		double MaxRouteError = 0.0;
		int32 RouteSamples = 0, RouteBackwards = 0, GapSamples = 0, GapMoves = 0, MenuMoves = 0, HoldFrames = 0;
		float ExpectedMax = 0.f;
		int32 ScanCountBefore = 0;
		float LastProgress = 0.f;
		bool bRouteBeforeZero = true;
		int32 LastRow = -1;
		int32 LastShown = -1;
		float FrozenProgress = -1.f;
		bool bMenuChecked = false;
	};
	static FRun G;

	static UWorld* W() { return TimeStateDebug::ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static AStdPlayerController* StdPC() { return Cast<AStdPlayerController>(PC()); }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }
	static UTimeStateSubsystem* TS() { UWorld* World = W(); return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; }
	static ULidarScanComponent* Lidar() { UWorld* World = W(); const UScanDirectorComponent* D = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr; return D ? D->GetLidarScan() : nullptr; }
	static UTextChannelComponent* Channel() { APawn* P = Pawn(); return P ? P->FindComponentByClass<UTextChannelComponent>() : nullptr; }

	template <typename T> static T* First()
	{
		UWorld* World = W();
		for (TActorIterator<T> It(World); World && It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static APrologueHologram* Holo() { return First<APrologueHologram>(); }
	static APrologueBriefingTerminal* Briefing() { return First<APrologueBriefingTerminal>(); }

	static void Note(const FString& Text) { UE_LOG(LogTheLastShift, Display, TEXT("HologramTest   ..  %s"), *Text); }
	static void Check(bool bOk, const FString& Text)
	{
		(bOk ? G.Passed : G.Failed)++;
		UE_LOG(LogTheLastShift, Display, TEXT("HologramTest   %s  %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *Text);
	}

	static FStep Do(const FString& Label, TFunction<float()> Fn) { FStep S; S.Label = Label; S.Run = MoveTemp(Fn); return S; }
	static FStep WaitFor(const FString& Label, TFunction<bool()> Until, float Timeout, TFunction<float()> Then = nullptr)
	{
		FStep S;
		S.Label = Label;
		S.Until = MoveTemp(Until);
		S.Timeout = Timeout;
		S.Run = Then ? MoveTemp(Then) : TFunction<float()>([]() { return 0.f; });
		return S;
	}

	static double RunSeconds() { const UTimeStateSubsystem* T = TS(); return T ? T->GetRunSeconds() : 0.0; }

	static void StandAt(double X, double Y, const FVector& LookAt)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		UWorld* World = W();
		if (!P || !C || !World)
		{
			return;
		}
		const ACharacter* Character = Cast<ACharacter>(P);
		const double Half = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		double Z = P->GetActorLocation().Z;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("TlsHologramTest"), false, P);
		const double From = FMath::Max(P->GetActorLocation().Z, Half) + 150.0;
		if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, From), FVector(X, Y, From - 2000.0), ECC_Visibility, Params))
		{
			Z = Hit.ImpactPoint.Z + Half + 2.0;
		}
		P->SetActorLocation(FVector(X, Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
		FVector Eye;
		FRotator Ignored;
		P->GetActorEyesViewPoint(Eye, Ignored);
		C->SetControlRotation((LookAt - Eye).Rotation());
	}

	static bool OpenMenu() { AStdPlayerController* C = StdPC(); return C && C->PauseGame(); }
	static bool CloseMenu() { AStdPlayerController* C = StdPC(); return C && C->ResumeGame(); }
	static bool Frozen() { const UTimeStateSubsystem* T = TS(); return T && T->IsRunFrozen(); }

	static void StopTime() { if (UTimeStateSubsystem* T = TS(); T && !T->IsStopped()) { T->Stop(); } }
	static void ResumeTime() { if (UTimeStateSubsystem* T = TS(); T && T->IsStopped()) { T->Resume(); } }
	static bool IsStopped() { const UTimeStateSubsystem* T = TS(); return T && T->IsStopped(); }

	// --- shots ---

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		G.Shot.Width = Width;
		G.Shot.Height = Height;
		G.Shot.Pixels = Pixels;
		G.Shots.Add(G.ShotName, G.Shot);
		if (G.ShotName.StartsWith(TEXT("_")))
		{
			// Measured, not saved (Brief 48's tuning).
			G.bShotReady = true;
			return;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief48"), TEXT("HologramTest_") + G.ShotName + TEXT(".png")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		Note(FString::Printf(TEXT("shot %s (%d x %d): %s"), *G.ShotName, Width, Height, *Path));
		G.bShotReady = true;
	}

	static void AddShot(TArray<FStep>& S, const FString& Name)
	{
		S.Add(Do(Name + TEXT(": shot"), [Name]()
		{
			if (!G.ShotHandle.IsValid())
			{
				G.ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
			}
			G.ShotName = Name;
			G.bShotReady = false;
			FScreenshotRequest::RequestScreenshot(FString(), false, false);
			return 0.f;
		}));
		S.Add(WaitFor(Name + TEXT(": shot taken"), []() { return G.bShotReady; }, 5.f));
	}

	static int32 Level(const FColor& C) { return FMath::Max3(C.R, C.G, C.B); }

	// --- Brief 48: what the player sees of the beams, on two shots from one pose (beams shown and hidden, motion held) ---

	struct FBeamSeen
	{
		/** Differences (levels, the largest channel) over the beam's lower half: from the rod's tip to half its length. */
		TArray<int32> Lower;
		/** Pixels of the whole beam, and of those the ones the beam pushes above 200 levels. */
		int32 Pixels = 0;
		int32 Above200 = 0;
		int32 Brightest = 0;
		int32 Hidden = 0;
	};

	struct FBeamMeasure
	{
		TArray<FBeamSeen> Beams;
		/** Differences by share of the length (10 bins, the rod's tip first), all beams together. */
		TArray<TArray<int32>> Bins;
		int32 MinMedian = -1;
		int32 Above200 = 0;
		int32 Checked = 0;
	};

	static int32 Median(TArray<int32> V)
	{
		if (V.IsEmpty())
		{
			return -1;
		}
		V.Sort();
		return V[V.Num() / 2];
	}

	/**
	 * Each pixel's ray from the camera, against each beam's cone (apex at the rod's tip, axis to its end, half-angle its
	 * spread): inside when its closest approach to the axis lies within the cone's radius there; its share of the length is
	 * that point's along the axis. A pixel where geometry stands between the camera and the beam is not the beam's.
	 */
	static FBeamMeasure MeasureBeams(const FShotPixels& With, const FShotPixels& Without)
	{
		FBeamMeasure R;
		R.Bins.SetNum(10);
		const APrologueHologram* H = Holo();
		APlayerController* C = PC();
		UWorld* World = W();
		if (!H || !C || !World || With.Pixels.Num() != Without.Pixels.Num() || With.Pixels.IsEmpty())
		{
			return R;
		}
		struct FCone { FVector A, U; double Length = 0.0, Tan = 0.0; };
		TArray<FCone> Cones;
		FVector2D Lo(1.e9, 1.e9), Hi(-1.e9, -1.e9);
		for (int32 I = 0; I < H->GetBeamCount(); ++I)
		{
			FVector A, B;
			float Spread = 0.f, Far = 0.f, Lvl = 0.f;
			H->GetBeam(I, A, B, Spread, Far, Lvl);
			FCone& K = Cones.AddDefaulted_GetRef();
			K.A = A;
			K.Length = FVector::Dist(A, B);
			K.U = (B - A).GetSafeNormal();
			K.Tan = FMath::Tan(FMath::DegreesToRadians(Spread));
			const double Radius = K.Length * K.Tan;
			const FVector Side = FVector::CrossProduct(K.U, FVector::UpVector).GetSafeNormal();
			const FVector Up = FVector::CrossProduct(K.U, Side);
			for (const FVector& P : { A, B + Side * Radius, B - Side * Radius, B + Up * Radius, B - Up * Radius })
			{
				FVector2D S2;
				if (C->ProjectWorldLocationToScreen(P, S2, false))
				{
					Lo = FVector2D(FMath::Min(Lo.X, S2.X), FMath::Min(Lo.Y, S2.Y));
					Hi = FVector2D(FMath::Max(Hi.X, S2.X), FMath::Max(Hi.Y, S2.Y));
				}
			}
		}
		R.Beams.SetNum(Cones.Num());
		FCollisionQueryParams Params(TEXT("TlsHologramBeams"), false, Pawn());
		const int32 X0 = FMath::Max(0, int32(Lo.X) - 2), X1 = FMath::Min(With.Width, int32(Hi.X) + 2);
		const int32 Y0 = FMath::Max(0, int32(Lo.Y) - 2), Y1 = FMath::Min(With.Height, int32(Hi.Y) + 2);
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				FVector O, D;
				if (!C->DeprojectScreenPositionToWorld(X + 0.5f, Y + 0.5f, O, D))
				{
					continue;
				}
				const int32 Index = Y * With.Width + X;
				const int32 Diff = FMath::Max3(FMath::Abs(With.Pixels[Index].R - Without.Pixels[Index].R), FMath::Abs(With.Pixels[Index].G - Without.Pixels[Index].G),
					FMath::Abs(With.Pixels[Index].B - Without.Pixels[Index].B));
				for (int32 K = 0; K < Cones.Num(); ++K)
				{
					const FCone& Cone = Cones[K];
					// The closest points of the ray (t >= 0) and the axis (0 <= s <= Length).
					const FVector W0 = O - Cone.A;
					const double Bd = FVector::DotProduct(D, Cone.U);
					const double Dd = FVector::DotProduct(D, W0);
					const double Ed = FVector::DotProduct(Cone.U, W0);
					const double Denom = 1.0 - Bd * Bd;
					const double S = Denom > 1.e-9 ? (Ed - Bd * Dd) / Denom : Ed;
					// A ray whose closest approach lies before the tip or past the beam's end does not pass through it.
					if (S <= 0.0 || S >= Cone.Length)
					{
						continue;
					}
					const double T = FMath::Max(0.0, FVector::DotProduct(D, Cone.A + Cone.U * S - O));
					const FVector OnRay = O + D * T;
					const double Dist = FVector::Dist(OnRay, Cone.A + Cone.U * S);
					if (S <= 0.0 || Dist > S * Cone.Tan)
					{
						continue;
					}
					FHitResult Hit;
					if (World->LineTraceSingleByChannel(Hit, O, OnRay, ECC_Visibility, Params) && Hit.Distance < T - 5.0)
					{
						++R.Beams[K].Hidden;
						continue;
					}
					FBeamSeen& B = R.Beams[K];
					const double Share = S / FMath::Max(Cone.Length, 1.0);
					++B.Pixels;
					B.Brightest = FMath::Max(B.Brightest, Level(With.Pixels[Index]));
					if (Level(With.Pixels[Index]) > 200 && Level(Without.Pixels[Index]) <= 200)
					{
						++B.Above200;
					}
					if (Share <= 0.5)
					{
						B.Lower.Add(Diff);
					}
					R.Bins[FMath::Clamp(int32(Share * 10.0), 0, 9)].Add(Diff);
				}
			}
		}
		for (FBeamSeen& B : R.Beams)
		{
			R.Above200 += B.Above200;
			if (B.Lower.Num() >= 30)
			{
				const int32 M = Median(B.Lower);
				R.MinMedian = R.MinMedian < 0 ? M : FMath::Min(R.MinMedian, M);
				++R.Checked;
			}
		}
		return R;
	}

	static FString DescribeBeams(const FBeamMeasure& R)
	{
		FString Out;
		for (int32 K = 0; K < R.Beams.Num(); ++K)
		{
			const FBeamSeen& B = R.Beams[K];
			Out += FString::Printf(TEXT("%sbeam %d: lower half %d px, median %d; whole %d px, %d above 200, brightest %d, %d hidden by geometry"), K ? TEXT("; ") : TEXT(""), K,
				B.Lower.Num(), Median(B.Lower), B.Pixels, B.Above200, B.Brightest, B.Hidden);
		}
		FString Bins;
		for (int32 I = 0; I < R.Bins.Num(); ++I)
		{
			Bins += FString::Printf(TEXT("%s%.1f-%.1f: %d (%d px)"), I ? TEXT(", ") : TEXT(""), I * 0.1, I * 0.1 + 0.1, Median(R.Bins[I]), R.Bins[I].Num());
		}
		return Out + TEXT(". Along the length (share from the rod's tip: median difference): ") + Bins;
	}

	// --- the route's expected share, from the fixture rows' lengths and the channel ---

	static float ExpectedShare()
	{
		const UTextChannelComponent* C = Channel();
		if (!C || G.RowNames.IsEmpty())
		{
			return -1.f;
		}
		const int32 Index = G.RowNames.IndexOfByKey(C->GetCurrentRowName().ToString());
		if (Index == INDEX_NONE)
		{
			return -1.f;
		}
		double Total = 0.0, Before = 0.0;
		for (int32 I = 0; I < G.RowLengths.Num(); ++I)
		{
			Total += G.RowLengths[I];
			Before += I < Index ? G.RowLengths[I] : 0;
		}
		const int32 Len = C->GetCurrentText().Len();
		const double Share = Len > 0 ? FMath::Clamp(static_cast<double>(C->GetShownCells()) / Len, 0.0, 1.0) : 1.0;
		return static_cast<float>((Before + G.RowLengths[Index] * Share) / Total);
	}

	/** Each frame while the briefing is said: the route's share against the share of the fixture's text typed. */
	static bool SampleRoute(float)
	{
		const APrologueHologram* H = Holo();
		const UTextChannelComponent* C = Channel();
		if (!H || !C)
		{
			return true;
		}
		const float Actual = H->GetRouteProgress01();
		if (Actual + 1.e-4f < G.LastProgress)
		{
			++G.RouteBackwards;
		}
		const float Expected = ExpectedShare();
		const int32 Index = G.RowNames.IndexOfByKey(C->GetCurrentRowName().ToString());
		const int32 Shown = C->GetShownCells();
		const bool bHolding = Index != INDEX_NONE && Shown >= C->GetCurrentText().Len();
		if (Frozen())
		{
			// Behind the pause menu: the share stands still.
			if (G.FrozenProgress < 0.f)
			{
				G.FrozenProgress = Actual;
			}
			G.bMenuChecked = true;
			G.MenuMoves += FMath::IsNearlyEqual(Actual, G.FrozenProgress, 1.e-5f) ? 0 : 1;
			G.HoldFrames = 0;
		}
		else
		{
			G.FrozenProgress = -1.f;
			if (Expected >= 0.f)
			{
				// The terminal keeps the highest share reached (a row restarted by a Stop does not undo it): so does the expectation.
				G.ExpectedMax = FMath::Max(G.ExpectedMax, Expected);
				G.MaxRouteError = FMath::Max(G.MaxRouteError, static_cast<double>(FMath::Abs(Actual - G.ExpectedMax)));
				++G.RouteSamples;
			}
			// Between two rows (a row typed out and held, or none being typed after one): after three such frames, the share stands still.
			if (bHolding || (Index == INDEX_NONE && G.LastRow != INDEX_NONE) || (Index == INDEX_NONE && G.HoldFrames > 0))
			{
				++G.HoldFrames;
				if (G.HoldFrames > 3)
				{
					++G.GapSamples;
					G.GapMoves += FMath::IsNearlyEqual(Actual, G.LastProgress, 1.e-5f) ? 0 : 1;
				}
			}
			else
			{
				G.HoldFrames = 0;
			}
		}
		G.LastProgress = Actual;
		G.LastRow = Index;
		G.LastShown = Shown;
		return true;
	}

	static UDataTable* BuildFixture(const TArray<int32>& Lengths)
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), TEXT("DT_TlsHologramPrologue")));
		Table->RowStruct = FPrologueRow::StaticStruct();
		G.RowNames.Reset();
		G.RowLengths.Reset();
		for (int32 I = 0; I < Lengths.Num(); ++I)
		{
			FPrologueRow Row;
			Row.Group = PrologueGroups::Hologram;
			Row.Order = I + 1;
			FString Text = FString::Printf(TEXT("Fixture briefing line %d."), I + 1);
			while (Text.Len() < Lengths[I])
			{
				Text += TEXT(" Route");
			}
			Text = Text.Left(Lengths[I]);
			Row.Text = FText::FromString(Text);
			const FName Name(*FString::Printf(TEXT("Hologram_%d"), I + 1));
			Table->AddRow(Name, Row);
			G.RowNames.Add(Name.ToString());
			G.RowLengths.Add(Text.Len());
		}
		return Table;
	}

	static void UseFixture(const TArray<int32>& Lengths)
	{
		UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
		if (!G.bSavedTable)
		{
			G.SavedPrologue = Settings->PrologueTable;
			G.bSavedTable = true;
		}
		G.Fixture.Reset(BuildFixture(Lengths));
		Settings->PrologueTable = TSoftObjectPtr<UDataTable>(G.Fixture.Get());
		FString Text;
		for (int32 I = 0; I < G.RowLengths.Num(); ++I)
		{
			Text += FString::Printf(TEXT(" %s (%d)"), *G.RowNames[I], G.RowLengths[I]);
		}
		Note(FString::Printf(TEXT("fixture prologue table: %d Hologram rows:%s"), G.RowLengths.Num(), Text.IsEmpty() ? TEXT(" none") : *Text));
	}

	static const APrologueLineTrigger* HologramTrigger()
	{
		UWorld* World = W();
		for (TActorIterator<APrologueLineTrigger> It(World); World && It; ++It)
		{
			if (It->Group == PrologueGroups::Hologram)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static const FVector GHoloCentre(1810.f, 870.f, 250.f);

	// --- one fixture: reload the home map, then the route through the briefing ---

	static void AddRouteRun(TArray<FStep>& S, const FString& Label, const TArray<int32>& Lengths, bool bMenuInRow)
	{
		S.Add(Do(Label + TEXT(": the fixture, the home map again"), [Lengths]()
		{
			UseFixture(Lengths);
			UGameplayStatics::OpenLevel(W(), FName(TEXT("/Game/TheLastShift/Maps/Prologue_Home")));
			return 1.f;
		}));
		S.Add(WaitFor(Label + TEXT(": in Prologue_Home with the hologram, the terminal and the text channel"), []()
		{
			const APrologueHologram* H = Holo();
			return H && H->HasActorBegunPlay() && Briefing() && Channel() && TS() && HologramTrigger() && !HologramTrigger()->HasFired();
		}, 30.f, [Label]()
		{
			StandAt(450.0, 250.0, FVector(800.0, 250.0, 150.0));
			return 1.5f;
		}));
		S.Add(Do(Label + TEXT(": before the Hologram group"), [Label]()
		{
			const APrologueHologram* H = Holo();
			Check(H && H->GetRouteProgress01() == 0.f && H->GetRouteNodesShown() == 0 && !Briefing()->HasBriefingStarted(),
				FString::Printf(TEXT("%s: before the Hologram group no route (share %.3f, %d nodes, briefing started %d)"), *Label,
					H ? H->GetRouteProgress01() : -1.f, H ? H->GetRouteNodesShown() : -1, Briefing()->HasBriefingStarted() ? 1 : 0));
			G.MaxRouteError = 0.0;
			G.ExpectedMax = 0.f;
			G.RouteSamples = G.RouteBackwards = G.GapSamples = G.GapMoves = G.MenuMoves = G.HoldFrames = 0;
			G.LastProgress = 0.f;
			G.LastRow = -1;
			G.LastShown = -1;
			G.FrozenProgress = -1.f;
			G.bMenuChecked = false;
			G.Sampler = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&SampleRoute));
			// Into the monitor room: the Hologram line trigger asks for the group.
			StandAt(1810.0, 600.0, GHoloCentre);
			return 0.f;
		}));
		if (bMenuInRow)
		{
			// Halfway into the second row: the pause menu for 1.5 s, the share must not move.
			S.Add(WaitFor(Label + TEXT(": halfway into the second row"), []()
			{
				const UTextChannelComponent* C = Channel();
				return C && G.RowNames.Num() > 1 && C->GetCurrentRowName().ToString() == G.RowNames[1] && C->GetShownCells() * 2 >= C->GetCurrentText().Len();
			}, 30.f, []()
			{
				Check(OpenMenu(), TEXT("the pause menu opens halfway into the second row"));
				return 1.5f;
			}));
			S.Add(Do(Label + TEXT(": the menu closed"), [Label]()
			{
				CloseMenu();
				Check(G.bMenuChecked && G.MenuMoves == 0, FString::Printf(TEXT("%s: behind the pause menu the route's share stood still (%d samples moved)"), *Label, G.MenuMoves));
				return 0.f;
			}));
		}
		S.Add(WaitFor(Label + TEXT(": the briefing said"), []() { const APrologueBriefingTerminal* B = Briefing(); return B && B->IsBriefed(); }, 60.f, [Label]()
		{
			const APrologueHologram* H = Holo();
			Check(H && FMath::IsNearlyEqual(H->GetRouteProgress01(), 1.f) && H->GetRouteNodesShown() == 4,
				FString::Printf(TEXT("%s: the briefing said, the whole route (share %.3f, %d nodes)"), *Label, H ? H->GetRouteProgress01() : -1.f, H ? H->GetRouteNodesShown() : -1));
			return 3.f;
		}));
		S.Add(Do(Label + TEXT(": still whole later"), [Label, Lengths]()
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
			G.Sampler.Reset();
			const APrologueHologram* H = Holo();
			Check(H && FMath::IsNearlyEqual(H->GetRouteProgress01(), 1.f) && H->GetRouteNodesShown() == 4,
				FString::Printf(TEXT("%s: 3 s after the briefing the route is still whole (share %.3f, %d nodes)"), *Label, H ? H->GetRouteProgress01() : -1.f, H ? H->GetRouteNodesShown() : -1));
			if (!Lengths.IsEmpty())
			{
				Check(G.RouteSamples > 20 && G.MaxRouteError <= 0.02,
					FString::Printf(TEXT("%s: while it was typed the route's share followed the share of the text typed, rows weighted by their length (%d samples, largest difference %.4f)"),
						*Label, G.RouteSamples, G.MaxRouteError));
				Check(G.RouteBackwards == 0, FString::Printf(TEXT("%s: the share never went back (%d times)"), *Label, G.RouteBackwards));
				if (Lengths.Num() > 1)
				{
					Check(G.GapSamples > 0 && G.GapMoves == 0, FString::Printf(TEXT("%s: between two rows the share stood still (%d samples between rows, %d moved)"), *Label, G.GapSamples, G.GapMoves));
				}
			}
			return 0.f;
		}));
	}

	// --- the scan's capture with each part shown while Stopped ---

	static void AddCapture(TArray<FStep>& S, const FString& Pose, double X, double Y, const FString& Part, bool bEnabled, bool bPoints, bool bRoute, bool bBeams)
	{
		const FString Key = Pose + TEXT("_") + Part;
		S.Add(Do(Key + TEXT(": pose and parts"), [X, Y, bEnabled, bPoints, bRoute, bBeams]()
		{
			ResumeTime();
			StandAt(X, Y, GHoloCentre);
			if (APrologueHologram* H = Holo())
			{
				H->SetEnabled(bEnabled);
				H->SetPartsHiddenForTest(!bPoints, !bRoute, !bBeams);
				H->SetShownWhileStoppedForTest(bEnabled);
				H->SetMotionFrozenForTest(true);
			}
			return 0.8f;
		}));
		S.Add(Do(Key + TEXT(": Stop"), []() { const ULidarScanComponent* L = Lidar(); G.ScanCountBefore = L ? L->GetScanCount() : 0; StopTime(); return 0.f; }));
		S.Add(WaitFor(Key + TEXT(": the first scan's depth"), []()
		{
			const ULidarScanComponent* L = Lidar();
			return L && L->GetScanCount() > G.ScanCountBefore;
		}, 5.f, [Key, Part, bPoints, bRoute, bBeams, bEnabled]()
		{
			const APrologueHologram* H = Holo();
			if (bEnabled && H)
			{
				Check(H->GetPointsComponent()->IsVisible() == bPoints && H->GetRouteComponent()->IsVisible() == bRoute && H->GetBeamsComponent()->IsVisible() == bBeams,
					FString::Printf(TEXT("%s: while Stopped, for the test, only %s is shown (points %d, route %d, beams %d)"), *Key, *Part,
						H->GetPointsComponent()->IsVisible() ? 1 : 0, H->GetRouteComponent()->IsVisible() ? 1 : 0, H->GetBeamsComponent()->IsVisible() ? 1 : 0));
			}
			return 0.3f;
		}));
		S.Add(Do(Key + TEXT(": read the depth"), [Key]()
		{
			TArray<float> Depth;
			int32 DW = 0, DH = 0;
			FLidarScanContext Context;
			const ULidarScanComponent* L = Lidar();
			const bool bOk = L && L->ReadNewestScanDepth(Depth, DW, DH, Context);
			Note(FString::Printf(TEXT("%s: scan %d depth %d x %d read %d"), *Key, Context.ScanIndex, DW, DH, bOk ? 1 : 0));
			G.Depths.Add(Key, Depth);
			ResumeTime();
			if (APrologueHologram* H = Holo())
			{
				H->SetShownWhileStoppedForTest(false);
				H->SetPartsHiddenForTest(false, false, false);
				H->SetEnabled(true);
				H->SetMotionFrozenForTest(false);
			}
			return 1.f;
		}));
	}

	static void CompareCapture(const FString& Pose, const FString& Part)
	{
		const TArray<float>* Base = G.Depths.Find(Pose + TEXT("_none"));
		const TArray<float>* With = G.Depths.Find(Pose + TEXT("_") + Part);
		int32 Differ = 0;
		double Largest = 0.0;
		if (Base && With && Base->Num() == With->Num())
		{
			for (int32 I = 0; I < Base->Num(); ++I)
			{
				const double D = FMath::Abs((*Base)[I] - (*With)[I]);
				Differ += D > 0.5 ? 1 : 0;
				Largest = FMath::Max(Largest, D);
			}
		}
		Check(Base && With && Base->Num() > 1000 && Base->Num() == With->Num() && Differ == 0,
			FString::Printf(TEXT("%s: the scan's capture with %s shown is the capture without the hologram (%d texels, %d differ by more than 0.5 cm, largest %.2f cm)"),
				*Pose, *Part, Base ? Base->Num() : -1, Differ, Largest));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The run
	// ---------------------------------------------------------------------------------------------------------------

	static bool Tick(float)
	{
		if (FPlatformTime::Seconds() < G.WaitUntil)
		{
			return true;
		}
		if (!G.Steps.IsValidIndex(G.Next))
		{
			if (G.Sampler.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
				G.Sampler.Reset();
			}
			if (G.bSavedTable)
			{
				GetMutableDefault<UTheLastShiftSettings>()->PrologueTable = G.SavedPrologue;
				G.bSavedTable = false;
			}
			G.Fixture.Reset();
			if (G.ShotHandle.IsValid())
			{
				UGameViewportClient::OnScreenshotCaptured().Remove(G.ShotHandle);
				G.ShotHandle.Reset();
			}
			UE_LOG(LogTheLastShift, Display, TEXT("HologramTest summary (%s): %d checks passed, %d failed."), G.Failed ? TEXT("FAIL") : TEXT("PASS"), G.Passed, G.Failed);
			if (G.bQuit)
			{
				FPlatformMisc::RequestExit(false, TEXT("tls.HologramTest"));
			}
			G.Ticker.Reset();
			return false;
		}
		FStep& Step = G.Steps[G.Next];
		if (Step.Until)
		{
			if (G.StepStarted < 0.0)
			{
				G.StepStarted = FPlatformTime::Seconds();
			}
			if (!Step.Until())
			{
				if (FPlatformTime::Seconds() - G.StepStarted > Step.Timeout)
				{
					Check(false, Step.Label + TEXT(" (timed out)"));
					G.StepStarted = -1.0;
					++G.Next;
				}
				return true;
			}
			G.StepStarted = -1.0;
		}
		++G.Next;
		const float Wait = Step.Run ? Step.Run() : 0.f;
		G.WaitUntil = FPlatformTime::Seconds() + Wait;
		return true;
	}

	/**
	 * Brief 48, tuning only (tls.HologramTest 0 tune): from the doorway, the motion held, the beams' measure of the check for
	 * each pair of BeamLevel and BeamFadePower set for the session (the first the settings' own), each at its level and at
	 * its level less the flicker's share (the lowest a held frame can show); TUNE lines in the log; the settings put back.
	 */
	static void AddTune(TArray<FStep>& S)
	{
		const FHologramTuning Saved = UTheLastShiftSettings::Get()->GetHologramTuning();
		S.Add(Do(TEXT("tune: the doorway, the route whole, motion held"), []()
		{
			StandAt(1810.0, 230.0, GHoloCentre);
			Holo()->SetRouteProgressOverride(1.f);
			return 1.f;
		}));
		TArray<FVector2D> Pairs = { FVector2D(Saved.BeamLevel, Saved.BeamFadePower) };
		for (const float Power : { 5.f, 3.f, 2.f, 1.5f })
		{
			for (const float Lvl : { 0.075f, 0.1f, 0.15f, 0.2f })
			{
				Pairs.Add(FVector2D(Lvl, Power));
			}
		}
		for (const FVector2D& Pair : Pairs)
		{
			for (const float Share : { 1.f, 1.f - Saved.BeamFlicker })
			{
				// Only the settings' own pair, at its level, is saved (the old values' shot); the others are measured, not saved.
				const bool bSave = &Pair == &Pairs[0] && Share >= 1.f;
				const FString Key = FString::Printf(TEXT("%sTune_L%.3f_P%.2f%s"), bSave ? TEXT("") : TEXT("_"), Pair.X, Pair.Y, Share < 1.f ? TEXT("_low") : TEXT(""));
				S.Add(Do(Key + TEXT(": set"), [Pair, Share]()
				{
					UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
					Settings->Hologram.BeamLevel = Pair.X * Share;
					Settings->Hologram.BeamFadePower = Pair.Y;
					APrologueHologram* H = Holo();
					H->SetMotionFrozenForTest(true);
					H->Rebuild();
					H->SetBeamsHiddenForTest(false);
					return 0.4f;
				}));
				AddShot(S, Key);
				S.Add(Do(Key + TEXT(": hidden"), []() { Holo()->SetBeamsHiddenForTest(true); return 0.3f; }));
				AddShot(S, Key + TEXT("_NoBeams"));
				S.Add(Do(Key + TEXT(": measured"), [Key, Pair, Share]()
				{
					Holo()->SetBeamsHiddenForTest(false);
					const FShotPixels* With = G.Shots.Find(Key);
					const FShotPixels* Without = G.Shots.Find(Key + TEXT("_NoBeams"));
					const FBeamMeasure R = (With && Without) ? MeasureBeams(*With, *Without) : FBeamMeasure();
					Note(FString::Printf(TEXT("TUNE level %.3f power %.2f%s: least lower-half median %d (%d beams), %d px above 200: %s"), Pair.X, Pair.Y,
						Share < 1.f ? *FString::Printf(TEXT(" (held at %.3f, the flicker's low)"), Pair.X * Share) : TEXT(""), R.MinMedian, R.Checked, R.Above200, *DescribeBeams(R)));
					return 0.f;
				}));
			}
		}
		S.Add(Do(TEXT("tune: the settings back"), [Saved]()
		{
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			Settings->Hologram.BeamLevel = Saved.BeamLevel;
			Settings->Hologram.BeamFadePower = Saved.BeamFadePower;
			APrologueHologram* H = Holo();
			H->Rebuild();
			H->SetMotionFrozenForTest(false);
			H->SetRouteProgressOverride(-1.f);
			return 0.f;
		}));
	}

	static void Start(bool bQuit, bool bTune)
	{
		if (G.Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Ticker);
		}
		if (G.Sampler.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
		}
		G = FRun();
		G.bQuit = bQuit;
		TArray<FStep>& S = G.Steps;
		const FHologramTuning T = UTheLastShiftSettings::Get()->GetHologramTuning();

		S.Add(WaitFor(TEXT("a game world in Prologue_Home with the hologram"), []() { const APrologueHologram* H = Holo(); return H && H->HasActorBegunPlay() && Pawn() && TS() && Lidar(); }, 20.f));
		if (bTune)
		{
			AddTune(S);
			Note(FString::Printf(TEXT("===== tuning: %d steps ====="), S.Num()));
			G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
			return;
		}

		// --- parts and colours ---
		S.Add(Do(TEXT("parts and colours"), [T]()
		{
			const APrologueHologram* H = Holo();
			const FScanColors Colors = UTheLastShiftSettings::GetScanColors();
			const auto MidColor = [](const UInstancedStaticMeshComponent* C)
			{
				UMaterialInstanceDynamic* M = C ? Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)) : nullptr;
				return M ? M->K2_GetVectorParameterValue(TEXT("Color")) : FLinearColor(-1, -1, -1, -1);
			};
			Check(H->GetPointCount() == T.Points, FString::Printf(TEXT("the hologram has its %d points (%d)"), T.Points, H->GetPointCount()));
			Check(H->GetRouteComponent()->GetInstanceCount() == 7, FString::Printf(TEXT("the route is 4 nodes and 3 lines (%d instances)"), H->GetRouteComponent()->GetInstanceCount()));
			Check(H->GetBeamCount() == T.BeamRods, FString::Printf(TEXT("one beam for each of the base's %d rods (%d)"), T.BeamRods, H->GetBeamCount()));
			Check(MidColor(H->GetPointsComponent()).Equals(Colors.Scan, 1.e-3f) && H->GetPointColor().Equals(Colors.Scan, 1.e-3f),
				FString::Printf(TEXT("the points are in ScanColors.Scan %s (material %s)"), *Colors.Scan.ToString(), *MidColor(H->GetPointsComponent()).ToString()));
			Check(MidColor(H->GetBeamsComponent()).Equals(Colors.Scan, 1.e-3f),
				FString::Printf(TEXT("the beams are in ScanColors.Scan, the hologram's colour (material %s)"), *MidColor(H->GetBeamsComponent()).ToString()));
			Check(MidColor(H->GetRouteComponent()).Equals(Colors.Entity, 1.e-3f) && H->GetRouteColor().Equals(Colors.Entity, 1.e-3f),
				FString::Printf(TEXT("the route is in ScanColors.Entity %s (material %s)"), *Colors.Entity.ToString(), *MidColor(H->GetRouteComponent()).ToString()));
			Check(H->GetPointsComponent()->bHiddenInSceneCapture && H->GetRouteComponent()->bHiddenInSceneCapture && H->GetBeamsComponent()->bHiddenInSceneCapture,
				TEXT("every part is hidden in scene captures"));
			Check(H->GetPointsComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision && H->GetRouteComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				&& H->GetBeamsComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision, TEXT("no part collides"));
			return 0.f;
		}));

		// --- the turn, the drift, the beams over 6 s of Normal time ---
		S.Add(Do(TEXT("sampling the motion"), []()
		{
			StandAt(1810.0, 230.0, GHoloCentre);
			const APrologueHologram* H = Holo();
			G.TurnTimes.Reset();
			G.TurnDegrees.Reset();
			G.Rest.Reset();
			G.PrevNow.Reset();
			G.BeamTransforms.Reset();
			for (int32 I = 0; I < H->GetPointCount(); I += 10)
			{
				FVector Rest, Now;
				H->GetPointLocal(I, Rest, Now);
				G.Rest.Add(Rest);
				G.PrevNow.Add(Now);
			}
			for (int32 I = 0; I < H->GetBeamsComponent()->GetInstanceCount(); ++I)
			{
				FTransform X;
				H->GetBeamsComponent()->GetInstanceTransform(I, X, true);
				G.BeamTransforms.Add(X);
			}
			G.Sampler = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				const APrologueHologram* H = Holo();
				if (!H)
				{
					return true;
				}
				G.TurnTimes.Add(RunSeconds());
				G.TurnDegrees.Add(H->GetTurnDegrees());
				const FHologramTuning Tn = UTheLastShiftSettings::Get()->GetHologramTuning();
				for (int32 K = 0; K < G.Rest.Num(); ++K)
				{
					FVector Rest, Now;
					H->GetPointLocal(K * 10, Rest, Now);
					const double Drift = FVector::Dist(Rest, Now);
					G.MaxDrift = FMath::Max(G.MaxDrift, Drift);
					G.MaxMove = FMath::Max(G.MaxMove, Drift);
					G.MaxOffFace = FMath::Max(G.MaxOffFace, FMath::Abs(FMath::Abs(Now.X) + FMath::Abs(Now.Y) + FMath::Abs(Now.Z) - H->GetHalfSize()));
					G.MaxStep = FMath::Max(G.MaxStep, FVector::Dist(G.PrevNow[K], Now));
					G.PrevNow[K] = Now;
				}
				for (int32 I = 0; I < H->GetBeamCount(); ++I)
				{
					FVector A, B;
					float Sp = 0.f, Far = 0.f, Level = 0.f;
					H->GetBeam(I, A, B, Sp, Far, Level);
					G.LevelMin = FMath::Min(G.LevelMin, Level);
					G.LevelMax = FMath::Max(G.LevelMax, Level);
					FTransform X;
					H->GetBeamsComponent()->GetInstanceTransform(I, X, true);
					G.bBeamsMoved |= !X.Equals(G.BeamTransforms[I], 0.01f);
				}
				return true;
			}));
			return 6.f;
		}));
		S.Add(Do(TEXT("the motion measured"), [T]()
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
			G.Sampler.Reset();
			// The turn's speed: unwrap the angles and fit over the run's clock.
			double Unwrapped = 0.0, Prev = G.TurnDegrees.Num() ? G.TurnDegrees[0] : 0.0;
			for (int32 I = 1; I < G.TurnDegrees.Num(); ++I)
			{
				double D = G.TurnDegrees[I] - Prev;
				D += D < -180.0 ? 360.0 : (D > 180.0 ? -360.0 : 0.0);
				Unwrapped += D;
				Prev = G.TurnDegrees[I];
			}
			const double Seconds = G.TurnTimes.Num() > 1 ? G.TurnTimes.Last() - G.TurnTimes[0] : 0.0;
			const double Speed = Seconds > 0.0 ? Unwrapped / Seconds : 0.0;
			Check(FMath::Abs(Speed - 360.0 / T.TurnSeconds) <= 0.01 * 360.0 / T.TurnSeconds,
				FString::Printf(TEXT("the hologram turns at %.3f deg/s over %.2f s of the run's clock (one turn in %.1f s: %.3f deg/s)"), Speed, Seconds, T.TurnSeconds, 360.0 / T.TurnSeconds));
			Check(G.MaxDrift <= T.DriftDistance + 0.01 && G.MaxMove >= 0.5 * T.DriftDistance,
				FString::Printf(TEXT("the points drift within %.1f cm of their rest (%d sampled, largest %.3f cm) and do move"), T.DriftDistance, G.Rest.Num(), G.MaxDrift));
			Check(G.MaxOffFace <= 0.05, FString::Printf(TEXT("they stay on the octahedron's faces (largest distance off its surface %.4f cm)"), G.MaxOffFace));
			// Smooth, not a vibration: no frame step larger than the drift's fastest speed allows (2 pi A / (T (1 - spread))) over a 1/20 s frame.
			const double MaxSpeed = 2.0 * PI * T.DriftDistance / (T.DriftSeconds * (1.0 - T.DriftPeriodSpread));
			Check(G.MaxStep <= MaxSpeed * 0.05 + 0.01, FString::Printf(TEXT("they move smoothly (largest step between two frames %.3f cm, at most %.3f cm at 20 fps)"), G.MaxStep, MaxSpeed * 0.05));
			Check(!G.bBeamsMoved, TEXT("the beams did not turn or change shape while the hologram turned"));
			Check(G.LevelMax > G.LevelMin && G.LevelMin >= T.BeamLevel * (1.f - T.BeamFlicker) - 1.e-4f && G.LevelMax <= T.BeamLevel * (1.f + T.BeamFlicker) + 1.e-4f,
				FString::Printf(TEXT("the beams' level flickers within %.3f..%.3f (%.4f..%.4f): the level only"), T.BeamLevel * (1.f - T.BeamFlicker), T.BeamLevel * (1.f + T.BeamFlicker), G.LevelMin, G.LevelMax));
			return 0.f;
		}));

		// --- the beams' geometry ---
		S.Add(Do(TEXT("the beams"), [T]()
		{
			const APrologueHologram* H = Holo();
			UWorld* World = W();
			const FVector Centre = H->GetActorLocation();
			const FTransform ToWorld = H->GetPointsComponent()->GetComponentTransform();
			// The cone that holds the hologram, measured here on its orbit (it turns: a circle at its equator, its two poles).
			const float Outer = H->GetHalfSize() + T.BeamMargin + T.PointSize;
			for (int32 I = 0; I < H->GetBeamCount(); ++I)
			{
				FVector A, B;
				float Spread = 0.f, Far = 0.f, Level = 0.f;
				H->GetBeam(I, A, B, Spread, Far, Level);
				const FVector Axis = (B - A).GetSafeNormal();
				float Holding = 0.f;
				for (int32 K = 0; K < 360; ++K)
				{
					const float Angle = 2.f * PI * K / 360.f;
					const FVector P = Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Outer;
					Holding = FMath::Max(Holding, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Axis, (P - A).GetSafeNormal()), -1.f, 1.f))));
				}
				for (const float Sign : { 1.f, -1.f })
				{
					Holding = FMath::Max(Holding, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Axis, (Centre + FVector(0, 0, Sign * Outer) - A).GetSafeNormal()), -1.f, 1.f))));
				}
				int32 Inside = 0, Count = 0;
				for (int32 P = 0; P < H->GetPointCount(); P += 5)
				{
					FVector Rest, Now;
					H->GetPointLocal(P, Rest, Now);
					const float Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Axis, (ToWorld.TransformPosition(Now) - A).GetSafeNormal()), -1.f, 1.f)));
					Inside += Ang <= Spread ? 1 : 0;
					++Count;
				}
				FHitResult Hit;
				FCollisionQueryParams Params(TEXT("TlsHologramTestRod"), true);
				const bool bRod = World->LineTraceSingleByChannel(Hit, A + FVector(0, 0, 30), A - FVector(0, 0, 30), ECC_Visibility, Params) && FVector::Dist(Hit.ImpactPoint, A) <= 1.f;
				const float AxisError = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Axis, (Centre - A).GetSafeNormal()), -1.f, 1.f)));
				const float Length = FVector::Dist(A, B);
				Check(bRod && AxisError <= 0.05f && FMath::Abs(Spread - T.BeamSpreadShare * Holding) <= 0.1f && FMath::Abs(Length - T.BeamLength * Far) <= 0.5f,
					FString::Printf(TEXT("beam %d: from its rod's tip (a trace down meets %s %.2f cm away), axis on the centre (%.3f deg off), spread %.2f deg = %.2f of the %.2f deg that hold the hologram, length %.1f cm = %.4f of %.1f cm to the far side; %d of %d sampled points inside its spread"),
						I, bRod ? *GetNameSafe(Hit.GetActor()) : TEXT("nothing"), bRod ? FVector::Dist(Hit.ImpactPoint, A) : -1.f, AxisError, Spread, T.BeamSpreadShare, Holding, Length, T.BeamLength, Far, Inside, Count));
				if (I == 0)
				{
					Note(FString::Printf(TEXT("beam 0 ends %.1f cm from the hologram's centre (BeamLength %.4f; the centre at %.4f of the way)"), FVector::Dist(B, Centre), T.BeamLength, FVector::Dist(A, Centre) / Far));
				}
			}
			return 0.f;
		}));

		// --- the route turns with the hologram ---
		S.Add(Do(TEXT("the route turns with the hologram: first reading"), []()
		{
			Holo()->SetRouteProgressOverride(1.f);
			G.TurnTimes = { Holo()->GetTurnDegrees() };
			G.Rest = { Holo()->GetRouteNodeWorld(0) };
			return 2.f;
		}));
		S.Add(Do(TEXT("the route turns with the hologram"), []()
		{
			const APrologueHologram* H = Holo();
			double Turned = H->GetTurnDegrees() - G.TurnTimes[0];
			Turned += Turned < 0.0 ? 360.0 : 0.0;
			const FVector C = H->GetActorLocation();
			const FVector A = G.Rest[0] - C;
			const FVector B = H->GetRouteNodeWorld(0) - C;
			double NodeTurned = FMath::RadiansToDegrees(FMath::Atan2(B.Y, B.X) - FMath::Atan2(A.Y, A.X));
			NodeTurned += NodeTurned < 0.0 ? 360.0 : 0.0;
			Check(FMath::Abs(NodeTurned - Turned) <= 0.2 && FMath::IsNearlyEqual(A.Z, B.Z, 0.01) && Turned > 5.0,
				FString::Printf(TEXT("node 1 turned %.2f deg about the hologram's vertical axis while the hologram turned %.2f deg"), NodeTurned, Turned));
			return 0.f;
		}));

		// --- behind the pause menu ---
		S.Add(Do(TEXT("the pause menu"), []()
		{
			const APrologueHologram* H = Holo();
			Check(OpenMenu(), TEXT("the pause menu opens"));
			G.TurnDegrees = { H->GetTurnDegrees() };
			FVector Rest, Now;
			H->GetPointLocal(37, Rest, Now);
			G.PrevNow = { Now };
			FVector A, B;
			float Sp = 0.f, Far = 0.f, Level = 0.f;
			H->GetBeam(0, A, B, Sp, Far, Level);
			G.LevelMin = Level;
			return 1.5f;
		}));
		S.Add(Do(TEXT("behind the pause menu nothing moved"), []()
		{
			const APrologueHologram* H = Holo();
			FVector Rest, Now;
			H->GetPointLocal(37, Rest, Now);
			FVector A, B;
			float Sp = 0.f, Far = 0.f, Level = 0.f;
			H->GetBeam(0, A, B, Sp, Far, Level);
			Check(Frozen() && FMath::IsNearlyEqual(H->GetTurnDegrees(), static_cast<float>(G.TurnDegrees[0]), 1.e-4f) && Now.Equals(G.PrevNow[0], 1.e-4f) && FMath::IsNearlyEqual(Level, G.LevelMin, 1.e-6f),
				FString::Printf(TEXT("behind the pause menu for 1.5 s the turn (%.4f -> %.4f deg), a point's drift and the beams' flicker stood still"), G.TurnDegrees[0], H->GetTurnDegrees()));
			CloseMenu();
			return 1.f;
		}));
		S.Add(Do(TEXT("after the menu it moves again"), []()
		{
			Check(!FMath::IsNearlyEqual(Holo()->GetTurnDegrees(), static_cast<float>(G.TurnDegrees[0]), 1.e-3f), TEXT("once the menu is closed the hologram turns again"));
			return 0.f;
		}));

		// --- the route's colour on a shot from the doorway, with and without the beams ---
		S.Add(Do(TEXT("the doorway, the route whole, motion held"), []()
		{
			StandAt(1810.0, 230.0, GHoloCentre);
			Holo()->SetRouteProgressOverride(1.f);
			return 1.f;
		}));
		S.Add(Do(TEXT("hold"), []() { Holo()->SetMotionFrozenForTest(true); return 0.3f; }));
		AddShot(S, TEXT("Route_Doorway"));
		S.Add(Do(TEXT("beams hidden"), []() { Holo()->SetBeamsHiddenForTest(true); return 0.3f; }));
		AddShot(S, TEXT("Route_Doorway_NoBeams"));
		S.Add(Do(TEXT("the route's colour"), []()
		{
			APrologueHologram* H = Holo();
			H->SetBeamsHiddenForTest(false);
			H->SetMotionFrozenForTest(false);
			H->SetRouteProgressOverride(-1.f);
			const FShotPixels* With = G.Shots.Find(TEXT("Route_Doorway"));
			const FShotPixels* Without = G.Shots.Find(TEXT("Route_Doorway_NoBeams"));
			// The route's pixels: where the entity's colour dominates in the shot without the beams.
			double SW[3] = { 0, 0, 0 }, SN[3] = { 0, 0, 0 };
			int32 N = 0;
			if (With && Without && With->Pixels.Num() == Without->Pixels.Num())
			{
				for (int32 I = 0; I < Without->Pixels.Num(); ++I)
				{
					const FColor& P = Without->Pixels[I];
					if (P.R > 150 && P.R > 1.6 * P.G && P.R > 1.8 * P.B)
					{
						const FColor& Q = With->Pixels[I];
						SW[0] += Q.R; SW[1] += Q.G; SW[2] += Q.B;
						SN[0] += P.R; SN[1] += P.G; SN[2] += P.B;
						++N;
					}
				}
			}
			const double D = N ? FMath::Max3(FMath::Abs(SW[0] - SN[0]), FMath::Abs(SW[1] - SN[1]), FMath::Abs(SW[2] - SN[2])) / N : 999.0;
			const bool bHue = N && SW[0] / N > 1.6 * SW[1] / N && SW[0] / N > 1.8 * SW[2] / N;
			Check(N > 200 && bHue && D <= 10.0,
				FString::Printf(TEXT("the route is in the entity's colour on the shot from the doorway: %d pixels, mean (%.0f, %.0f, %.0f) through the beams, (%.0f, %.0f, %.0f) without them (largest difference %.1f levels)"),
					N, N ? SW[0] / N : 0.0, N ? SW[1] / N : 0.0, N ? SW[2] / N : 0.0, N ? SN[0] / N : 0.0, N ? SN[1] / N : 0.0, N ? SN[2] / N : 0.0, D));

			// Brief 48: what the player sees of the beams, the same two shots (the motion held, so that only the beams differ):
			// over each beam's lower half (from its rod's tip to half its length; pixels where geometry stands in front left
			// out) the median difference with the beams shown and hidden at least 20 levels (8-bit, the largest channel), and
			// no pixel of a beam pushed above 200 levels by it. A beam with fewer than 30 such pixels is not seen from there.
			const FBeamMeasure R = (With && Without) ? MeasureBeams(*With, *Without) : FBeamMeasure();
			Check(R.Checked >= 6 && R.MinMedian >= 20 && R.Above200 == 0,
				FString::Printf(TEXT("the beams are seen from the doorway: over each beam's lower half the median difference with the beams shown and hidden is at least 20 levels (the least %d, %d beams measured, 6 needed) and no pixel of a beam is pushed above 200 levels (%d) (BeamLevel %.3f, BeamFadePower %.2f): %s"),
					R.MinMedian, R.Checked, R.Above200, UTheLastShiftSettings::Get()->GetHologramTuning().BeamLevel, UTheLastShiftSettings::Get()->GetHologramTuning().BeamFadePower,
					*DescribeBeams(R)));
			return 0.f;
		}));

		// --- while Stopped: each part hidden, nothing on screen before the first scan ---
		for (const FVector& Pose : { FVector(1810.0, 230.0, 0.0), FVector(2160.0, 560.0, 0.0) })
		{
			const FString Name = Pose.X < 2000.f ? TEXT("Doorway") : TEXT("Close");
			S.Add(Do(Name + TEXT(": Stop"), [Pose]()
			{
				StandAt(Pose.X, Pose.Y, GHoloCentre);
				return 0.8f;
			}));
			S.Add(Do(Name + TEXT(": Stopped"), []() { StopTime(); return 0.25f; }));
			AddShot(S, TEXT("Stopped_") + Name);
			S.Add(Do(Name + TEXT(": hidden while Stopped"), [Name]()
			{
				const APrologueHologram* H = Holo();
				Check(IsStopped() && !H->GetPointsComponent()->IsVisible() && !H->GetRouteComponent()->IsVisible() && !H->GetBeamsComponent()->IsVisible(),
					FString::Printf(TEXT("%s: while Stopped the points, the route and the beams are each hidden"), *Name));
				const FShotPixels* Shot = G.Shots.Find(TEXT("Stopped_") + Name);
				int32 Lit = 0, Max = 0;
				if (Shot)
				{
					// The hologram's place on screen: its bounds, with its beams down to the base, projected.
					FVector2D Lo(1.e9, 1.e9), Hi(-1.e9, -1.e9);
					for (const FVector& P : { FVector(1610, 670, 45), FVector(2010, 1070, 450), FVector(1610, 1070, 45), FVector(2010, 670, 450), FVector(1610, 670, 450), FVector(2010, 1070, 45), FVector(1610, 1070, 450), FVector(2010, 670, 45) })
					{
						FVector2D S2;
						if (PC()->ProjectWorldLocationToScreen(P, S2, false))
						{
							Lo = FVector2D(FMath::Min(Lo.X, S2.X), FMath::Min(Lo.Y, S2.Y));
							Hi = FVector2D(FMath::Max(Hi.X, S2.X), FMath::Max(Hi.Y, S2.Y));
						}
					}
					for (int32 Y = FMath::Max(0, int32(Lo.Y)); Y < FMath::Min(Shot->Height, int32(Hi.Y)); ++Y)
					{
						for (int32 X = FMath::Max(0, int32(Lo.X)); X < FMath::Min(Shot->Width, int32(Hi.X)); ++X)
						{
							const int32 L = Level(Shot->Pixels[Y * Shot->Width + X]);
							Lit += L > 10 ? 1 : 0;
							Max = FMath::Max(Max, L);
						}
					}
				}
				Check(Shot && Lit == 0, FString::Printf(TEXT("%s: the shot while Stopped, before the first scan, is black where the hologram is (%d pixels above 10 levels, brightest %d)"), *Name, Lit, Max));
				ResumeTime();
				return 1.f;
			}));
		}

		// --- the scan's capture with each part shown while Stopped (test only) against the capture without the hologram ---
		for (const FVector& Pose : { FVector(1810.0, 230.0, 0.0), FVector(2160.0, 560.0, 0.0) })
		{
			const FString Name = Pose.X < 2000.f ? TEXT("Doorway") : TEXT("Close");
			AddCapture(S, Name, Pose.X, Pose.Y, TEXT("none"), false, false, false, false);
			AddCapture(S, Name, Pose.X, Pose.Y, TEXT("points"), true, true, false, false);
			AddCapture(S, Name, Pose.X, Pose.Y, TEXT("route"), true, false, true, false);
			AddCapture(S, Name, Pose.X, Pose.Y, TEXT("beams"), true, false, false, true);
			AddCapture(S, Name, Pose.X, Pose.Y, TEXT("all"), true, true, true, true);
			S.Add(Do(Name + TEXT(": captures compared"), [Name]()
			{
				for (const TCHAR* Part : { TEXT("points"), TEXT("route"), TEXT("beams"), TEXT("all") })
				{
					CompareCapture(Name, Part);
				}
				return 0.f;
			}));
		}

		// --- the route by the briefing, three fixtures (each reloads the home map) ---
		AddRouteRun(S, TEXT("two rows"), { 45, 140 }, false);
		AddRouteRun(S, TEXT("four rows"), { 20, 90, 35, 60 }, true);
		AddRouteRun(S, TEXT("no row"), {}, false);

		Note(FString::Printf(TEXT("===== %d steps ====="), S.Num()));
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsHologramTestCommand(
	TEXT("tls.HologramTest"),
	TEXT("tls.HologramTest [Quit=0] [tune]: (fresh PIE or a package, in Prologue_Home) Brief 18's hologram: parts and colours, the turn, the drift, the beams, behind the pause menu, the route's colour on a shot and the beams seen on it (Brief 48: over each beam's lower half a median difference of 20 levels or more with the beams shown and hidden, no pixel pushed above 200), nothing while Stopped or in the scan's capture (each part, two poses), the route drawn by the briefing with fixture rows (two rows, four rows, none). tune: the beams' measure only, for pairs of BeamLevel and BeamFadePower set for the session (TUNE lines). Shots to Saved/Screenshots/Brief48."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		HologramTest::Start(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0, Args.Contains(TEXT("tune")));
	}));

#endif // !UE_BUILD_SHIPPING

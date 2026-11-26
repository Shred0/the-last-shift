// SPDX-License-Identifier: MIT

// tls.StyleCheck [Quit] (Brief 27): one look for menus, HUD and text, the static at the opening's cut, black screens and
// the monitor room's rack lights. Development builds only. It checks what the map it runs in has, in PIE or a package:
//  - MainMenu: the font of every text of the main menu, the settings and rebinding screens, the credits, the select-act
//    list, a text scene on white and on black, the ending's choice, the loss screen and the travel screen (the one font of
//    StdCore's style, else a failure); the static at a scene's cut (noise for StaticSeconds, then a fade over
//    StaticFadeSeconds; the screen's mean level steady from frame to frame within 5 levels; the glyphs' contrast against
//    the patch as on the plain background, within 10 levels; both values changed for the session and followed); the
//    longest row of each real narrative table against its box (lines with the old font and the new, against the lines
//    the box holds; nothing cut); the travel screen's label against its width.
//  - Prologue_Home: the text channel's font with a home AI row and an engineer's row (fixture rows); the monitors' plates
//    and the terminals' plates (black from 2 m, mean level at most 20); the rack lights (blinking each on its own rhythm,
//    hidden while Stopped with a frame of at most 2 levels before the first scan, not in a scan's capture, still behind
//    the pause menu, the values followed from the settings).
//  - Area2_ServerHalls: a log console unpaid and paid (its screen black from 2 m with the flashlight while it shows no
//    entry, the entry shown once paid), a gate terminal's screen, the twin AI's terminal's plate, the Interact prompt, the
//    pause menu and the archive; the font of each.
// Shots to Saved/Screenshots/Brief27. Every value it changes (energy, the archive, the settings) is put back.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Area/AreaGenerator.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Widgets/SWindow.h"
#include "Debug/NarrativeFixture.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LogTerminal.h"
#include "Energy/PowerTerminal.h"
#include "Energy/TwinAITerminal.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flow/StdFlowSubsystem.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "LidarScanComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueMonitorRoom.h"
#include "Rendering/SlateRenderer.h"
#include "Run/ArchiveScreenWidget.h"
#include "Run/RunActListWidget.h"
#include "Run/RunEndingWidget.h"
#include "Run/RunMainMenuWidget.h"
#include "Run/RunPauseMenuWidget.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TextChannelWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "Settings/StdProjectSettings.h"
#include "Styling/CoreStyle.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "TlsDebugCommon.h"
#include "UI/InteractPromptWidget.h"
#include "UI/StdScreenWidget.h"
#include "UI/StdTextSceneWidget.h"
#include "UI/StdUIStyle.h"
#include "UI/StdUITheme.h"
#include "UnrealClient.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace StyleCheck
{
	struct FStep
	{
		FString Label;
		TFunction<float()> Run;
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	struct FShot
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
		FString Map;
		FTSTicker::FDelegateHandle Ticker;
		FTSTicker::FDelegateHandle Sampler;
		FDelegateHandle ShotHandle;
		FString ShotName;
		bool bShotReady = false;
		FShot Shot;
		TMap<FString, FShot> Shots;
		// fonts
		int32 FontTexts = 0;
		int32 FontOthers = 0;
		// the scene
		TWeakObjectPtr<URunTextSceneWidget> Scene;
		TArray<double> StaticMeans;
		double StaticStartReal = 0.0;
		float SavedStatic = 0.f, SavedFade = 0.f;
		int32 ContrastStatic = -1, ContrastPlain = -1;
		// the rack lights
		TArray<int32> Toggles;
		TArray<uint8> LastOn;
		int32 BlinkFrames = 0;
		int32 NeighbourDiffer = 0, NeighbourPairs = 0;
		TArray<uint8> MenuOn;
		TMap<FString, TArray<float>> Depths;
		FRackLightsTuning SavedRack;
		// Area 2
		TWeakObjectPtr<ALogTerminal> Console;
		int32 SavedEnergy = 0;
		FSuitArchive SavedArchive;
		bool bArchiveSaved = false;
		TWeakObjectPtr<AActor> Spawned;
		bool bFixture = false;
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

	template <typename T> static T* FirstWidget()
	{
		UWorld* World = W();
		for (TObjectIterator<T> It; It; ++It)
		{
			if (World && It->GetWorld() == World && It->IsInViewport())
			{
				return *It;
			}
		}
		for (TObjectIterator<T> It; It; ++It)
		{
			if (World && It->GetWorld() == World)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static void Note(const FString& Text) { UE_LOG(LogTheLastShift, Display, TEXT("StyleCheck   ..  %s"), *Text); }
	static void Check(bool bOk, const FString& Text)
	{
		(bOk ? G.Passed : G.Failed)++;
		UE_LOG(LogTheLastShift, Display, TEXT("StyleCheck   %s  %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *Text);
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

	static int32 Level(const FColor& C) { return FMath::Max3(C.R, C.G, C.B); }
	static double Luma(const FColor& C) { return 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B; }

	// --- shots ---

	static FString ShotPath(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief27"), G.Map + TEXT("_") + Name + TEXT(".png")));
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		G.Shot.Width = Width;
		G.Shot.Height = Height;
		G.Shot.Pixels = Pixels;
		for (FColor& C : G.Shot.Pixels)
		{
			C.A = 255;
		}
		G.Shots.Add(G.ShotName, G.Shot);
		if (!G.ShotName.StartsWith(TEXT("_")))
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(G.Shot.Pixels.GetData(), G.Shot.Pixels.Num()), Png);
			const FString Path = ShotPath(G.ShotName);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Png, *Path);
			Note(FString::Printf(TEXT("shot %s (%d x %d): %s"), *G.ShotName, Width, Height, *Path));
		}
		G.bShotReady = true;
	}

	/**
	 * A shot of the game's window with its UI (bUI: run PIE in its own window, so the window is the game's), or of the
	 * scene alone; a name starting with _ is measured and not saved.
	 */
	static void RequestShot(const FString& Name, bool bUI = true)
	{
		if (!G.ShotHandle.IsValid())
		{
			G.ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
		}
		G.ShotName = Name;
		G.bShotReady = false;
		FScreenshotRequest::RequestScreenshot(FString(), bUI, false);
	}

	static void AddShot(TArray<FStep>& S, const FString& Name, float Settle = 0.3f, bool bUI = true)
	{
		S.Add(Do(Name + TEXT(": settle"), [Settle]() { return Settle; }));
		S.Add(Do(Name + TEXT(": shot"), [Name, bUI]() { RequestShot(Name, bUI); return 0.f; }));
		S.Add(WaitFor(Name + TEXT(": shot taken"), []() { return G.bShotReady; }, 5.f));
	}

	/** A point in desktop (Slate absolute) coordinates as a pixel of a shot taken with the UI (the game's window). */
	static FVector2D ToShotPixel(const FVector2D& Absolute)
	{
		const TSharedPtr<SWindow> Window = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
		return Window.IsValid() ? Absolute - FVector2D(Window->GetPositionInScreen()) : Absolute;
	}

	/** The contrast of the glyphs of the scene's line on a shot with the UI: their brightest 2% against the median of their block. */
	static int32 GlyphContrast(const FShot& Shot)
	{
		const URunTextSceneWidget* Scene = G.Scene.Get();
		const UWidget* Text = Scene && Scene->WidgetTree ? Scene->WidgetTree->FindWidget(TEXT("Text_Line")) : nullptr;
		if (!Text || Shot.Pixels.IsEmpty())
		{
			return -1;
		}
		const FGeometry Geo = Text->GetCachedGeometry();
		const FVector2D P0 = ToShotPixel(Geo.LocalToAbsolute(FVector2D::ZeroVector));
		const FVector2D P1 = ToShotPixel(Geo.LocalToAbsolute(Geo.GetLocalSize()));
		TArray<double> L;
		for (int32 Y = FMath::Max(0, FMath::FloorToInt(P0.Y)); Y < FMath::Min(Shot.Height, FMath::CeilToInt(P1.Y)); ++Y)
		{
			for (int32 X = FMath::Max(0, FMath::FloorToInt(P0.X)); X < FMath::Min(Shot.Width, FMath::CeilToInt(P1.X)); ++X)
			{
				L.Add(Luma(Shot.Pixels[Y * Shot.Width + X]));
			}
		}
		Note(FString::Printf(TEXT("the line's block on the shot: (%.0f, %.0f) to (%.0f, %.0f), %d pixels"), P0.X, P0.Y, P1.X, P1.Y, L.Num()));
		if (L.Num() < 100)
		{
			return -1;
		}
		L.Sort();
		double Core = 0.0;
		const int32 Top = FMath::Max(1, L.Num() / 50);
		for (int32 I = L.Num() - Top; I < L.Num(); ++I)
		{
			Core += L[I];
		}
		return FMath::RoundToInt32(Core / Top - L[L.Num() / 2]);
	}


	static double MeanLuma(const FShot& Shot)
	{
		double Sum = 0.0;
		for (const FColor& C : Shot.Pixels)
		{
			Sum += Luma(C);
		}
		return Shot.Pixels.Num() ? Sum / Shot.Pixels.Num() : -1.0;
	}

	// --- fonts ---

	static bool IsStyleFont(const FSlateFontInfo& Font)
	{
		const UObject* Want = StdUIStyle::GetFontObject();
		return Want && Font.FontObject.Get() == Want;
	}

	static void AuditTree(const FString& Screen, const UUserWidget* Widget, int32 Depth, int32& Texts, int32& Others)
	{
		if (!Widget || !Widget->WidgetTree || Depth > 6)
		{
			return;
		}
		Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
		{
			if (const UTextBlock* Text = Cast<UTextBlock>(Child))
			{
				const FSlateFontInfo Font = Text->GetFont();
				const bool bOk = IsStyleFont(Font);
				++Texts;
				Others += bOk ? 0 : 1;
				UE_LOG(LogTheLastShift, Display, TEXT("StyleCheck   FONT %s: %s.%s \"%s\": %s%s"), *Screen, *Widget->GetClass()->GetName(), *Text->GetName(),
					*Text->GetText().ToString().Left(32).ReplaceCharWithEscapedChar(), *StdUIStyle::DescribeFont(Font), bOk ? TEXT("") : TEXT("  <- NOT THE STYLE'S FONT"));
			}
			else if (const UUserWidget* Nested = Cast<UUserWidget>(Child))
			{
				AuditTree(Screen, Nested, Depth + 1, Texts, Others);
			}
		});
	}

	static void AuditFonts(const FString& Screen, const UUserWidget* Widget, int32 MinTexts = 1)
	{
		int32 Texts = 0, Others = 0;
		AuditTree(Screen, Widget, 0, Texts, Others);
		G.FontTexts += Texts;
		G.FontOthers += Others;
		Check(Widget && Texts >= MinTexts && Others == 0, FString::Printf(TEXT("fonts of %s (%s): %d texts, %d in another font than the style's %s"),
			*Screen, Widget ? *Widget->GetClass()->GetName() : TEXT("missing"), Texts, Others, *GetNameSafe(StdUIStyle::GetFontObject())));
	}

	/** Every visible STextBlock of the game viewport's Slate tree (the covered screen's label is one). */
	static void CollectSlateTexts(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<STextBlock>>& Out, int32 Depth)
	{
		if (Depth > 64)
		{
			return;
		}
		if (Widget->GetType() == FName(TEXT("STextBlock")))
		{
			Out.Add(StaticCastSharedRef<STextBlock>(Widget));
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 I = 0; Children && I < Children->Num(); ++I)
		{
			CollectSlateTexts(Children->GetChildAt(I), Out, Depth + 1);
		}
	}

	// --- the face of a screen on a shot ---

	/** Mean level of the pixels inside the inner 80% of a plate's face, as the camera sees it now. */
	static bool MeasureFace(const UStaticMeshComponent* Plate, double& OutMean, int32& OutCount)
	{
		APlayerController* C = PC();
		if (!Plate || !C || G.Shot.Pixels.IsEmpty())
		{
			return false;
		}
		const FTransform T = Plate->GetComponentTransform();
		TArray<FVector2D> Q;
		for (const FVector2D& Corner : { FVector2D(-40, -40), FVector2D(40, -40), FVector2D(40, 40), FVector2D(-40, 40) })
		{
			FVector2D P;
			if (!C->ProjectWorldLocationToScreen(T.TransformPosition(FVector(Corner.X, Corner.Y, 0.0)), P, false))
			{
				return false;
			}
			Q.Add(P);
		}
		FVector2D ViewportSize(G.Shot.Width, G.Shot.Height);
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(ViewportSize);
		}
		const double SX = G.Shot.Width / FMath::Max(ViewportSize.X, 1.0), SY = G.Shot.Height / FMath::Max(ViewportSize.Y, 1.0);
		for (FVector2D& P : Q)
		{
			P.X *= SX;
			P.Y *= SY;
		}
		const auto Inside = [&Q](double X, double Y)
		{
			int32 Sign = 0;
			for (int32 I = 0; I < 4; ++I)
			{
				const FVector2D A = Q[I], B = Q[(I + 1) % 4];
				const double Cross = (B.X - A.X) * (Y - A.Y) - (B.Y - A.Y) * (X - A.X);
				const int32 S = Cross > 0 ? 1 : (Cross < 0 ? -1 : 0);
				if (S != 0)
				{
					if (Sign != 0 && S != Sign)
					{
						return false;
					}
					Sign = S;
				}
			}
			return true;
		};
		double Sum = 0.0;
		OutCount = 0;
		const int32 X0 = FMath::Clamp(FMath::FloorToInt(FMath::Min(FMath::Min(Q[0].X, Q[1].X), FMath::Min(Q[2].X, Q[3].X))), 0, G.Shot.Width - 1);
		const int32 X1 = FMath::Clamp(FMath::CeilToInt(FMath::Max(FMath::Max(Q[0].X, Q[1].X), FMath::Max(Q[2].X, Q[3].X))), 0, G.Shot.Width - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(FMath::Min(FMath::Min(Q[0].Y, Q[1].Y), FMath::Min(Q[2].Y, Q[3].Y))), 0, G.Shot.Height - 1);
		const int32 Y1 = FMath::Clamp(FMath::CeilToInt(FMath::Max(FMath::Max(Q[0].Y, Q[1].Y), FMath::Max(Q[2].Y, Q[3].Y))), 0, G.Shot.Height - 1);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				if (Inside(X + 0.5, Y + 0.5))
				{
					Sum += Level(G.Shot.Pixels[Y * G.Shot.Width + X]);
					++OutCount;
				}
			}
		}
		OutMean = OutCount ? Sum / OutCount : -1.0;
		return OutCount > 50;
	}

	static void Flashlight(bool bOn)
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

	/** Stands the pawn on the floor Distance in front of a plate's face, at eye height of its centre, looking at it. */
	static void StandBefore(const UStaticMeshComponent* Plate, double Distance)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		UWorld* World = W();
		if (!Plate || !P || !C || !World)
		{
			return;
		}
		const FVector Centre = Plate->GetComponentLocation();
		FVector Normal = Plate->GetUpVector();
		FVector Flat(Normal.X, Normal.Y, 0.0);
		if (!Flat.Normalize())
		{
			Flat = FVector::XAxisVector;
		}
		const FVector At = Centre + Flat * Distance;
		const ACharacter* Character = Cast<ACharacter>(P);
		const double Half = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		double Z = At.Z;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("TlsStyleCheck"), false, P);
		if (World->LineTraceSingleByChannel(Hit, FVector(At.X, At.Y, At.Z + 100.0), FVector(At.X, At.Y, At.Z - 1000.0), ECC_Visibility, Params))
		{
			Z = Hit.ImpactPoint.Z + Half + 2.0;
		}
		P->SetActorLocation(FVector(At.X, At.Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
		FVector Eye;
		FRotator Ignored;
		P->GetActorEyesViewPoint(Eye, Ignored);
		C->SetControlRotation((Centre - Eye).Rotation());
	}

	static void StandAt(const FVector& At, const FVector& LookAt)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		if (!P || !C)
		{
			return;
		}
		P->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		FVector Eye;
		FRotator Ignored;
		P->GetActorEyesViewPoint(Eye, Ignored);
		C->SetControlRotation((LookAt - Eye).Rotation());
	}

	/** Stands the pawn on the floor at (X, Y) (a trace down from above its head), looking at LookAt: the same pose every time. */
	static void StandOnFloor(double X, double Y, const FVector& LookAt)
	{
		APawn* P = Pawn();
		UWorld* World = W();
		if (!P || !World)
		{
			return;
		}
		const ACharacter* Character = Cast<ACharacter>(P);
		const double Half = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		double Z = P->GetActorLocation().Z;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("TlsStyleCheck"), false, P);
		const double From = LookAt.Z + 100.0;
		if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, From), FVector(X, Y, From - 2000.0), ECC_Visibility, Params))
		{
			Z = Hit.ImpactPoint.Z + Half + 2.0;
		}
		StandAt(FVector(X, Y, Z), LookAt);
		P->GetMovementComponent() ? P->GetMovementComponent()->StopMovementImmediately() : void();
	}

	static void AddFaceCheck(TArray<FStep>& S, const FString& Name, TFunction<UStaticMeshComponent*()> PlateOf, bool bFlashlight)
	{
		S.Add(Do(Name + TEXT(": stand 2 m before the screen"), [PlateOf, bFlashlight]()
		{
			StandBefore(PlateOf(), 200.0);
			Flashlight(bFlashlight);
			return 0.6f;
		}));
		AddShot(S, Name, 0.2f, false);
		S.Add(Do(Name + TEXT(": measure"), [Name, PlateOf, bFlashlight]()
		{
			const UStaticMeshComponent* Plate = PlateOf();
			double Mean = -1.0;
			int32 Count = 0;
			const bool bOk = MeasureFace(Plate, Mean, Count);
			Check(Plate && Plate->IsVisible() && bOk && Mean <= 20.0, FString::Printf(TEXT("%s: the screen's face from 2 m%s, mean level %.1f over %d pixels (at most 20); plate %s, shown %d, material %s"),
				*Name, bFlashlight ? TEXT(" with the flashlight") : TEXT(" in the room's light (no flashlight in this map)"), Mean, Count, *GetNameSafe(Plate), Plate && Plate->IsVisible() ? 1 : 0,
				Plate ? *GetNameSafe(Plate->GetMaterial(0)) : TEXT("-")));
			return 0.f;
		}));
	}

	// --- the run ---

	static void Finish()
	{
		if (G.Sampler.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
			G.Sampler.Reset();
		}
		if (G.ShotHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(G.ShotHandle);
			G.ShotHandle.Reset();
		}
		if (G.bFixture)
		{
			NarrativeFixture::Restore(W());
			G.bFixture = false;
		}
		UE_LOG(LogTheLastShift, Display, TEXT("StyleCheck summary (%s, %s): %d checks passed, %d failed; %d texts listed, %d in another font."), *G.Map,
			G.Failed ? TEXT("FAIL") : TEXT("PASS"), G.Passed, G.Failed, G.FontTexts, G.FontOthers);
		if (G.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.StyleCheck"));
		}
	}

	static bool Tick(float)
	{
		if (FPlatformTime::Seconds() < G.WaitUntil)
		{
			return true;
		}
		if (!G.Steps.IsValidIndex(G.Next))
		{
			Finish();
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

	// =================================================================================================================
	// MainMenu
	// =================================================================================================================

	static UStdScreenWidget* Top() { return TimeStateDebug::TopScreen(W()); }

	static bool ClickNamed(UUserWidget* Widget, FName Name)
	{
		UButton* Button = Widget && Widget->WidgetTree ? Cast<UButton>(Widget->WidgetTree->FindWidget(Name)) : nullptr;
		if (!Button)
		{
			return false;
		}
		Button->OnClicked.Broadcast();
		return true;
	}

	/** Lines of Text wrapped at Width in Font, and its widest line. */
	static int32 Lines(const FString& Text, const FSlateFontInfo& Font, float Width, float* OutWidest = nullptr)
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FString Laid = StdTextScene::BreakLines(Text, Font, Width, Measure);
		TArray<FString> Out;
		Laid.ParseIntoArray(Out, TEXT("\n"), false);
		if (OutWidest)
		{
			*OutWidest = 0.f;
			for (const FString& L : Out)
			{
				*OutWidest = FMath::Max(*OutWidest, static_cast<float>(Measure->Measure(L, Font).X));
			}
		}
		return FMath::Max(1, Out.Num());
	}

	static float LineHeight(const FSlateFontInfo& Font)
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return static_cast<float>(Measure->GetMaxCharacterHeight(Font));
	}

	/** The longest row's text of a narrative table (its Text column), and its name. */
	static FString LongestRow(const UDataTable* Table, FName& OutRow, const TFunction<bool(FName)>& Filter = nullptr)
	{
		FString Best;
		if (!Table || !Table->GetRowStruct())
		{
			return Best;
		}
		const FTextProperty* TextProp = FindFProperty<FTextProperty>(Table->GetRowStruct(), TEXT("Text"));
		if (!TextProp)
		{
			return Best;
		}
		for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
		{
			if (Filter && !Filter(Row.Key))
			{
				continue;
			}
			const FString Text = TextProp->GetPropertyValue_InContainer(Row.Value).ToString();
			if (Text.Len() > Best.Len())
			{
				Best = Text;
				OutRow = Row.Key;
			}
		}
		return Best;
	}

	static void FitCheck(const FString& Box, const UDataTable* Table, int32 Size, float Width, float HoldHeight, const TFunction<bool(FName)>& Filter = nullptr)
	{
		FName Row;
		const FString Text = LongestRow(Table, Row, Filter);
		const FSlateFontInfo Old = FCoreStyle::GetDefaultFontStyle("Regular", Size);
		const FSlateFontInfo New = StdUIStyle::MakeFont(Size);
		float OldWide = 0.f, NewWide = 0.f;
		const int32 OldLines = Lines(Text, Old, Width, &OldWide);
		const int32 NewLines = Lines(Text, New, Width, &NewWide);
		const int32 Holds = FMath::FloorToInt(HoldHeight / FMath::Max(LineHeight(New), 1.f));
		Check(!Text.IsEmpty() && NewLines <= Holds && NewWide <= Width + 0.5f,
			FString::Printf(TEXT("fit, %s: longest row %s of %s (%d characters): %d lines in the old font (widest %.0f), %d in the style's (widest %.0f), the box %.0f wide holds %d lines"),
				*Box, *Row.ToString(), *GetNameSafe(Table), Text.Len(), OldLines, OldWide, NewLines, NewWide, Width, Holds));
	}

	static void BuildMainMenu(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("MainMenu: the main menu"), []() { return Cast<URunMainMenuWidget>(Top()) != nullptr; }, 20.f));
		S.Add(Do(TEXT("style on, font"), []()
		{
			const UObject* Font = StdUIStyle::GetFontObject();
			Check(StdUIStyle::IsEnabled() && Font && Font->GetPathName() == TEXT("/Engine/EngineFonts/DroidSansMono.DroidSansMono"),
				FString::Printf(TEXT("StdCore's style is on and its font is the engine's monospace font: %s"), Font ? *Font->GetPathName() : TEXT("none")));
			const FLinearColor Accent = StdUIStyle::GetAccentColor();
			const FLinearColor Tint = UTheLastShiftSettings::Get()->GetSurfaceVariant(true).Tint;
			Check(Accent.Equals(Tint, 1.e-4f), FString::Printf(TEXT("the accent is home's tint (SurfaceLook.Home.Tint %s): %s"), *Tint.ToString(), *Accent.ToString()));
			return 0.f;
		}));
		S.Add(Do(TEXT("MainMenu: fonts"), []()
		{
			UStdScreenWidget* Menu = Top();
			if (Menu)
			{
				Menu->FocusDefaultWidget();
			}
			AuditFonts(TEXT("main menu"), Menu, 4);
			return 0.f;
		}));
		AddShot(S, TEXT("MainMenu"), 0.5f);

		S.Add(Do(TEXT("settings: open"), []() { Check(ClickNamed(Top(), TEXT("Button_Settings")), TEXT("the main menu's Settings opens the settings screen")); return 0.5f; }));
		S.Add(Do(TEXT("settings: fonts"), []() { AuditFonts(TEXT("settings"), Top(), 4); return 0.f; }));
		AddShot(S, TEXT("Settings"));
		S.Add(Do(TEXT("rebinding: Controls tab"), []() { Check(ClickNamed(Top(), TEXT("Button_TabControls")), TEXT("the settings' Controls tab opens the rebinding rows")); return 0.6f; }));
		S.Add(Do(TEXT("rebinding: fonts"), []() { AuditFonts(TEXT("rebinding"), Top(), 8); return 0.f; }));
		AddShot(S, TEXT("Rebinding"));
		S.Add(Do(TEXT("settings: close"), []() { if (UStdScreenWidget* T = Top()) { T->CloseScreen(); } return 0.4f; }));

		S.Add(Do(TEXT("credits"), []()
		{
			ClickNamed(Top(), TEXT("Button_Credits"));
			return 0.4f;
		}));
		S.Add(Do(TEXT("credits: fonts, close"), []()
		{
			UStdScreenWidget* T = Top();
			AuditFonts(TEXT("credits"), T, 1);
			if (T && !Cast<URunMainMenuWidget>(T))
			{
				T->CloseScreen();
			}
			return 0.4f;
		}));
		S.Add(Do(TEXT("select act / loss screen: fonts"), []()
		{
			UStdScreenWidget* Menu = Top();
			if (URunActListWidget* List = Menu ? Cast<URunActListWidget>(Menu->OpenChildScreen(URunActListWidget::StaticClass())) : nullptr)
			{
				List->Configure(ERunActListMode::SelectAct);
				AuditFonts(TEXT("select act"), List, 1);
				List->Configure(ERunActListMode::RunLost);
				AuditFonts(TEXT("loss screen"), List, 1);
				List->CloseScreen();
			}
			else
			{
				Check(false, TEXT("the act list opens over the main menu"));
			}
			return 0.4f;
		}));

		// --- a text scene: a line on white, then the cut to a line on black with the static ---
		S.Add(Do(TEXT("scene: open"), []()
		{
			URunTextSceneWidget* Scene = CreateWidget<URunTextSceneWidget>(PC(), URunTextSceneWidget::StaticClass());
			G.Scene = Scene;
			TArray<FRunSceneLine> Lines;
			FRunSceneLine White;
			White.RowName = TEXT("StyleCheck_1");
			White.Voice = ESceneVoice::Paradise;
			White.Text = TEXT("[PLACEHOLDER] A fixture line of the style check, typed on the paradise's white background.");
			Lines.Add(White);
			FRunSceneLine Black;
			Black.RowName = TEXT("StyleCheck_2");
			Black.Voice = ESceneVoice::HomeAI;
			Black.Text = TEXT("[PLACEHOLDER] A fixture line of the style check, typed on black after the cut, over the static.");
			Lines.Add(Black);
			FRunSceneLine After;
			After.RowName = TEXT("StyleCheck_3");
			After.Voice = ESceneVoice::Engineer;
			After.Text = TEXT("[PLACEHOLDER] A third fixture line.");
			Lines.Add(After);
			if (Scene)
			{
				Scene->ShowAsRoot(60);
				Scene->Play(ERunSceneKind::EndingText, TEXT("StyleCheck"), Lines);
				Scene->Confirm();
			}
			Check(Scene && Scene->GetCurrentIndex() == 0 && !Scene->IsEffectShown(), TEXT("scene: the first line (on white) shows no static"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("scene: fonts on white"), []() { AuditFonts(TEXT("text scene"), G.Scene.Get(), 1); return 0.f; }));
		AddShot(S, TEXT("SceneWhite"), 0.1f);
		S.Add(Do(TEXT("scene: the cut"), []()
		{
			URunTextSceneWidget* Scene = G.Scene.Get();
			if (Scene)
			{
				Scene->Confirm();
				Scene->Confirm();
			}
			G.StaticStartReal = FPlatformTime::Seconds();
			G.StaticMeans.Reset();
			const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
			Check(Scene && Scene->GetCurrentIndex() == 1 && Scene->IsEffectShown() && FMath::IsNearlyEqual(Scene->GetEffectOpacity(), 1.f),
				FString::Printf(TEXT("scene: the cut to black (the interruption's line) shows the static at full strength (opacity %.2f); StaticSeconds %.2f, StaticFadeSeconds %.2f"),
					Scene ? Scene->GetEffectOpacity() : -1.f, Settings->GetStaticSeconds(), Settings->GetStaticFadeSeconds()));
			return 0.f;
		}));
		AddShot(S, TEXT("StaticCut"), 0.05f);
		for (int32 I = 0; I < 6; ++I)
		{
			S.Add(Do(FString::Printf(TEXT("static: frame %d"), I), [I]() { RequestShot(FString::Printf(TEXT("_static%d"), I)); return 0.f; }));
			S.Add(WaitFor(FString::Printf(TEXT("static: frame %d taken"), I), []() { return G.bShotReady; }, 5.f, []()
			{
				G.StaticMeans.Add(MeanLuma(G.Shot));
				return 0.f;
			}));
		}
		S.Add(Do(TEXT("static: steady, readable"), []()
		{
			double MaxStep = 0.0;
			for (int32 I = 1; I < G.StaticMeans.Num(); ++I)
			{
				MaxStep = FMath::Max(MaxStep, FMath::Abs(G.StaticMeans[I] - G.StaticMeans[I - 1]));
			}
			FString Means;
			for (const double M : G.StaticMeans)
			{
				Means += FString::Printf(TEXT(" %.2f"), M);
			}
			const URunTextSceneWidget* Scene = G.Scene.Get();
			Check(G.StaticMeans.Num() == 6 && MaxStep <= 5.0 && Scene && Scene->GetEffectOpacity() >= 1.f,
				FString::Printf(TEXT("static: the screen's mean level over %d frames of the static:%s; largest change from a frame to the next %.2f (at most 5); still at full strength"), G.StaticMeans.Num(), *Means, MaxStep));
			if (const FShot* Cut = G.Shots.Find(TEXT("StaticCut")))
			{
				G.ContrastStatic = GlyphContrast(*Cut);
			}
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("static: half-way through the fade"), []()
		{
			const URunTextSceneWidget* Scene = G.Scene.Get();
			const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
			return Scene && Scene->GetEffectElapsed() >= Settings->GetStaticSeconds() + 0.5f * Settings->GetStaticFadeSeconds();
		}, 10.f, []()
		{
			const URunTextSceneWidget* Scene = G.Scene.Get();
			const double Held = FPlatformTime::Seconds() - G.StaticStartReal;
			Check(Scene && Scene->GetEffectOpacity() > 0.2f && Scene->GetEffectOpacity() < 0.8f,
				FString::Printf(TEXT("static: half-way through its fade at %.2f s of the effect (%.2f s of real time): opacity %.2f"), Scene ? Scene->GetEffectElapsed() : -1.f, Held, Scene ? Scene->GetEffectOpacity() : -1.f));
			RequestShot(TEXT("StaticHalf"));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("static: half-way shot"), []() { return G.bShotReady; }, 5.f));
		S.Add(WaitFor(TEXT("static: gone"), []() { const URunTextSceneWidget* Scene = G.Scene.Get(); return Scene && !Scene->IsEffectShown(); }, 10.f, []()
		{
			const URunTextSceneWidget* Scene = G.Scene.Get();
			const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
			const float Total = Settings->GetStaticSeconds() + Settings->GetStaticFadeSeconds();
			Check(Scene && FMath::Abs(Scene->GetEffectElapsed() - Total) < 0.15f && Scene->GetCurrentIndex() == 1,
				FString::Printf(TEXT("static: gone after %.2f s of the effect (StaticSeconds + StaticFadeSeconds = %.2f), the line still on screen"), Scene ? Scene->GetEffectElapsed() : -1.f, Total));
			return 0.2f;
		}));
		AddShot(S, TEXT("SceneBlack"), 0.1f);
		S.Add(Do(TEXT("static: the glyphs' contrast"), []()
		{
			if (const FShot* Plain = G.Shots.Find(TEXT("SceneBlack")))
			{
				G.ContrastPlain = GlyphContrast(*Plain);
			}
			Check(G.ContrastStatic > 20 && G.ContrastPlain > 20 && FMath::Abs(G.ContrastStatic - G.ContrastPlain) <= 10,
				FString::Printf(TEXT("static: the glyphs against the patch: contrast %d levels over the static, %d on the plain background (within 10)"), G.ContrastStatic, G.ContrastPlain));
			return 0.f;
		}));
		S.Add(Do(TEXT("scene: close"), []() { if (URunTextSceneWidget* Scene = G.Scene.Get()) { Scene->Skip(); } return 0.4f; }));

		// --- both values from the settings ---
		S.Add(Do(TEXT("static: other values for the session"), []()
		{
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			G.SavedStatic = Settings->StaticSeconds;
			G.SavedFade = Settings->StaticFadeSeconds;
			Settings->StaticSeconds = 0.8f;
			Settings->StaticFadeSeconds = 0.3f;
			URunTextSceneWidget* Scene = CreateWidget<URunTextSceneWidget>(PC(), URunTextSceneWidget::StaticClass());
			G.Scene = Scene;
			TArray<FRunSceneLine> Lines;
			FRunSceneLine White;
			White.Voice = ESceneVoice::EngineerParadise;
			White.Text = TEXT("[PLACEHOLDER] White.");
			Lines.Add(White);
			FRunSceneLine Black;
			Black.Voice = ESceneVoice::Engineer;
			Black.Text = TEXT("[PLACEHOLDER] Black.");
			Lines.Add(Black);
			if (Scene)
			{
				Scene->ShowAsRoot(60);
				Scene->Play(ERunSceneKind::EndingText, TEXT("StyleCheckValues"), Lines);
				Scene->Confirm();
				Scene->Confirm();
			}
			G.StaticStartReal = FPlatformTime::Seconds();
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("static: gone with the session's values"), []() { const URunTextSceneWidget* Scene = G.Scene.Get(); return Scene && Scene->GetCurrentIndex() == 1 && !Scene->IsEffectShown(); }, 6.f, []()
		{
			const URunTextSceneWidget* Scene = G.Scene.Get();
			const double Real = FPlatformTime::Seconds() - G.StaticStartReal;
			Check(Scene && FMath::Abs(Scene->GetEffectElapsed() - 1.1f) < 0.15f,
				FString::Printf(TEXT("static: StaticSeconds 0.8 and StaticFadeSeconds 0.3 set for the session: the static ends after %.2f s of the effect (%.2f s real), at 1.1"),
					Scene ? Scene->GetEffectElapsed() : -1.f, Real));
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			Settings->StaticSeconds = G.SavedStatic;
			Settings->StaticFadeSeconds = G.SavedFade;
			if (URunTextSceneWidget* Sc = G.Scene.Get())
			{
				Sc->Skip();
			}
			return 0.4f;
		}));

		// --- the ending's choice ---
		S.Add(Do(TEXT("ending: open"), []()
		{
			URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(W());
			UStdScreenWidget* Menu = Top();
			Check(Scenes && Menu && Scenes->OpenEnding(Menu) != nullptr, TEXT("the ending's choice opens over the main menu"));
			return 0.8f;
		}));
		S.Add(Do(TEXT("ending: fonts"), []()
		{
			URunEndingWidget* Ending = FirstWidget<URunEndingWidget>();
			AuditFonts(TEXT("ending's choice"), Ending, 2);
			if (Ending)
			{
				Ending->FocusDefaultWidget();
			}
			return 0.f;
		}));
		AddShot(S, TEXT("EndingChoice"), 0.4f);
		S.Add(Do(TEXT("ending: close"), []() { if (URunEndingWidget* Ending = FirstWidget<URunEndingWidget>()) { Ending->CloseScreen(); } return 0.6f; }));

		// --- fit ---
		S.Add(Do(TEXT("fit"), []()
		{
			const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
			const UTextChannelWidget* Defaults = GetDefault<UTextChannelWidget>();
			const float ChannelWidth = FMath::Clamp(Defaults->MaxWidth, 100.f, 1200.f);
			// The channel's box grows up from the bottom left corner: it holds what fits in the lower half of a 1080 screen.
			const float ChannelHold = 540.f - FMath::Clamp(Defaults->BottomMargin, 0.f, 600.f) - 16.f;
			const auto NotEntry = [](FName Row) { return !Row.ToString().Contains(TEXT("AreaEntry")); };
			FitCheck(TEXT("text channel, home AI"), Settings->LoadAIMessagesTable(), Defaults->FontSize, ChannelWidth, ChannelHold, NotEntry);
			FitCheck(TEXT("text channel, prologue"), Settings->LoadPrologueTable(), Defaults->FontSize, ChannelWidth, ChannelHold);
			FitCheck(TEXT("text channel, engineer"), Settings->LoadEngineerLinesTable(), Defaults->FontSize, ChannelWidth, ChannelHold);
			// The scenes: centred in a 960 column at 26, between the top and the controls at the bottom.
			const UStdTextSceneWidget* Scene = GetDefault<UStdTextSceneWidget>();
			const float SceneHold = 1080.f - 2.f * 120.f;
			FitCheck(TEXT("text scene"), Settings->LoadScenesTable(), Scene->FontSize, Scene->ColumnWidth, SceneHold);
			FitCheck(TEXT("text scene, endings"), Settings->LoadEndingsTable(), Scene->FontSize, Scene->ColumnWidth, SceneHold, [](FName Row) { return Row != TEXT("Loss"); });
			// The loss text: the loss screen's body, the column of a choice list at the body size.
			const UStdUITheme* Theme = StdUITheme::Get();
			const int32 Body = Theme ? Theme->BodyFont.Size : 18;
			FitCheck(TEXT("loss screen"), Settings->LoadEndingsTable(), Body, 420.f, 540.f, [](FName Row) { return Row == TEXT("Loss"); });
			// The travel screen's label: the AreaEntry rows, wrapped at TransitionLabelMaxWidth: every line within it.
			const UStdProjectSettings* Std = UStdProjectSettings::Get();
			FitCheck(TEXT("travel screen's label"), Settings->LoadAIMessagesTable(), Std->TransitionLabelFontSize, Std->TransitionLabelMaxWidth, 1080.f * 0.5f,
				[](FName Row) { return Row.ToString().Contains(TEXT("AreaEntry")); });
			// Barcode text: one cell per character; a character of the style's font must fit its cell.
			const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const int32 ChannelSize = FMath::Clamp(Defaults->FontSize, 8, 48);
			const float Cell = FMath::RoundToFloat(ChannelSize * 0.72f);
			// Every character is drawn centred in its cell: the style's widest character against the old font's widest.
			const FString Sample = TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
			float NewWide = 0.f, OldWide = 0.f;
			for (const TCHAR Ch : Sample)
			{
				const FString One(1, &Ch);
				NewWide = FMath::Max(NewWide, static_cast<float>(Measure->Measure(One, StdUIStyle::MakeFont(ChannelSize)).X));
				OldWide = FMath::Max(OldWide, static_cast<float>(Measure->Measure(One, FCoreStyle::GetDefaultFontStyle("Regular", ChannelSize)).X));
			}
			Check(NewWide <= FMath::Max(Cell, OldWide), FString::Printf(TEXT("fit, the twin AI's barcode text on the channel: the style's widest character is %.1f wide, the old font's %.1f, in cells of %.0f (the rows' lines are the cells', the same as before)"), NewWide, OldWide, Cell));
			FName LogRow, TwinRow;
			const FString Log = LongestRow(Settings->LoadLogsTable(), LogRow);
			const FString Twin = LongestRow(Settings->LoadTwinAITable(), TwinRow);
			Note(FString::Printf(TEXT("fit, logs and the twin AI: longest rows %s (%d characters) and %s (%d characters), laid out in barcode cells whose number and size the font does not change"),
				*LogRow.ToString(), Log.Len(), *TwinRow.ToString(), Twin.Len()));
			return 0.f;
		}));

		// --- the travel screen, last: a travel back to the main menu with the longest entry line as its label ---
		S.Add(Do(TEXT("travel: start"), []()
		{
			const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
			FName Row;
			const FString Label = LongestRow(Settings->LoadAIMessagesTable(), Row, [](FName R) { return R.ToString().Contains(TEXT("AreaEntry")); });
			UStdFlowSubsystem* Flow = W() && W()->GetGameInstance() ? W()->GetGameInstance()->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			FStdTravelOptions Options;
			Options.Label = FText::FromString(Label);
			Options.MinCoveredSeconds = 4.f;
			const bool bOk = Flow && Flow->TravelToLevelWithOptions(UStdProjectSettings::Get()->MainMenuMap, Options);
			Check(bOk, FString::Printf(TEXT("travel: a travel to the main menu with the label of %s"), *Row.ToString()));
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("travel: covered"), []()
		{
			UWorld* World = W();
			const UStdFlowSubsystem* Flow = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UStdFlowSubsystem>() : nullptr;
			return Flow && Flow->IsTransitioning();
		}, 15.f, []() { return 1.5f; }));
		S.Add(Do(TEXT("travel: fonts"), []()
		{
			TArray<TSharedRef<STextBlock>> Texts;
			if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid())
			{
				// The window's content only: its title bar belongs to the window (the editor's in PIE), not to the game.
				CollectSlateTexts(GEngine->GameViewport->GetWindow()->GetContent(), Texts, 0);
			}
			int32 Label = 0, Others = 0;
			for (const TSharedRef<STextBlock>& Text : Texts)
			{
				if (Text->GetText().IsEmpty() || Text->GetVisibility() == EVisibility::Collapsed)
				{
					continue;
				}
				const FSlateFontInfo Font = Text->GetFont();
				const bool bOk = IsStyleFont(Font);
				++Label;
				Others += bOk ? 0 : 1;
				UE_LOG(LogTheLastShift, Display, TEXT("StyleCheck   FONT travel screen: STextBlock \"%s\": %s%s"), *Text->GetText().ToString().Left(32), *StdUIStyle::DescribeFont(Font), bOk ? TEXT("") : TEXT("  <- NOT THE STYLE'S FONT"));
			}
			G.FontTexts += Label;
			G.FontOthers += Others;
			Check(Label >= 1 && Others == 0, FString::Printf(TEXT("fonts of the travel screen: %d texts, %d in another font"), Label, Others));
			return 0.f;
		}));
		AddShot(S, TEXT("TravelScreen"), 0.1f);
	}

	// =================================================================================================================
	// Prologue_Home
	// =================================================================================================================

	static APrologueMonitorRoom* Room() { return First<APrologueMonitorRoom>(); }

	static void SampleLights()
	{
		const APrologueMonitorRoom* R = Room();
		if (!R)
		{
			return;
		}
		const int32 N = R->GetLightCount();
		if (G.Toggles.Num() != N)
		{
			G.Toggles.Init(0, N);
			G.LastOn.Init(2, N);
		}
		++G.BlinkFrames;
		for (int32 I = 0; I < N; ++I)
		{
			FVector L;
			int32 C;
			float P, Ph;
			bool bOn;
			R->GetLight(I, L, C, P, Ph, bOn);
			if (G.LastOn[I] != 2 && G.LastOn[I] != (bOn ? 1 : 0))
			{
				++G.Toggles[I];
			}
			G.LastOn[I] = bOn ? 1 : 0;
		}
		for (int32 I = 1; I < N; ++I)
		{
			++G.NeighbourPairs;
			G.NeighbourDiffer += G.LastOn[I] != G.LastOn[I - 1] ? 1 : 0;
		}
	}

	static TArray<uint8> LightStates()
	{
		TArray<uint8> Out;
		if (const APrologueMonitorRoom* R = Room())
		{
			for (int32 I = 0; I < R->GetLightCount(); ++I)
			{
				FVector L;
				int32 C;
				float P, Ph;
				bool bOn;
				R->GetLight(I, L, C, P, Ph, bOn);
				Out.Add(bOn ? 1 : 0);
			}
		}
		return Out;
	}

	static void AddRackCapture(TArray<FStep>& S, const FString& Key, bool bLights)
	{
		S.Add(Do(Key + TEXT(": pose"), [bLights]()
		{
			if (UTimeStateSubsystem* T = TS(); T && T->IsStopped()) { T->Resume(); }
			if (APrologueMonitorRoom* R = Room())
			{
				R->SetShownWhileStoppedForTest(bLights);
				R->SetHiddenForTest(!bLights);
				FVector L;
				int32 C;
				float P, Ph;
				bool bOn;
				if (R->GetLight(R->GetLightCount() / 2, L, C, P, Ph, bOn))
				{
					// 3 m in front of a rack, looking at its lights.
					// Into the room: the racks face its middle (the monitor room actor stands there).
					const FVector Ahead(FMath::Sign(R->GetActorLocation().X - L.X), 0.0, 0.0);
					StandOnFloor(L.X + Ahead.X * 300.0, L.Y + Ahead.Y * 300.0, L);
				}
			}
			return 0.8f;
		}));
		S.Add(Do(Key + TEXT(": Stop"), []()
		{
			// The same pose for both captures: no look or move input reaches the pawn until the scan is read.
			if (APlayerController* C = PC())
			{
				C->SetIgnoreLookInput(true);
				C->SetIgnoreMoveInput(true);
			}
			const ULidarScanComponent* L = Lidar();
			G.BlinkFrames = L ? L->GetScanCount() : 0;
			if (UTimeStateSubsystem* T = TS()) { T->Stop(); }
			return 0.f;
		}));
		S.Add(WaitFor(Key + TEXT(": the first scan"), []() { const ULidarScanComponent* L = Lidar(); return L && L->GetScanCount() > G.BlinkFrames; }, 6.f, [Key, bLights]()
		{
			Check(Room() && Room()->AreLightsShown() == bLights, FString::Printf(TEXT("%s: the lights %s while Stopped for the test"), *Key, bLights ? TEXT("shown") : TEXT("hidden")));
			return 0.3f;
		}));
		S.Add(Do(Key + TEXT(": depth"), [Key]()
		{
			TArray<float> Depth;
			int32 DW = 0, DH = 0;
			FLidarScanContext Context;
			const ULidarScanComponent* L = Lidar();
			const bool bOk = L && L->ReadNewestScanDepth(Depth, DW, DH, Context);
			Note(FString::Printf(TEXT("%s: scan %d depth %d x %d read %d, from %s looking %s"), *Key, Context.ScanIndex, DW, DH, bOk ? 1 : 0,
				*Context.CaptureOrigin.ToCompactString(), *Context.CaptureForward.ToCompactString()));
			G.Depths.Add(Key, Depth);
			if (APlayerController* C = PC())
			{
				C->ResetIgnoreLookInput();
				C->ResetIgnoreMoveInput();
			}
			if (UTimeStateSubsystem* T = TS()) { T->Resume(); }
			if (APrologueMonitorRoom* R = Room())
			{
				R->SetShownWhileStoppedForTest(false);
				R->SetHiddenForTest(false);
			}
			return 1.f;
		}));
	}

	static void BuildHome(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("Prologue_Home: the pawn, the channel and the monitor room"), []() { return Pawn() && Channel() && TS() && Room() && Room()->HasActorBegunPlay(); }, 20.f));

		// --- the text channel: a home AI row, an engineer's row ---
		S.Add(Do(TEXT("channel: fixture rows"), []()
		{
			NarrativeFixture::Install(W());
			G.bFixture = true;
			if (UTextChannelComponent* C = Channel())
			{
				C->ClearAll();
			}
			return 0.4f;
		}));
		for (const TPair<FString, FName>& V : TArray<TPair<FString, FName>>{ { TEXT("ChannelHomeAI"), TEXT("A1_AreaStart_1") }, { TEXT("ChannelEngineer"), TEXT("A1_EntitySeen_1") } })
		{
			S.Add(Do(V.Key + TEXT(": push"), [V]()
			{
				if (UTextChannelComponent* C = Channel())
				{
					C->ClearAll();
					FTextChannelContext Context;
					Context.BatteryFraction = 1.0;
					C->PushRow(V.Value, Context);
				}
				return 0.2f;
			}));
			S.Add(WaitFor(V.Key + TEXT(": typed out"), []() { const UTextChannelComponent* C = Channel(); return C && C->IsVisible() && C->IsTypedOut(); }, 20.f, [V]()
			{
				AuditFonts(V.Key == TEXT("ChannelHomeAI") ? TEXT("text channel, home AI") : TEXT("text channel, engineer"), FirstWidget<UTextChannelWidget>(), 1);
				return 0.1f;
			}));
			AddShot(S, V.Key, 0.1f);
		}
		S.Add(Do(TEXT("channel: fixture rows away"), []()
		{
			if (UTextChannelComponent* C = Channel())
			{
				C->ClearAll();
			}
			NarrativeFixture::Restore(W());
			G.bFixture = false;
			return 0.2f;
		}));

		// --- plates ---
		S.Add(Do(TEXT("monitors: plates"), []()
		{
			const APrologueMonitorRoom* R = Room();
			const TArray<UStaticMeshComponent*> Plates = R ? R->GetPlates() : TArray<UStaticMeshComponent*>();
			int32 Good = 0;
			FString Owners;
			for (const UStaticMeshComponent* P : Plates)
			{
				Good += P && P->IsVisible() && P->bHiddenInSceneCapture && P->GetMaterial(0) == UTheLastShiftSettings::GetScreenMaterial() && P->GetCollisionEnabled() == ECollisionEnabled::NoCollision
					&& P->GetAttachParent() && P->GetAttachParent()->GetOwner() == P->GetOwner() ? 1 : 0;
				if (P && !Owners.Contains(GetNameSafe(P->GetOwner())))
				{
					Owners += GetNameSafe(P->GetOwner()) + TEXT(" ");
				}
			}
			const FVector Scale = Plates.Num() ? Plates[0]->GetRelativeScale3D() : FVector::ZeroVector;
			Check(Plates.Num() > 0 && Plates.Num() % 24 == 0 && Good == Plates.Num(),
				FString::Printf(TEXT("monitors: %d plates (24 a monitor wall), %d shown in %s, not captured, colliding with nothing, components of the wall's own actor (%s); size %.1f x %.1f cm"),
					Plates.Num(), Good, *GetNameSafe(UTheLastShiftSettings::GetScreenMaterial()), *Owners, Scale.X * 100.0, Scale.Y * 100.0));
			return 0.f;
		}));
		AddFaceCheck(S, TEXT("MonitorBlack"), []() { const APrologueMonitorRoom* R = Room(); const TArray<UStaticMeshComponent*> P = R ? R->GetPlates() : TArray<UStaticMeshComponent*>(); return P.IsValidIndex(11) ? P[11] : nullptr; }, false);
		S.Add(Do(TEXT("terminals: plates"), []()
		{
			int32 N = 0;
			for (TActorIterator<APowerTerminal> It(W()); It; ++It)
			{
				const UStaticMeshComponent* P = It->GetScreenPlate();
				const bool bOk = It->bModelledBody ? It->IsScreenPlateShown() && P->bHiddenInSceneCapture : !It->IsScreenPlateShown();
				Check(bOk, FString::Printf(TEXT("terminal %s (%s body): plate shown %d"), *It->GetName(), It->bModelledBody ? TEXT("modelled") : TEXT("cube"), It->IsScreenPlateShown() ? 1 : 0));
				++N;
			}
			Note(FString::Printf(TEXT("%d terminals"), N));
			return 0.f;
		}));
		AddFaceCheck(S, TEXT("TerminalBlackHome"), []() -> UStaticMeshComponent*
		{
			for (TActorIterator<APowerTerminal> It(W()); It; ++It)
			{
				if (It->IsScreenPlateShown())
				{
					return It->GetScreenPlate();
				}
			}
			return nullptr;
		}, false);

		// --- the monitor room ---
		S.Add(Do(TEXT("monitor room: view"), []()
		{
			const APrologueMonitorRoom* R = Room();
			const TArray<UStaticMeshComponent*> P = R ? R->GetPlates() : TArray<UStaticMeshComponent*>();
			if (P.IsValidIndex(11))
			{
				StandBefore(P[11], 650.0);
			}
			return 0.8f;
		}));
		AddShot(S, TEXT("MonitorRoom"));

		// --- the rack lights ---
		S.Add(Do(TEXT("rack lights: built"), []()
		{
			const APrologueMonitorRoom* R = Room();
			const FRackLightsTuning T = UTheLastShiftSettings::Get()->GetRackLightsTuning();
			int32 Second = 0;
			TSet<FString> Rhythms;
			for (int32 I = 0; R && I < R->GetLightCount(); ++I)
			{
				FVector L;
				int32 C;
				float P, Ph;
				bool bOn;
				R->GetLight(I, L, C, P, Ph, bOn);
				Second += C;
				Rhythms.Add(FString::Printf(TEXT("%.4f/%.4f"), P, Ph));
			}
			const int32 N = R ? R->GetLightCount() : 0;
			const float Share = N ? static_cast<float>(Second) / N : 0.f;
			Check(N > 0 && N % (6 * T.PerServer) == 0 && R->AreLightsShown() && R->GetLightComponent(0)->bHiddenInSceneCapture && R->GetLightComponent(1)->bHiddenInSceneCapture
				&& !R->GetLightComponent(0)->CastShadow,
				FString::Printf(TEXT("rack lights: %d lights (%d a server, 6 servers a rack), shown in Normal time, never captured by a scan, no shadow"), N, T.PerServer));
			Check(Rhythms.Num() == N && FMath::Abs(Share - T.SecondShare) <= 0.15f,
				FString::Printf(TEXT("rack lights: %d rhythms (period/phase) for %d lights; %d in the second colour (%.2f, SecondShare %.2f)"), Rhythms.Num(), N, Second, Share, T.SecondShare));
			G.Toggles.Reset();
			G.LastOn.Reset();
			G.BlinkFrames = 0;
			G.NeighbourDiffer = G.NeighbourPairs = 0;
			G.Sampler = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { SampleLights(); return true; }));
			return 3.5f;
		}));
		S.Add(Do(TEXT("rack lights: blinking"), []()
		{
			FTSTicker::GetCoreTicker().RemoveTicker(G.Sampler);
			G.Sampler.Reset();
			int32 Blinked = 0;
			for (const int32 T : G.Toggles)
			{
				Blinked += T >= 2 ? 1 : 0;
			}
			const float Differ = G.NeighbourPairs ? static_cast<float>(G.NeighbourDiffer) / G.NeighbourPairs : 0.f;
			Check(G.Toggles.Num() > 0 && Blinked == G.Toggles.Num() && Differ > 0.2f,
				FString::Printf(TEXT("rack lights: over 3.5 s (%d frames) %d of %d lights went on and off at least once; neighbours differed in %.0f%% of the frames (out of step)"),
					G.BlinkFrames, Blinked, G.Toggles.Num(), Differ * 100.f));
			const APrologueMonitorRoom* R = Room();
			const TArray<UStaticMeshComponent*> P = R ? R->GetPlates() : TArray<UStaticMeshComponent*>();
			FVector L;
			int32 C;
			float Pe, Ph;
			bool bOn;
			if (R && R->GetLight(FMath::Min(7, R->GetLightCount() - 1), L, C, Pe, Ph, bOn))
			{
				const FVector Ahead(FMath::Sign(R->GetActorLocation().X - L.X), 0.0, 0.0);
				StandOnFloor(L.X + Ahead.X * 250.0, L.Y + Ahead.Y * 250.0, L);
			}
			return 0.8f;
		}));
		AddShot(S, TEXT("RackLights"));
		S.Add(Do(TEXT("rack lights: the pause menu"), []()
		{
			AStdPlayerController* C = StdPC();
			const bool bOpen = C && C->PauseGame();
			G.MenuOn = LightStates();
			Check(bOpen, TEXT("rack lights: the pause menu opens"));
			return 1.5f;
		}));
		S.Add(Do(TEXT("rack lights: still behind the menu"), []()
		{
			const TArray<uint8> Now = LightStates();
			Check(Now.Num() > 0 && Now == G.MenuOn, FString::Printf(TEXT("rack lights: 1.5 s behind the pause menu, none of %d lights changed"), Now.Num()));
			if (AStdPlayerController* C = StdPC())
			{
				C->ResumeGame();
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("rack lights: Stop"), []()
		{
			const ULidarScanComponent* L = Lidar();
			G.BlinkFrames = L ? L->GetScanCount() : 0;
			if (UTimeStateSubsystem* T = TS()) { T->Stop(); }
			return 0.15f;
		}));
		S.Add(Do(TEXT("rack lights: hidden while Stopped"), []()
		{
			const ULidarScanComponent* L = Lidar();
			Check(Room() && !Room()->AreLightsShown() && L && L->GetScanCount() == G.BlinkFrames, TEXT("rack lights: hidden while Stopped (before the first scan)"));
			RequestShot(TEXT("StoppedBeforeScan"), false);
			return 0.f;
		}));
		S.Add(WaitFor(TEXT("rack lights: Stopped shot"), []() { return G.bShotReady; }, 5.f, []()
		{
			int32 Max = 0;
			for (const FColor& C : G.Shot.Pixels)
			{
				Max = FMath::Max(Max, Level(C));
			}
			const ULidarScanComponent* L = Lidar();
			Check(Max <= 2 && L && L->GetScanCount() == G.BlinkFrames, FString::Printf(TEXT("rack lights: a Stopped frame before the first scan, every pixel at most 2 levels (largest %d)"), Max));
			if (UTimeStateSubsystem* T = TS()) { T->Resume(); }
			return 1.f;
		}));
		AddRackCapture(S, TEXT("RackScanWith"), true);
		AddRackCapture(S, TEXT("RackScanWithout"), false);
		S.Add(Do(TEXT("rack lights: not in a scan"), []()
		{
			const TArray<float>* A = G.Depths.Find(TEXT("RackScanWith"));
			const TArray<float>* B = G.Depths.Find(TEXT("RackScanWithout"));
			int32 Differ = 0;
			for (int32 I = 0; A && B && A->Num() == B->Num() && I < A->Num(); ++I)
			{
				Differ += FMath::Abs((*A)[I] - (*B)[I]) > 0.5f ? 1 : 0;
			}
			Check(A && B && A->Num() > 1000 && A->Num() == B->Num() && Differ == 0,
				FString::Printf(TEXT("rack lights: the scan's capture with the lights shown is the capture without them (%d texels, %d differ by more than 0.5 cm)"), A ? A->Num() : -1, Differ));
			return 0.f;
		}));
		S.Add(Do(TEXT("rack lights: values from the settings"), []()
		{
			UTheLastShiftSettings* Settings = GetMutableDefault<UTheLastShiftSettings>();
			G.SavedRack = Settings->RackLights;
			Settings->RackLights.PerServer = 1;
			Settings->RackLights.SecondShare = 1.f;
			APrologueMonitorRoom* R = Room();
			const int32 Before = R ? R->GetLightCount() : 0;
			if (R)
			{
				R->Rebuild();
			}
			int32 Second = 0;
			for (int32 I = 0; R && I < R->GetLightCount(); ++I)
			{
				FVector L;
				int32 C;
				float P, Ph;
				bool bOn;
				R->GetLight(I, L, C, P, Ph, bOn);
				Second += C;
			}
			const int32 After = R ? R->GetLightCount() : 0;
			Check(Before > 0 && After * G.SavedRack.PerServer == Before && Second == After,
				FString::Printf(TEXT("rack lights: PerServer 1 and SecondShare 1 for the session: %d lights (from %d), %d in the second colour"), After, Before, Second));
			Settings->RackLights = G.SavedRack;
			if (R)
			{
				R->Rebuild();
			}
			return 0.f;
		}));
	}

	// =================================================================================================================
	// Area2_ServerHalls
	// =================================================================================================================

	static ALogTerminal* ConsoleTerminal()
	{
		if (!G.Console.IsValid())
		{
			G.Console = First<ALogTerminal>();
		}
		return G.Console.Get();
	}

	static void BuildArea(TArray<FStep>& S)
	{
		S.Add(WaitFor(TEXT("Area2: the area ready, the pawn"), []() { return AAreaGenerator::IsAreaReady(W()) && Pawn() && ConsoleTerminal() && TS(); }, 60.f));
		S.Add(Do(TEXT("area: keep the run's state"), []()
		{
			const URunSubsystem* Run = URunSubsystem::Get(W());
			const UEnergySubsystem* Energy = UEnergySubsystem::Get(W());
			G.SavedArchive = Run ? Run->GetArchive() : FSuitArchive();
			G.bArchiveSaved = Run != nullptr;
			G.SavedEnergy = Energy ? Energy->GetBankedEnergy() : 0;
			const ALogTerminal* T = ConsoleTerminal();
			Check(T && !T->IsUnlocked() && T->IsScreenPlateShown(), FString::Printf(TEXT("log console %s: locked, its screen's plate shown"), *GetNameSafe(T)));
			return 0.f;
		}));
		AddFaceCheck(S, TEXT("LogUnpaid"), []() { const ALogTerminal* T = ConsoleTerminal(); return T ? T->GetScreenPlate() : nullptr; }, true);
		AddFaceCheck(S, TEXT("GateTerminalBlack"), []() -> UStaticMeshComponent*
		{
			for (TActorIterator<APowerTerminal> It(W()); It; ++It)
			{
				if (!Cast<ALogTerminal>(*It) && It->IsScreenPlateShown())
				{
					return It->GetScreenPlate();
				}
			}
			return nullptr;
		}, true);
		S.Add(Do(TEXT("interact prompt: before the console"), []()
		{
			const ALogTerminal* T = ConsoleTerminal();
			StandBefore(T ? T->GetScreenPlate() : nullptr, 110.0);
			Flashlight(true);
			return 1.0f;
		}));
		S.Add(Do(TEXT("interact prompt: fonts"), []() { AuditFonts(TEXT("Interact prompt"), FirstWidget<UInteractPromptWidget>(), 1); return 0.f; }));
		AddShot(S, TEXT("InteractPrompt"), 0.2f);
		S.Add(Do(TEXT("log console: pay"), []()
		{
			UEnergySubsystem* Energy = UEnergySubsystem::Get(W());
			ALogTerminal* T = ConsoleTerminal();
			if (Energy && T)
			{
				Energy->AddBankedEnergy(T->GetCost() + 1);
			}
			const bool bPaid = T && T->TryActivate(Pawn());
			Check(bPaid && T->IsUnlocked(), TEXT("log console: paid and unlocked"));
			return 1.5f;
		}));
		S.Add(WaitFor(TEXT("log console: an entry on its screen"), []() { const ALogTerminal* T = ConsoleTerminal(); return T && !T->IsScreenPlateShown(); }, 5.f, []()
		{
			const ALogTerminal* T = ConsoleTerminal();
			StandBefore(T ? T->GetScreenPlate() : nullptr, 200.0);
			Check(T && !T->IsScreenPlateShown() && !T->IsScreenBlank(), TEXT("log console: while it shows an entry its plate is hidden: the screen shows it as before"));
			return 2.5f;
		}));
		AddShot(S, TEXT("LogPaid"), 0.1f);

		// --- the twin AI's terminal's plate (spawned here: the twin AI's room is area 3's) ---
		S.Add(Do(TEXT("twin AI terminal: plate"), []()
		{
			UWorld* World = W();
			const APawn* P = Pawn();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			ATwinAITerminal* Twin = World && P ? World->SpawnActor<ATwinAITerminal>(ATwinAITerminal::StaticClass(), FTransform(P->GetActorLocation() + FVector(0, 0, -5000)), Params) : nullptr;
			if (Twin)
			{
				Twin->bModelledBody = true;
				Twin->FinishSpawning(FTransform(P->GetActorLocation() + FVector(0, 0, -5000)));
				Twin->ApplyTerminalBody();
				G.Spawned = Twin;
			}
			FVector Centre, Normal, Up;
			FVector2D Size(0.0, 0.0);
			const bool bFace = Twin && Twin->GetScreenFace(Centre, Normal, Up, Size);
			const UStaticMeshComponent* Plate = Twin ? Twin->GetScreenPlate() : nullptr;
			Check(bFace && Twin->IsScreenPlateShown() && Plate->bHiddenInSceneCapture && Plate->GetMaterial(0) == UTheLastShiftSettings::GetScreenMaterial(),
				FString::Printf(TEXT("twin AI terminal (SM_TwinTerminal): its desk's plate shown, %.0f x %.0f cm, not captured, in %s"), Size.X, Size.Y, Plate ? *GetNameSafe(Plate->GetMaterial(0)) : TEXT("-")));
			if (Twin)
			{
				Twin->Destroy();
			}
			return 0.f;
		}));

		// --- the pause menu and the archive (it holds the console's entries now) ---
		S.Add(Do(TEXT("pause menu: open"), []()
		{
			AStdPlayerController* C = StdPC();
			Check(C && C->PauseGame(), TEXT("the pause menu opens"));
			return 0.6f;
		}));
		S.Add(Do(TEXT("pause menu: fonts"), []() { AuditFonts(TEXT("pause menu"), Top(), 4); return 0.f; }));
		AddShot(S, TEXT("PauseMenu"));
		S.Add(Do(TEXT("archive: open"), []() { Check(ClickNamed(Top(), TEXT("Button_Archive")), TEXT("the pause menu's Archive opens the archive")); return 0.6f; }));
		S.Add(Do(TEXT("archive: fonts"), []() { AuditFonts(TEXT("archive"), Top(), 2); return 0.f; }));
		AddShot(S, TEXT("Archive"));
		S.Add(Do(TEXT("archive: an item"), []() { ClickNamed(Top(), TEXT("Button_Choice_0")); return 0.6f; }));
		S.Add(Do(TEXT("archive item: fonts"), []() { AuditFonts(TEXT("archive item"), Top(), 2); return 0.f; }));
		S.Add(Do(TEXT("pause menu: close"), []()
		{
			if (AStdPlayerController* C = StdPC())
			{
				C->ResumeGame();
			}
			return 0.5f;
		}));
		S.Add(Do(TEXT("area: fonts of the HUD"), []()
		{
			AuditFonts(TEXT("text channel"), FirstWidget<UTextChannelWidget>(), 1);
			return 0.f;
		}));
		S.Add(Do(TEXT("area: put the run's state back"), []()
		{
			URunSubsystem* Run = URunSubsystem::Get(W());
			UEnergySubsystem* Energy = UEnergySubsystem::Get(W());
			if (ALogTerminal* T = ConsoleTerminal())
			{
				T->Relock();
			}
			if (Run && G.bArchiveSaved)
			{
				Run->ClearArchive();
				Run->StoreLogEntries(G.SavedArchive.LogOrders);
				for (int32 Part = 1; Part <= G.SavedArchive.TwinPartsPlayed; ++Part)
				{
					Run->StoreTwinPart(Part);
				}
			}
			if (Energy)
			{
				Energy->RestoreBankedEnergy(G.SavedEnergy);
			}
			const ALogTerminal* T = ConsoleTerminal();
			Check(Run && Run->GetArchive().LogOrders.Num() == G.SavedArchive.LogOrders.Num() && Energy && Energy->GetBankedEnergy() == G.SavedEnergy && T && !T->IsUnlocked(),
				TEXT("area: the archive, the banked energy and the console's lock put back"));
			return 0.5f;
		}));
		S.Add(Do(TEXT("area: the console black again"), []()
		{
			const ALogTerminal* T = ConsoleTerminal();
			Check(T && T->IsScreenPlateShown(), TEXT("log console: locked again, its screen black again"));
			return 0.f;
		}));
	}

	static void Start(bool bQuit)
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
		UWorld* World = W();
		G.Map = World ? FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : FString(TEXT("none"));
		if (G.Map == TEXT("MainMenu"))
		{
			BuildMainMenu(G.Steps);
		}
		else if (G.Map == TEXT("Prologue_Home"))
		{
			BuildHome(G.Steps);
		}
		else if (G.Map.StartsWith(TEXT("Area")))
		{
			BuildArea(G.Steps);
		}
		else
		{
			Check(false, FString::Printf(TEXT("tls.StyleCheck runs in MainMenu, Prologue_Home or an area map (here: %s)"), *G.Map));
		}
		UE_LOG(LogTheLastShift, Display, TEXT("StyleCheck: %s, %d steps."), *G.Map, G.Steps.Num());
		G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsStyleCheckCommand(
	TEXT("tls.StyleCheck"),
	TEXT("tls.StyleCheck [Quit=0]: (fresh PIE or a package) Brief 27's look in the map it runs in: MainMenu (the font of every screen, the static at a scene's cut, the fit of the narrative tables, the travel screen), Prologue_Home (the channel's font, black monitors and terminals, the rack lights), an area (log console unpaid and paid, a gate terminal, the twin AI terminal's plate, the Interact prompt, the pause menu, the archive). Shots to Saved/Screenshots/Brief27."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		StyleCheck::Start(Args.Num() > 0 && FCString::Atoi(*Args[0]) != 0);
	}));

#endif // !UE_BUILD_SHIPPING

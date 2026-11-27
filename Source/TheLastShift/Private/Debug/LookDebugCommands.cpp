// SPDX-License-Identifier: MIT

// The look of the surfaces and the colour code of the two structures (Brief 21). Development builds only.
//  tls.Look.Report: the map's variant and every value pushed into MPC_Surface; every mesh of the world with the material
//    each of its slots shows (its family: MI_Surface_<Family>) and how many components and instances show it; for each
//    material, whether it is opaque, and whether emissive, world position offset, pixel depth offset or displacement is
//    connected (each must be no).
//  tls.Look.Set <Home|Twin> <Field> <Value...>: sets a value of SurfaceLook for the session and pushes it at once. Fields:
//    Tint R G B, BaseTint R G B, AccentStrength, AccentLevel, Tone, Wear, Dirt, PanelVariation.
//  tls.Look.Text <HomeAI|Engineer|TwinAI|EngineerParadise> R G B [A]: sets a text colour of TextColors for the session (what is shown next).
//  tls.Look.EngineerTone <Share>: the engineer's colour for the session: the home AI's lightened toward white by Share
//    (0 to 1), with the home AI's alpha.
//  tls.Look.Print: the ini lines of SurfaceLook and TextColors as they are now (to store what was chosen).
//  tls.Look.Check [Label] [Quit]: in the current map, Normal time: every terminal with a body of its own shot from 2 m
//    (the flashlight on outside home) and the hue of its painted pixels measured against the map's tint; the first gate
//    frame, curb (areas) or door frame (prologue maps) the same from 3 m; the ship (a map with an actor tagged Ship) with
//    home's base tint and the twin's set to test hues, its pixels measured (home's) and the values put back; then a Stop:
//    a shot before the first scan (every pixel at most 2 levels) and one with the first scan. Shots to
//    Saved/Screenshots/Brief21.
//  tls.Look.TextShots [Label] [Quit]: (a map with the player's text channel) with fixture rows, a home AI row, an
//    engineer's row and a twin AI part, each shot when typed out and its glyphs' cores (the engineer's bar left out)
//    measured against its colour of TextColors (tolerance 14 levels a channel); the engineer's bar measured on its own; the
//    engineer's tone against the home AI's (the same hue within 6 degrees, lighter by at least 14 levels). Shots to
//    Saved/Screenshots/Brief21.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Area/AreaGenerator.h"
#include "Debug/TestLidarCharacter.h"
#include "Components/Border.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Energy/LogTerminal.h"
#include "Energy/PowerTerminal.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Look/SurfaceLookSubsystem.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NarrativeFixture.h"
#include "Run/TextChannelWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "TlsDebugCommon.h"
#include "UnrealClient.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Widgets/SWindow.h"

namespace LookDebug
{
	static UWorld* W() { return TimeStateDebug::ResolveGameWorld(nullptr); }
	static APlayerController* PC() { UWorld* World = W(); return World ? World->GetFirstPlayerController() : nullptr; }
	static APawn* Pawn() { const APlayerController* C = PC(); return C ? C->GetPawn() : nullptr; }

	static FString MapName(const UWorld* World)
	{
		return World ? FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : FString();
	}

	static UTheLastShiftSettings* MutableSettings() { return GetMutableDefault<UTheLastShiftSettings>(); }

	static bool ApplyNow(UWorld* World)
	{
		USurfaceLookSubsystem* Look = World ? World->GetSubsystem<USurfaceLookSubsystem>() : nullptr;
		return Look && Look->ApplyLook();
	}

	// --- tls.Look.Report ---

	struct FMeshUse
	{
		int32 Components = 0;
		int32 Instances = 0;
	};

	static void Report(UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (!World)
		{
			World = InWorld;
		}
		if (!World)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Look.Report: no world."));
			return;
		}
		const USurfaceLookSubsystem* Look = World->GetSubsystem<USurfaceLookSubsystem>();
		const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
		UMaterialParameterCollection* Collection = USurfaceLookSubsystem::GetCollection();
		const UMaterialParameterCollectionInstance* Instance = Collection ? World->GetParameterCollectionInstance(Collection) : nullptr;
		UE_LOG(LogTheLastShift, Display, TEXT("LOOK map %s: variant %s (pushed %d), collection %s"), *MapName(World), Look && Look->IsHomeLook() ? TEXT("home") : TEXT("twin"),
			Look ? Look->GetPushedVariant() : -1, *GetNameSafe(Collection));
		for (int32 Which = 0; Which < 2; ++Which)
		{
			const bool bHome = Which == 0;
			const FSurfaceVariant V = Settings->GetSurfaceVariant(bHome);
			UE_LOG(LogTheLastShift, Display, TEXT("  %s: Tint %s, AccentStrength %.2f, AccentLevel %.2f, BaseTint %s, Tone %.2f, Wear %.2f, Dirt %.2f, PanelVariation %.2f"),
				bHome ? TEXT("Home") : TEXT("Twin"), *V.Tint.ToString(), V.AccentStrength, V.AccentLevel, *V.BaseTint.ToString(), V.Tone, V.Wear, V.Dirt, V.PanelVariation);
			if (Instance)
			{
				float Wear = -1.f, Tone = -1.f;
				FLinearColor Tint;
				Instance->GetScalarParameterValue(USurfaceLookSubsystem::ParamName(bHome, TEXT("Wear")), Wear);
				Instance->GetScalarParameterValue(USurfaceLookSubsystem::ParamName(bHome, TEXT("Tone")), Tone);
				Instance->GetVectorParameterValue(USurfaceLookSubsystem::ParamName(bHome, TEXT("Tint")), Tint);
				UE_LOG(LogTheLastShift, Display, TEXT("  %s in the collection: Tint %s, Tone %.2f, Wear %.2f"), bHome ? TEXT("Home") : TEXT("Twin"), *Tint.ToString(), Tone, Wear);
			}
		}
		// Every mesh with what its slots show.
		TMap<FString, FMeshUse> Uses;
		TMap<FString, int32> Families;
		TSet<UMaterial*> Bases;
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			UStaticMeshComponent* C = *It;
			if (!C || C->GetWorld() != World || !C->IsRegistered() || !C->GetStaticMesh() || !C->IsVisible() || !C->ShouldRender())
			{
				continue;
			}
			const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(C);
			const int32 Count = ISM ? ISM->GetInstanceCount() : 1;
			if (Count == 0)
			{
				continue;
			}
			FString Mats;
			for (int32 S = 0; S < C->GetNumMaterials(); ++S)
			{
				UMaterialInterface* M = C->GetMaterial(S);
				Mats += (S ? TEXT(", ") : TEXT("")) + GetNameSafe(M);
				// A surface outside the families (not MI_Surface_*, not a light's own material: the hologram's, the rack lights' and,
				// Brief 36B, the battery's glow and its hall's lit screens, all hidden while Stopped): named, to be found. A material
				// made at runtime is named by the asset it was made from.
				const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(M);
				const FString MatName = Mid && Mid->Parent ? GetNameSafe(Mid->Parent) : GetNameSafe(M);
				const FString BaseName = M && M->GetMaterial() ? M->GetMaterial()->GetName() : FString();
				const bool bLight = BaseName.Contains(TEXT("Hologram")) || BaseName == TEXT("M_BatteryGlow") || BaseName == TEXT("M_BatteryScreen");
				if (!MatName.StartsWith(TEXT("MI_Surface_")) && !bLight && C->bRenderInMainPass)
				{
					UE_LOG(LogTheLastShift, Display, TEXT("  OUTSIDE THE FAMILIES: %s on %s of %s"), *MatName, *C->GetName(), *GetNameSafe(C->GetOwner()));
				}
				Families.FindOrAdd(MatName) += Count;
				if (M && M->GetMaterial())
				{
					Bases.Add(M->GetMaterial());
				}
			}
			const FString Key = C->GetStaticMesh()->GetName() + TEXT(" -> ") + Mats + (C->bRenderInMainPass ? FString() : FString(TEXT(" (not in the main pass)")));
			FMeshUse& U = Uses.FindOrAdd(Key);
			++U.Components;
			U.Instances += Count;
		}
		Uses.KeySort(TLess<FString>());
		for (const TPair<FString, FMeshUse>& U : Uses)
		{
			UE_LOG(LogTheLastShift, Display, TEXT("  MESH %s: %d components, %d instances"), *U.Key, U.Value.Components, U.Value.Instances);
		}
		Families.KeySort(TLess<FString>());
		for (const TPair<FString, int32>& F : Families)
		{
			UE_LOG(LogTheLastShift, Display, TEXT("  FAMILY %s: %d meshes and instances"), *F.Key, F.Value);
		}
		int32 Bad = 0;
		for (UMaterial* M : Bases)
		{
			const bool bOpaque = M->GetBlendMode() == BLEND_Opaque;
			const bool bEmissive = M->HasEmissiveColorConnected();
			const bool bWpo = M->HasVertexPositionOffsetConnected();
			const bool bPdo = M->HasPixelDepthOffsetConnected();
			const bool bDisp = M->HasDisplacementConnected();
			const bool bSurface = M->GetName().StartsWith(TEXT("M_Surface"));
			if (bSurface && (!bOpaque || bEmissive || bWpo || bPdo || bDisp))
			{
				++Bad;
			}
			UE_LOG(LogTheLastShift, Display, TEXT("  MATERIAL %s: opaque %d, emissive %d, world position offset %d, pixel depth offset %d, displacement %d%s"), *M->GetName(),
				bOpaque ? 1 : 0, bEmissive ? 1 : 0, bWpo ? 1 : 0, bPdo ? 1 : 0, bDisp ? 1 : 0, bSurface ? TEXT(" (a surface material)") : TEXT(""));
		}
		UE_LOG(LogTheLastShift, Display, TEXT("LOOK map %s: %d meshes and materials, %d families, %d surface materials not opaque or with emissive, offset or displacement; M_Greybox shown %d times"),
			*MapName(World), Uses.Num(), Families.Num(), Bad, Families.FindRef(TEXT("M_Greybox")));
	}

	// --- tls.Look.Set / Text / EngineerTone / Print ---

	static float ValueAt(const TArray<FString>& Values, int32 I)
	{
		return Values.IsValidIndex(I) ? FCString::Atof(*Values[I]) : 0.f;
	}

	static bool SetField(FSurfaceVariant& V, const FString& Field, const TArray<FString>& Values)
	{
		if (Values.Num() >= 3 && Field.Equals(TEXT("Tint")))
		{
			V.Tint = FLinearColor(ValueAt(Values, 0), ValueAt(Values, 1), ValueAt(Values, 2), 1.f);
			return true;
		}
		if (Values.Num() >= 3 && Field.Equals(TEXT("BaseTint")))
		{
			V.BaseTint = FLinearColor(ValueAt(Values, 0), ValueAt(Values, 1), ValueAt(Values, 2), 1.f);
			return true;
		}
		if (Values.Num() < 1)
		{
			return false;
		}
		const float X = ValueAt(Values, 0);
		float* Target = nullptr;
		if (Field.Equals(TEXT("AccentStrength"))) { Target = &V.AccentStrength; }
		else if (Field.Equals(TEXT("AccentLevel"))) { Target = &V.AccentLevel; }
		else if (Field.Equals(TEXT("Tone"))) { Target = &V.Tone; }
		else if (Field.Equals(TEXT("Wear"))) { Target = &V.Wear; }
		else if (Field.Equals(TEXT("Dirt"))) { Target = &V.Dirt; }
		else if (Field.Equals(TEXT("PanelVariation"))) { Target = &V.PanelVariation; }
		if (!Target)
		{
			return false;
		}
		*Target = X;
		return true;
	}

	static FString ColorText(const FLinearColor& C)
	{
		return FString::Printf(TEXT("(R=%.3f,G=%.3f,B=%.3f,A=%.3f)"), C.R, C.G, C.B, C.A);
	}

	static FString VariantText(const FSurfaceVariant& V)
	{
		return FString::Printf(TEXT("(Tint=%s,AccentStrength=%.3f,AccentLevel=%.3f,BaseTint=%s,Tone=%.3f,Wear=%.3f,Dirt=%.3f,PanelVariation=%.3f)"),
			*ColorText(V.Tint), V.AccentStrength, V.AccentLevel, *ColorText(V.BaseTint), V.Tone, V.Wear, V.Dirt, V.PanelVariation);
	}

	static void Print()
	{
		const UTheLastShiftSettings* S = UTheLastShiftSettings::Get();
		UE_LOG(LogTheLastShift, Display, TEXT("LOOK ini: SurfaceLook Home=%s"), *VariantText(S->SurfaceLook.Home));
		UE_LOG(LogTheLastShift, Display, TEXT("LOOK ini: SurfaceLook Twin=%s"), *VariantText(S->SurfaceLook.Twin));
		const FSuitTextColors& T = S->TextColors;
		UE_LOG(LogTheLastShift, Display, TEXT("LOOK ini: TextColors HomeAI=%s, Engineer=%s, TwinAI=%s, EngineerParadise=%s"), *ColorText(T.HomeAI), *ColorText(T.Engineer), *ColorText(T.TwinAI), *ColorText(T.EngineerParadise));
		const FFlashlightTuning& F = S->Flashlight;
		UE_LOG(LogTheLastShift, Display, TEXT("LOOK ini: Flashlight=(Intensity=%.3f,bInverseSquaredFalloff=%s,FalloffExponent=%.3f,AttenuationRadius=%.1f,InnerConeAngle=%.1f,OuterConeAngle=%.1f)"),
			F.Intensity, F.bInverseSquaredFalloff ? TEXT("True") : TEXT("False"), F.FalloffExponent, F.AttenuationRadius, F.InnerConeAngle, F.OuterConeAngle);
	}

	// --- Shots measured on their pixels ---

	struct FShot
	{
		FString Name;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
		bool bReady = false;
	};
	static FShot GShot;
	static FDelegateHandle GShotHandle;
	static FString GLabel;

	static FString ShotPath(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Brief21"), GLabel + TEXT("_") + Name + TEXT(".png")));
	}

	static void HandleShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		// Unbound at once: while the delegate is bound the engine saves no other screenshot to its file.
		UGameViewportClient::OnScreenshotCaptured().Remove(GShotHandle);
		GShotHandle.Reset();
		GShot.Width = Width;
		GShot.Height = Height;
		GShot.Pixels = Pixels;
		for (FColor& C : GShot.Pixels)
		{
			C.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(GShot.Pixels.GetData(), GShot.Pixels.Num()), Png);
		const FString Path = ShotPath(GShot.Name);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Png, *Path);
		UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: shot %s (%d x %d)"), *GLabel, *Path, Width, Height);
		GShot.bReady = true;
	}

	static void RequestShot(const FString& Name, bool bShowUI)
	{
		GShot = FShot();
		GShot.Name = Name;
		if (!GShotHandle.IsValid())
		{
			GShotHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&HandleShot);
		}
		FScreenshotRequest::RequestScreenshot(FString(), bShowUI, false);
	}

	/** Hue (degrees), saturation and value (0 to 1) of an on-screen colour. */
	static FVector HSV(const FColor& C)
	{
		const float R = C.R / 255.f, G = C.G / 255.f, B = C.B / 255.f;
		const float Max = FMath::Max3(R, G, B), Min = FMath::Min3(R, G, B), D = Max - Min;
		float H = 0.f;
		if (D > 1.e-4f)
		{
			if (Max == R) { H = 60.f * FMath::Fmod((G - B) / D + 6.f, 6.f); }
			else if (Max == G) { H = 60.f * ((B - R) / D + 2.f); }
			else { H = 60.f * ((R - G) / D + 4.f); }
		}
		return FVector(H, Max > 0.f ? D / Max : 0.f, Max);
	}

	static float HueGap(float A, float B)
	{
		return FMath::Abs(FMath::Fmod(A - B + 540.f, 360.f) - 180.f);
	}

	/** The hue a tint shows on screen (its sRGB colour's). */
	static float TintHue(const FLinearColor& Tint)
	{
		return HSV(FLinearColor(Tint.R, Tint.G, Tint.B, 1.f).ToFColor(true)).X;
	}

	/** The screen rectangle of a world box from the player's view (false when it is behind the camera). */
	static bool ProjectBox(const FBox& Box, FBox2D& Out)
	{
		APlayerController* C = PC();
		Out = FBox2D(ForceInit);
		if (!C)
		{
			return false;
		}
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector P((I & 1) ? Box.Max.X : Box.Min.X, (I & 2) ? Box.Max.Y : Box.Min.Y, (I & 4) ? Box.Max.Z : Box.Min.Z);
			FVector2D S;
			if (!UGameplayStatics::ProjectWorldToScreen(C, P, S, false))
			{
				return false;
			}
			Out += S;
		}
		return Out.bIsValid;
	}

	struct FHueSample
	{
		int32 Pixels = 0;      // inside the rectangle
		int32 Painted = 0;     // saturation >= 0.3 and value >= 0.1
		float Hue = 0.f;       // circular mean of the painted ones
		float Value = 0.f;     // their mean value
		int32 NearHue = 0;     // within 30 degrees of the wanted hue
		FColor Mean = FColor(0, 0, 0);  // every pixel of the rectangle
		int32 White = 0;       // min channel at 235 or above: burnt
	};

	/** Painted pixels of the last shot inside Rect (shrunk to its central Share), their mean hue and how many lie near Want. */
	static FHueSample SampleHue(const FBox2D& InRect, float Want, float Share = 1.f)
	{
		FHueSample S;
		if (!InRect.bIsValid || GShot.Pixels.Num() < GShot.Width * GShot.Height)
		{
			return S;
		}
		const FVector2D C = InRect.GetCenter(), E = InRect.GetExtent() * Share;
		const int32 X0 = FMath::Clamp(FMath::FloorToInt32(C.X - E.X), 0, GShot.Width), X1 = FMath::Clamp(FMath::CeilToInt32(C.X + E.X), 0, GShot.Width);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(C.Y - E.Y), 0, GShot.Height), Y1 = FMath::Clamp(FMath::CeilToInt32(C.Y + E.Y), 0, GShot.Height);
		double SX = 0.0, SY = 0.0, SV = 0.0;
		int64 MR = 0, MG = 0, MB = 0;
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				++S.Pixels;
				const FColor& Px = GShot.Pixels[Y * GShot.Width + X];
				MR += Px.R; MG += Px.G; MB += Px.B;
				S.White += FMath::Min3(Px.R, Px.G, Px.B) >= 235 ? 1 : 0;
				const FVector V = HSV(Px);
				if (V.Y >= 0.3 && V.Z >= 0.1)
				{
					++S.Painted;
					SX += FMath::Cos(FMath::DegreesToRadians(V.X));
					SY += FMath::Sin(FMath::DegreesToRadians(V.X));
					SV += V.Z;
					S.NearHue += HueGap(V.X, Want) <= 30.f ? 1 : 0;
				}
			}
		}
		if (S.Pixels > 0)
		{
			S.Mean = FColor(static_cast<uint8>(MR / S.Pixels), static_cast<uint8>(MG / S.Pixels), static_cast<uint8>(MB / S.Pixels));
		}
		if (S.Painted > 0)
		{
			S.Hue = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(SY, SX)) + 360.f, 360.f);
			S.Value = static_cast<float>(SV / S.Painted);
		}
		return S;
	}

	// --- The steps of a run ---

	struct FStep
	{
		FString Label;
		TFunction<float()> Run;          // returns the seconds to wait before the next step
		TFunction<bool()> Until;         // optional: the step waits until this is true (timeout 20 s)
	};

	struct FRun
	{
		TArray<FStep> Steps;
		int32 Next = 0;
		double WaitUntil = 0.0;
		double StepStarted = 0.0;
		bool bWaiting = false;
		bool bQuit = false;
		int32 Passed = 0;
		int32 Failed = 0;
		FString Name;
		FTSTicker::FDelegateHandle Ticker;
		FSurfaceLook SavedLook;
		FSuitTextColors SavedText;
	};
	static FRun GRun;

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: %s %s"), *GLabel, bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	}

	static bool TickRun(float)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now < GRun.WaitUntil)
		{
			return true;
		}
		if (GRun.bWaiting)
		{
			const FStep Step = GRun.Steps[GRun.Next - 1];  // a copy: a step may append steps
			if (!Step.Until() && Now - GRun.StepStarted < 20.0)
			{
				return true;
			}
			GRun.bWaiting = false;
			GRun.WaitUntil = Now + Step.Run();
			return true;
		}
		if (!GRun.Steps.IsValidIndex(GRun.Next))
		{
			UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: ===== %s: %d passed, %d failed ===== summary (%s)"), *GLabel, *GRun.Name, GRun.Passed, GRun.Failed,
				GRun.Failed == 0 ? TEXT("PASS") : TEXT("FAIL"));
			if (GRun.bQuit)
			{
				FPlatformMisc::RequestExit(false, TEXT("tls.Look"));
			}
			GRun.Ticker.Reset();
			return false;
		}
		const FStep Step = GRun.Steps[GRun.Next++];  // a copy: a step may append steps
		UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: %s"), *GLabel, *Step.Label);
		if (Step.Until)
		{
			GRun.bWaiting = true;
			GRun.StepStarted = Now;
			return true;
		}
		GRun.WaitUntil = Now + Step.Run();
		return true;
	}

	static void StartRun(const FString& Name, const FString& Label, bool bQuit, TArray<FStep>&& Steps)
	{
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.Name = Name;
		GRun.bQuit = bQuit;
		GRun.Steps = MoveTemp(Steps);
		GRun.SavedLook = UTheLastShiftSettings::Get()->SurfaceLook;
		GRun.SavedText = UTheLastShiftSettings::Get()->TextColors;
		GLabel = Label;
		UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: ===== %s in %s: %d steps ====="), *GLabel, *Name, *MapName(W()), GRun.Steps.Num());
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
	}

	static FStep Do(const FString& Label, TFunction<float()> Run) { return { Label, MoveTemp(Run), nullptr }; }
	static FStep WaitFor(const FString& Label, TFunction<bool()> Until, TFunction<float()> Run) { return { Label, MoveTemp(Run), MoveTemp(Until) }; }

	// --- Poses ---

	/** Stands the pawn on the floor under Floor's (X, Y) and turns its view to LookAt. */
	static void StandAt(const FVector& Floor, const FVector& LookAt)
	{
		APawn* P = Pawn();
		APlayerController* C = PC();
		UWorld* World = W();
		if (!P || !C || !World)
		{
			return;
		}
		const ACharacter* Ch = Cast<ACharacter>(P);
		const double Half = Ch && Ch->GetCapsuleComponent() ? Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		double Z = Floor.Z + Half + 2.0;
		FHitResult Hit;
		FCollisionQueryParams Params(TEXT("TlsLook"), false, P);
		// From above the higher of the given floor and the pawn (a hangar's floor may stand above the given height).
		const FVector From(Floor.X, Floor.Y, FMath::Max(Floor.Z, P->GetActorLocation().Z) + 150.0);
		if (World->LineTraceSingleByChannel(Hit, From, From - FVector(0, 0, 2000), ECC_Visibility, Params)
			|| World->LineTraceSingleByChannel(Hit, From, From - FVector(0, 0, 2000), ECC_Pawn, Params))
		{
			Z = Hit.ImpactPoint.Z + Half + 2.0;
		}
		P->SetActorLocation(FVector(Floor.X, Floor.Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
		FVector Eye;
		FRotator Ignored;
		P->GetActorEyesViewPoint(Eye, Ignored);
		C->SetControlRotation((LookAt - Eye).Rotation());
	}

	/** Of the two points Centre +- Axis * Distance (on the floor), one a trace from Centre reaches without a hit and with a floor under it. */
	/** Toward (if not zero): the side facing it is tried first (a hangar's door: the hangar's side, not the exit's). */
	static bool PickSide(const FVector& Centre, const FVector& Axis, double Distance, FVector& Out, const FVector& Toward = FVector::ZeroVector)
	{
		UWorld* World = W();
		if (!World)
		{
			return false;
		}
		FCollisionQueryParams Params(TEXT("TlsLookSide"), false, Pawn());
		const double First = (!Toward.IsZero() && FVector::DotProduct(Axis.GetSafeNormal2D(), (Toward - Centre).GetSafeNormal2D()) < 0.0) ? -1.0 : 1.0;
		for (const double Sign : { First, -First })
		{
			const FVector Stand = Centre + Axis.GetSafeNormal2D() * Distance * Sign;
			FHitResult Hit;
			const FVector A(Centre.X, Centre.Y, Centre.Z + 150.0), B(Stand.X, Stand.Y, Centre.Z + 150.0);
			const bool bBlocked = World->LineTraceSingleByChannel(Hit, A + (B - A).GetSafeNormal() * 40.0, B, ECC_Pawn, Params);
			const bool bFloor = World->LineTraceSingleByChannel(Hit, B, B - FVector(0, 0, 2000), ECC_Pawn, Params);
			if (!bBlocked && bFloor)
			{
				Out = FVector(Stand.X, Stand.Y, Hit.ImpactPoint.Z);
				return true;
			}
		}
		return false;
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

	static bool IsHomeMap() { const UWorld* World = W(); return World && UTheLastShiftSettings::Get()->IsHomeLookMap(World->GetOutermost()->GetName()); }

	/** The body of a terminal: its largest static mesh component. */
	static UStaticMeshComponent* BodyOf(const AActor* A)
	{
		TArray<UStaticMeshComponent*> Meshes;
		A->GetComponents(Meshes);
		UStaticMeshComponent* Best = nullptr;
		for (UStaticMeshComponent* M : Meshes)
		{
			if (M && M->GetStaticMesh() && M->IsVisible() && (!Best || M->Bounds.SphereRadius > Best->Bounds.SphereRadius))
			{
				Best = M;
			}
		}
		return Best;
	}

	struct FTarget
	{
		FString Name;
		FBox Box = FBox(ForceInit);
		FVector Stand = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		/** A second, unmeasured shot from here (a terminal from 2 m, where the flashlight burns every surface white). */
		FVector NearStand = FVector::ZeroVector;
		bool bNear = false;
	};

	/** The first of Offsets from Centre with a floor under it and a clear line from Centre (at 1.5 m up). */
	static bool PickStand(const FVector& Centre, const TArray<FVector>& Offsets, FVector& Out)
	{
		UWorld* World = W();
		if (!World)
		{
			return false;
		}
		FCollisionQueryParams Params(TEXT("TlsLookStand"), false, Pawn());
		for (const FVector& O : Offsets)
		{
			const FVector A(Centre.X, Centre.Y, Centre.Z + 150.0), B(Centre.X + O.X, Centre.Y + O.Y, Centre.Z + 150.0);
			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(Hit, A + (B - A).GetSafeNormal() * 40.0, B, ECC_Pawn, Params);
			const bool bFloor = World->LineTraceSingleByChannel(Hit, B, B - FVector(0, 0, 2000), ECC_Pawn, Params);
			if (!bBlocked && bFloor)
			{
				Out = FVector(B.X, B.Y, Hit.ImpactPoint.Z);
				return true;
			}
		}
		return false;
	}

	/** What carries the map's accent: terminals with a body of their own, the first gate frame and curb (areas), the first door frame. */
	static TArray<FTarget> AccentTargets()
	{
		TArray<FTarget> Out;
		UWorld* World = W();
		if (!World)
		{
			return Out;
		}
		const UStaticMesh* Body = UTheLastShiftSettings::GetTerminalBodyMesh();
		int32 Terminals = 0;
		for (TActorIterator<APowerTerminal> It(World); It && Terminals < 4; ++It)
		{
			UStaticMeshComponent* M = BodyOf(*It);
			const bool bLog = It->IsA<ALogTerminal>();
			if (!M || (!bLog && M->GetStaticMesh() != Body))
			{
				continue;
			}
			FTarget T;
			T.Name = FString::Printf(TEXT("Terminal%d_%s"), Terminals++, *It->GetActorNameOrLabel());
			T.Box = M->Bounds.GetBox();
			const FVector Front = It->GetActorForwardVector();
			// At home (the rooms' light) measured from 2 m; elsewhere from 4 m, with a shot from 2 m: there the flashlight
			// burns every surface white (as it burned the grey before).
			const bool bHomeLight = IsHomeMap();
			T.Stand = It->GetActorLocation() + Front * (bHomeLight ? 200.0 : 400.0);
			// Measured from the farthest of 4, 3 and 2 m that has a floor at the terminal's own level in front of it (a
			// terminal near a ledge has none at 4 m).
			if (!bHomeLight)
			{
				const double BaseZ = T.Box.Min.Z;
				FCollisionQueryParams Params(TEXT("TlsLookTerminalStand"), false, Pawn());
				for (const double D : { 400.0, 300.0, 200.0 })
				{
					const FVector C = It->GetActorLocation() + Front.GetSafeNormal2D() * D;
					FHitResult Hit;
					if (World->LineTraceSingleByChannel(Hit, FVector(C.X, C.Y, BaseZ + 150.0), FVector(C.X, C.Y, BaseZ - 100.0), ECC_Pawn, Params)
						&& FMath::Abs(Hit.ImpactPoint.Z - BaseZ) < 60.0)
					{
						T.Stand = C;
						break;
					}
				}
			}
			T.NearStand = It->GetActorLocation() + Front * 200.0;
			T.bNear = !bHomeLight;
			T.Look = T.Box.GetCenter();
			Out.Add(T);
		}
		// Instanced dressing (areas) and placed frames (prologue maps).
		auto FirstInstance = [World](FName Tag, FTransform& OutT, FBox& OutBox) -> bool
		{
			for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() == World && It->ComponentHasTag(Tag) && It->GetInstanceCount() > 0 && It->GetStaticMesh())
				{
					It->GetInstanceTransform(0, OutT, true);
					OutBox = It->GetStaticMesh()->GetBoundingBox().TransformBy(OutT);
					return true;
				}
			}
			return false;
		};
		FTransform T0;
		FBox B0;
		UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: %d terminals with a body of their own"), *GLabel, Out.Num());
		if (FirstInstance(TEXT("AreaDressGateFrames"), T0, B0))
		{
			FTarget T;
			T.Name = TEXT("GateFrame0");
			T.Box = B0;
			// Looked at by one of its posts, so that the post stands in the flashlight's beam.
			T.Look = FVector(B0.GetCenter().X, B0.GetCenter().Y, B0.Min.Z + 150.0) + T0.GetUnitAxis(EAxis::Y) * (B0.GetExtent().Size2D() * 0.8);
			if (PickSide(FVector(B0.GetCenter().X, B0.GetCenter().Y, B0.Min.Z), T0.GetUnitAxis(EAxis::X), 350.0, T.Stand))
			{
				Out.Add(T);
			}
			else
			{
				UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: no free side 3.5 m from the first gate frame at %s"), *GLabel, *B0.GetCenter().ToCompactString());
			}
		}
		if (FirstInstance(TEXT("AreaDressCurbs"), T0, B0))
		{
			FTarget T;
			T.Name = TEXT("Curb0");
			// The first curb piece and the pieces of its run within 3.5 m of it (up to three): a single piece can lie whole
			// in a worn patch (the twin's wear), and the check is about the paint of the run, not of one piece.
			T.Box = B0;
			int32 Pieces = 1;
			for (TObjectIterator<UInstancedStaticMeshComponent> It; It && Pieces < 3; ++It)
			{
				if (It->GetWorld() != World || !It->ComponentHasTag(TEXT("AreaDressCurbs")) || !It->GetStaticMesh())
				{
					continue;
				}
				for (int32 I = 1; I < It->GetInstanceCount() && Pieces < 3; ++I)
				{
					FTransform Ti;
					It->GetInstanceTransform(I, Ti, true);
					if (FVector::Dist2D(Ti.GetLocation(), T0.GetLocation()) <= 350.0 && FMath::Abs(Ti.GetLocation().Z - T0.GetLocation().Z) < 50.0
						&& FMath::Abs(FVector::DotProduct(Ti.GetUnitAxis(EAxis::Y), T0.GetUnitAxis(EAxis::Y))) > 0.99)
					{
						T.Box += It->GetStaticMesh()->GetBoundingBox().TransformBy(Ti);
						++Pieces;
					}
				}
				break;
			}
			UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: Curb0 measured over %d pieces of its run"), *GLabel, Pieces);
			B0 = T.Box;
			T.Look = FVector(B0.GetCenter().X, B0.GetCenter().Y, B0.Max.Z - 20.0);
			// On the walkway beside it (across, X) and 3 m back along it (Y), looking at its top.
			const FVector X = T0.GetUnitAxis(EAxis::X).GetSafeNormal2D() * 150.0, Y = T0.GetUnitAxis(EAxis::Y).GetSafeNormal2D() * 300.0;
			if (PickStand(FVector(B0.GetCenter().X, B0.GetCenter().Y, B0.Max.Z - 40.0), { X + Y, X - Y, -X + Y, -X - Y }, T.Stand))
			{
				Out.Add(T);
			}
			else
			{
				UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: no free side 2.2 m from the first curb at %s"), *GLabel, *B0.GetCenter().ToCompactString());
			}
		}
		// A hangar's hatch frame first (where there is one), else a door frame: the one nearest the ship where there is a
		// ship (the hangar's), else the first.
		bool bFrame = false;
		const AActor* ShipNear = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(TEXT("Ship"))) { ShipNear = *It; break; }
		}
		const UStaticMeshComponent* NearestFrame = nullptr;
		if (ShipNear)
		{
			double Best = TNumericLimits<double>::Max();
			for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() == World && It->GetStaticMesh() && !It->IsA<UInstancedStaticMeshComponent>() && It->IsVisible()
					&& It->GetStaticMesh()->GetName() == TEXT("SM_KitDoorFrame"))
				{
					const double D = FVector::Dist(It->Bounds.Origin, ShipNear->GetActorLocation());
					if (D < Best) { Best = D; NearestFrame = *It; }
				}
			}
		}
		static const TCHAR* const WantedFrames[] = { TEXT("SM_HangarHatchFrame"), TEXT("SM_KitDoorFrame") };
		for (int32 Pass = 0; Pass < 2 && !bFrame; ++Pass)
		{
		const FString Wanted = WantedFrames[Pass];
		for (TObjectIterator<UStaticMeshComponent> It; It && !bFrame; ++It)
		{
			const UStaticMesh* Mesh = It->GetStaticMesh();
			if (It->GetWorld() != World || !Mesh || It->IsA<UInstancedStaticMeshComponent>() || !It->IsVisible())
			{
				continue;
			}
			const FString Name = Mesh->GetName();
			if (Name == Wanted && (!NearestFrame || Name != TEXT("SM_KitDoorFrame") || *It == NearestFrame))
			{
				FTarget T;
				T.Name = FString::Printf(TEXT("Frame_%s"), *Name);
				T.Box = It->Bounds.GetBox();
				// Its lintel, the part always in front of whoever looks at the doorway.
				T.Look = FVector(T.Box.GetCenter().X, T.Box.GetCenter().Y, T.Box.Max.Z - 40.0);
				if (PickSide(FVector(T.Box.GetCenter().X, T.Box.GetCenter().Y, T.Box.Min.Z), It->GetComponentTransform().GetUnitAxis(EAxis::X), Name == TEXT("SM_HangarHatchFrame") ? 900.0 : 350.0, T.Stand,
					ShipNear ? ShipNear->GetActorLocation() : FVector::ZeroVector))
				{
					Out.Add(T);
					bFrame = true;
				}
			}
		}
		}
		return Out;
	}

	static AActor* Ship()
	{
		if (UWorld* World = W())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(TEXT("Ship")))
				{
					return *It;
				}
			}
		}
		return nullptr;
	}

	/** The ship check's pose, also the Stopped check's. */
	static FVector GShipStand = FVector::ZeroVector, GShipLook = FVector::ZeroVector;

	static void AddCheckSteps(TArray<FStep>& S);

	static TArray<FStep> BuildCheck()
	{
		TArray<FStep> S;
		// The targets are taken once the area (if any) is built: the command may start with the map.
		S.Add(WaitFor(TEXT("the area ready"), []() { return AAreaGenerator::IsAreaReady(W()); }, []() { return 2.f; }));
		S.Add(Do(TEXT("settle, the report, the targets"), []() { Report(nullptr); AddCheckSteps(GRun.Steps); return 0.5f; }));
		return S;
	}

	static void AddCheckSteps(TArray<FStep>& S)
	{
		const bool bHome = IsHomeMap();
		const FSurfaceVariant Variant = UTheLastShiftSettings::Get()->GetSurfaceVariant(bHome);
		const float Want = TintHue(Variant.Tint);
		for (const FTarget& T : AccentTargets())
		{
			if (T.bNear)
			{
				S.Add(Do(T.Name + TEXT(": from 2 m"), [T, bHome]() { StandAt(T.NearStand, T.Look); Flashlight(!bHome); return 1.2f; }));
				S.Add(Do(T.Name + TEXT(": shot from 2 m"), [T]() { RequestShot(T.Name + TEXT("_2m"), false); return 0.f; }));
				S.Add(WaitFor(T.Name + TEXT(": saved"), []() { return GShot.bReady; }, []() { return 0.2f; }));
			}
			S.Add(Do(T.Name + TEXT(": pose"), [T, bHome]() { StandAt(T.Stand, T.Look); Flashlight(!bHome); return 1.2f; }));
			S.Add(Do(T.Name + TEXT(": shot"), [T]() { RequestShot(T.Name + TEXT("_Accent"), false); return 0.f; }));
			S.Add(WaitFor(T.Name + TEXT(": measured"), []() { return GShot.bReady; }, [T, Want, bHome]()
			{
				FBox2D Rect;
				const bool bSeen = ProjectBox(T.Box, Rect);
				const FHueSample H = SampleHue(Rect, Want);
				const bool bOk = bSeen && H.Painted >= 150 && HueGap(H.Hue, Want) <= 15.f;
				// The likely cause of a failure: the pose (not in view), the flashlight burning the paint white, or the paint.
				const TCHAR* Cause = bOk ? TEXT("") : (!bSeen || H.Pixels < 2000 ? TEXT("; cause: the pose (the piece is not in view)")
					: (H.White * 2 > H.Pixels ? TEXT("; cause: the flashlight burns the paint white")
					: (H.Painted < 150 ? TEXT("; cause: the paint does not show (dark or unsaturated)") : TEXT("; cause: the paint's hue"))));
				const FColor WantC = FLinearColor(UTheLastShiftSettings::Get()->GetSurfaceVariant(bHome).Tint).ToFColor(true);
				Check(bOk, FString::Printf(TEXT("%s: %d painted pixels of %d in its rectangle, mean hue %.1f (value %.2f), %d within 30 degrees of %s's tint hue %.1f (tint on screen (%d, %d, %d)); rectangle's mean colour (%d, %d, %d), %d burnt white%s; camera %s: the structure's accent"),
						*T.Name, H.Painted, H.Pixels, H.Hue, H.Value, H.NearHue, bHome ? TEXT("home") : TEXT("the twin"), Want, WantC.R, WantC.G, WantC.B,
						H.Mean.R, H.Mean.G, H.Mean.B, H.White, Cause, PC() && PC()->PlayerCameraManager ? *PC()->PlayerCameraManager->GetCameraLocation().ToCompactString() : TEXT("-")));
				return 0.3f;
			}));
		}
		if (const AActor* ShipActor = Ship())
		{
			// The ship shows home's variant wherever it stands: with two test hues as the base tints, its hull takes home's.
			// At home a fixed pose on the hangar's floor; elsewhere (the arrival) from the ship's bounds: 6 m off its hull on
			// the side of the door frame nearest it (inside the hangar), looking at the hull's middle.
			const double FloorZ = ShipActor->GetActorLocation().Z;
			FVector Stand = FVector(3150.0, -1750.0, FloorZ), Look = FVector(4330.0, 0.0, 250.0);
			if (!bHome)
			{
				const FBox Hull = ShipActor->GetComponentsBoundingBox();
				FVector Toward = FVector::ZeroVector;
				double Best = TNumericLimits<double>::Max();
				for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
				{
					if (It->GetWorld() == ShipActor->GetWorld() && It->GetStaticMesh() && !It->IsA<UInstancedStaticMeshComponent>()
						&& It->GetStaticMesh()->GetName() == TEXT("SM_KitDoorFrame"))
					{
						const double D = FVector::Dist2D(It->Bounds.Origin, Hull.GetCenter());
						if (D < Best) { Best = D; Toward = It->Bounds.Origin; }
					}
				}
				const FVector Dir = Toward.IsZero() ? FVector(1, 0, 0) : (Toward - Hull.GetCenter()).GetSafeNormal2D();
				const double Reach = FMath::Abs(Dir.X) * Hull.GetExtent().X + FMath::Abs(Dir.Y) * Hull.GetExtent().Y + 600.0;
				Stand = FVector(Hull.GetCenter().X + Dir.X * Reach, Hull.GetCenter().Y + Dir.Y * Reach, Hull.Min.Z);
				Look = FVector(Hull.GetCenter().X, Hull.GetCenter().Y, Hull.Min.Z + 230.0);
				UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: ship hull %s, stood at %s toward the door frame at %s"), *GLabel, *Hull.ToString(), *Stand.ToCompactString(), *Toward.ToCompactString());
			}
			GShipStand = Stand;
			GShipLook = Look;
			S.Add(Do(TEXT("ship: test hues"), [Stand, Look, bHome]()
			{
				FSurfaceLook& L = MutableSettings()->SurfaceLook;
				L.Home.BaseTint = FLinearColor(1.f, 0.05f, 0.05f, 1.f);
				L.Twin.BaseTint = FLinearColor(0.05f, 1.f, 0.05f, 1.f);
				L.Home.PanelVariation = L.Twin.PanelVariation = 0.f;
				ApplyNow(W());
				StandAt(Stand, Look);
				Flashlight(!bHome);
				return 1.2f;
			}));
			S.Add(Do(TEXT("ship: shot"), []() { RequestShot(TEXT("Ship_TestHues"), false); return 0.f; }));
			S.Add(WaitFor(TEXT("ship: measured"), []() { return GShot.bReady; }, []()
			{
				FBox2D Rect;
				const AActor* A = Ship();
				const UStaticMeshComponent* M = A ? A->FindComponentByClass<UStaticMeshComponent>() : nullptr;
				const bool bSeen = M && ProjectBox(M->Bounds.GetBox(), Rect);
				const FHueSample H = SampleHue(Rect, 0.f, 0.35f);
				Check(bSeen && H.Painted >= 500 && H.NearHue >= H.Painted * 8 / 10,
					FString::Printf(TEXT("ship: %d painted pixels in the centre of its rectangle (%d pixels, mean colour (%d, %d, %d)), %d of them in home's test hue (red), mean hue %.1f; camera %s: the ship shows home's variant"),
						H.Painted, H.Pixels, H.Mean.R, H.Mean.G, H.Mean.B, H.NearHue, H.Hue,
						PC() && PC()->PlayerCameraManager ? *PC()->PlayerCameraManager->GetCameraLocation().ToCompactString() : TEXT("-")));
				FSurfaceLook& L = MutableSettings()->SurfaceLook;
				L = GRun.SavedLook;
				ApplyNow(W());
				return 0.5f;
			}));
		}
		// While Stopped nothing of the look shows: before the first scan the screen is black.
		S.Add(Do(TEXT("stop: pose"), [bHome]()
		{
			if (const AActor* A = Ship())
			{
				StandAt(GShipStand, GShipLook);
			}
			Flashlight(!bHome);
			return 1.f;
		}));
		S.Add(Do(TEXT("stop"), []()
		{
			UWorld* World = W();
			if (UTimeStateSubsystem* T = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; T && !T->IsStopped())
			{
				T->Stop();
			}
			return 0.3f;
		}));
		S.Add(Do(TEXT("stopped, no scan: shot"), []() { RequestShot(TEXT("Stopped_NoScan"), false); return 0.f; }));
		S.Add(WaitFor(TEXT("stopped, no scan: measured"), []() { return GShot.bReady; }, []()
		{
			int32 Lit = 0, Max = 0;
			for (const FColor& C : GShot.Pixels)
			{
				const int32 M = FMath::Max3(C.R, C.G, C.B);
				Max = FMath::Max(Max, M);
				Lit += M > 2 ? 1 : 0;
			}
			Check(GShot.Pixels.Num() > 0 && Lit == 0, FString::Printf(TEXT("stopped before the first scan: %d pixels above 2 levels, brightest %d: black"), Lit, Max));
			return 2.0f;
		}));
		S.Add(Do(TEXT("stopped, first scan: shot"), []() { RequestShot(TEXT("Stopped_Scan"), false); return 0.f; }));
		S.Add(WaitFor(TEXT("stopped, first scan: saved"), []() { return GShot.bReady; }, []()
		{
			UWorld* World = W();
			if (UTimeStateSubsystem* T = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr; T && T->IsStopped())
			{
				T->Resume();
			}
			Flashlight(false);
			return 1.f;
		}));
	}

	// --- tls.Look.Flashlight / tls.Look.FlashlightLevels ---

	static FString FlashlightText(const FFlashlightTuning& F)
	{
		return FString::Printf(TEXT("(Intensity=%.3f,bInverseSquaredFalloff=%s,FalloffExponent=%.3f,AttenuationRadius=%.1f,InnerConeAngle=%.1f,OuterConeAngle=%.1f)"),
			F.Intensity, F.bInverseSquaredFalloff ? TEXT("True") : TEXT("False"), F.FalloffExponent, F.AttenuationRadius, F.InnerConeAngle, F.OuterConeAngle);
	}

	static int32 ApplyFlashlightEverywhere()
	{
		int32 N = 0;
		for (TObjectIterator<ATestLidarCharacter> It; It; ++It)
		{
			if (It->GetWorld() && It->GetWorld()->IsGameWorld() && !It->IsTemplate())
			{
				It->ApplyFlashlightTuning();
				++N;
			}
		}
		return N;
	}

	struct FWall
	{
		FVector Hit = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		double Distance = 0.0;
		double FloorZ = 0.0;
		FString What;
	};
	static FWall GWall;

	/** From where the pawn stands, the longest of 8 horizontal lines at eye height to a surface: the wall measured. */
	static bool FindWall()
	{
		APawn* P = Pawn();
		UWorld* World = W();
		if (!P || !World)
		{
			return false;
		}
		FVector Eye;
		FRotator Ignored;
		P->GetActorEyesViewPoint(Eye, Ignored);
		const ACharacter* Ch = Cast<ACharacter>(P);
		const double Half = Ch && Ch->GetCapsuleComponent() ? Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		FCollisionQueryParams Params(TEXT("TlsLookWall"), true, P);
		GWall = FWall();
		for (int32 I = 0; I < 8; ++I)
		{
			const FVector Dir = FRotator(0.0, I * 45.0, 0.0).Vector();
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * 10000.0, ECC_Visibility, Params) && Hit.Distance > GWall.Distance)
			{
				GWall.Hit = Hit.ImpactPoint;
				GWall.Dir = Dir;
				GWall.Distance = Hit.Distance;
				GWall.FloorZ = P->GetActorLocation().Z - Half;
				GWall.What = Hit.GetComponent() ? FString::Printf(TEXT("%s of %s"), *Hit.GetComponent()->GetName(), *GetNameSafe(Hit.GetActor())) : FString(TEXT("-"));
			}
		}
		return GWall.Distance > 0.0;
	}

	static TArray<FStep> BuildFlashlightLevels()
	{
		TArray<FStep> S;
		S.Add(WaitFor(TEXT("the area ready"), []() { return AAreaGenerator::IsAreaReady(W()); }, []() { return 2.f; }));
		S.Add(Do(TEXT("the wall"), []()
		{
			const bool bFound = FindWall();
			UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: FLASHLIGHT %s; the wall: %s at %.1f m (%s)"), *GLabel, *FlashlightText(UTheLastShiftSettings::Get()->GetFlashlightTuning()),
				bFound ? *GWall.Hit.ToCompactString() : TEXT("none"), GWall.Distance / 100.0, *GWall.What);
			for (const double D : { 300.0, 1000.0, 2500.0 })
			{
				if (GWall.Distance < D + 60.0)
				{
					continue;
				}
				const FString Name = FString::Printf(TEXT("FlashlightWall_%dm"), FMath::RoundToInt32(D / 100.0));
				GRun.Steps.Add(Do(Name + TEXT(": pose"), [D]()
				{
					const FVector Stand = GWall.Hit - GWall.Dir * D;
					StandAt(FVector(Stand.X, Stand.Y, GWall.FloorZ), GWall.Hit);
					Flashlight(true);
					return 1.2f;
				}));
				GRun.Steps.Add(Do(Name + TEXT(": shot"), [Name]() { RequestShot(Name, false); return 0.f; }));
				GRun.Steps.Add(WaitFor(Name + TEXT(": measured"), []() { return GShot.bReady; }, [D]()
				{
					// The wall's level: the mean luma of the 120 x 120 pixels at the centre of the view (where the beam is);
					// the burnt share: pixels with a channel at 250 or above, over the whole shot.
					int64 Sum = 0;
					int32 N = 0, Burnt = 0;
					const int32 CX = GShot.Width / 2, CY = GShot.Height / 2;
					for (int32 Y = 0; Y < GShot.Height; ++Y)
					{
						for (int32 X = 0; X < GShot.Width; ++X)
						{
							const FColor& C = GShot.Pixels[Y * GShot.Width + X];
							Burnt += FMath::Max3(C.R, C.G, C.B) >= 250 ? 1 : 0;
							if (FMath::Abs(X - CX) < 60 && FMath::Abs(Y - CY) < 60)
							{
								Sum += FMath::RoundToInt32(0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B);
								++N;
							}
						}
					}
					const float Mean = N > 0 ? static_cast<float>(Sum) / N : 0.f;
					const float Share = GShot.Pixels.Num() > 0 ? 100.f * Burnt / GShot.Pixels.Num() : 0.f;
					UE_LOG(LogTheLastShift, Display, TEXT("Look: %s: FLASHLIGHT wall at %.0f m: centre mean level %.1f, %.2f%% of pixels at 250 or above"), *GLabel, D / 100.0, Mean, Share);
					Check(N > 0, FString::Printf(TEXT("wall at %.0f m measured: centre %.1f, burnt %.2f%%"), D / 100.0, Mean, Share));
					return 0.2f;
				}));
			}
			return 0.f;
		}));
		return S;
	}

	// --- tls.Look.TextShots ---

	static UTextChannelComponent* Channel() { const APawn* P = Pawn(); return P ? P->FindComponentByClass<UTextChannelComponent>() : nullptr; }

	static UTextChannelWidget* ChannelWidget()
	{
		TArray<UUserWidget*> Found;
		if (UWorld* World = W())
		{
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Found, UTextChannelWidget::StaticClass(), false);
		}
		return Found.Num() > 0 ? Cast<UTextChannelWidget>(Found[0]) : nullptr;
	}

	static FBox2D GBoxRect(ForceInit);
	static FBox2D GBarRect(ForceInit);
	static FColor GHomeMeasured(0, 0, 0);

	static FBox2D ToShot(const FBox2D& Abs)
	{
		if (!Abs.bIsValid)
		{
			return Abs;
		}
		if (GEngine && GEngine->GameViewport)
		{
			if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
			{
				return Abs.ShiftBy(-Window->GetPositionInScreen());
			}
		}
		return Abs;
	}

	static void TakeBoxShot(const FString& Name)
	{
		const UTextChannelWidget* Widget = ChannelWidget();
		GBoxRect = ToShot(Widget ? Widget->GetShownRect() : FBox2D(ForceInit));
		GBarRect = FBox2D(ForceInit);
		if (const UBorder* Bar = Widget ? Widget->GetVoiceBar() : nullptr; Bar && Bar->IsVisible())
		{
			const FGeometry& G = Bar->GetCachedGeometry();
			GBarRect = ToShot(FBox2D(G.GetAbsolutePosition(), G.GetAbsolutePosition() + G.GetAbsoluteSize()));
		}
		RequestShot(Name, true);
	}

	static bool InRect(const FBox2D& R, int32 X, int32 Y, int32 Grow) { return R.bIsValid && X >= R.Min.X - Grow && X <= R.Max.X + Grow && Y >= R.Min.Y - Grow && Y <= R.Max.Y + Grow; }

	/** The glyphs' cores: the brightest pixels (92 % of the brightest luma) of the box, the bar left out. */
	static bool Glyphs(FColor& Out, int32& Count)
	{
		Count = 0;
		if (!GBoxRect.bIsValid || GShot.Pixels.Num() < GShot.Width * GShot.Height)
		{
			return false;
		}
		const int32 X0 = FMath::Clamp(FMath::FloorToInt32(GBoxRect.Min.X) + 2, 0, GShot.Width), X1 = FMath::Clamp(FMath::CeilToInt32(GBoxRect.Max.X) - 2, 0, GShot.Width);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(GBoxRect.Min.Y) + 2, 0, GShot.Height), Y1 = FMath::Clamp(FMath::CeilToInt32(GBoxRect.Max.Y) - 2, 0, GShot.Height);
		auto Lum = [](const FColor& C) { return C.R * 2 + C.G * 5 + C.B; };
		int32 MaxLum = 0;
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				if (!InRect(GBarRect, X, Y, 2)) { MaxLum = FMath::Max(MaxLum, Lum(GShot.Pixels[Y * GShot.Width + X])); }
			}
		}
		int64 R = 0, G = 0, B = 0;
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				const FColor& C = GShot.Pixels[Y * GShot.Width + X];
				if (MaxLum > 0 && !InRect(GBarRect, X, Y, 2) && Lum(C) * 100 >= MaxLum * 92)
				{
					R += C.R; G += C.G; B += C.B; ++Count;
				}
			}
		}
		if (Count == 0)
		{
			return false;
		}
		Out = FColor(static_cast<uint8>(R / Count), static_cast<uint8>(G / Count), static_cast<uint8>(B / Count));
		return true;
	}

	static bool BarMean(FColor& Out, int32& Count)
	{
		Count = 0;
		if (!GBarRect.bIsValid)
		{
			return false;
		}
		int64 R = 0, G = 0, B = 0;
		for (int32 Y = FMath::Max(0, FMath::CeilToInt32(GBarRect.Min.Y) + 1); Y < FMath::Min(GShot.Height, FMath::FloorToInt32(GBarRect.Max.Y) - 1); ++Y)
		{
			for (int32 X = FMath::Max(0, FMath::CeilToInt32(GBarRect.Min.X)); X < FMath::Min(GShot.Width, FMath::FloorToInt32(GBarRect.Max.X)); ++X)
			{
				const FColor& C = GShot.Pixels[Y * GShot.Width + X];
				R += C.R; G += C.G; B += C.B; ++Count;
			}
		}
		if (Count == 0)
		{
			return false;
		}
		Out = FColor(static_cast<uint8>(R / Count), static_cast<uint8>(G / Count), static_cast<uint8>(B / Count));
		return true;
	}

	/** The colour a text colour reaches on screen (sRGB): drawn with its alpha over the box's dark backdrop (taken as black). */
	static FColor OnScreen(const FLinearColor& C) { return FLinearColor(C.R * C.A, C.G * C.A, C.B * C.A, 1.f).ToFColor(true); }
	static bool Near(const FColor& A, const FColor& B, int32 Tol) { return FMath::Abs(A.R - B.R) <= Tol && FMath::Abs(A.G - B.G) <= Tol && FMath::Abs(A.B - B.B) <= Tol; }
	static int32 Luma(const FColor& C) { return FMath::RoundToInt32(0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B); }

	static TArray<FStep> BuildTextShots()
	{
		TArray<FStep> S;
		S.Add(Do(TEXT("fixture rows"), []()
		{
			NarrativeFixture::Install(W());
			if (UTextChannelComponent* C = Channel())
			{
				C->ClearAll();
			}
			return 0.5f;
		}));
		struct FVoice { FString Name; FName Row; int32 TwinPart; FLinearColor FSuitTextColors::* Color; };
		const TArray<FVoice> Voices = {
			{ TEXT("HomeAI"), FName(TEXT("A1_AreaStart_1")), 0, &FSuitTextColors::HomeAI },
			{ TEXT("Engineer"), FName(TEXT("A1_EntitySeen_1")), 0, &FSuitTextColors::Engineer },
			{ TEXT("TwinAI"), NAME_None, 1, &FSuitTextColors::TwinAI } };
		for (const FVoice& V : Voices)
		{
			S.Add(Do(V.Name + TEXT(": push"), [V]()
			{
				UTextChannelComponent* C = Channel();
				if (C)
				{
					C->ClearAll();
					FTextChannelContext Context;
					Context.BatteryFraction = 1.0;
					if (V.TwinPart > 0) { C->PushTwinPart(V.TwinPart); }
					else { C->PushRow(V.Row, Context); }
				}
				return 0.2f;
			}));
			S.Add(WaitFor(V.Name + TEXT(": typed out"), []() { const UTextChannelComponent* C = Channel(); return C && C->IsVisible() && C->IsTypedOut(); }, [V]()
			{
				TakeBoxShot(TEXT("Text_") + V.Name);
				return 0.f;
			}));
			S.Add(WaitFor(V.Name + TEXT(": measured"), []() { return GShot.bReady; }, [V]()
			{
				const FLinearColor Want = UTheLastShiftSettings::GetTextColors().*V.Color;
				const FColor Expected = OnScreen(Want);
				FColor Mean(0, 0, 0);
				int32 Count = 0;
				const bool bOk = Glyphs(Mean, Count);
				Check(bOk && Count >= 10 && Near(Mean, Expected, 14),
					FString::Printf(TEXT("%s on the channel: the brightest %d pixels of the glyphs (bar left out) are (%d, %d, %d); its colour of TextColors %s is (%d, %d, %d) on screen; tolerance 14 levels a channel"),
						*V.Name, Count, Mean.R, Mean.G, Mean.B, *Want.ToString(), Expected.R, Expected.G, Expected.B));
				if (V.Name == TEXT("HomeAI"))
				{
					GHomeMeasured = Mean;
				}
				if (V.Name == TEXT("Engineer"))
				{
					FColor Bar(0, 0, 0);
					int32 BarCount = 0;
					const bool bBar = BarMean(Bar, BarCount);
					Check(bBar && BarCount >= 10 && Near(Bar, Expected, 14),
						FString::Printf(TEXT("Engineer's bar: %d pixels, mean (%d, %d, %d): the engineer's colour (%d, %d, %d) within 14 levels"), BarCount, Bar.R, Bar.G, Bar.B, Expected.R, Expected.G, Expected.B));
					const float Gap = HueGap(HSV(Mean).X, HSV(GHomeMeasured).X);
					Check(Gap <= 6.f && Luma(Mean) >= Luma(GHomeMeasured) + 14,
						FString::Printf(TEXT("Engineer's tone: hue %.1f against the home AI's %.1f (%.1f apart, at most 6), luma %d against %d (lighter by at least 14): the same family, lighter"),
							HSV(Mean).X, HSV(GHomeMeasured).X, Gap, Luma(Mean), Luma(GHomeMeasured)));
				}
				return 0.3f;
			}));
		}
		S.Add(Do(TEXT("the game's rows again"), []()
		{
			if (UTextChannelComponent* C = Channel())
			{
				C->ClearAll();
			}
			NarrativeFixture::Restore(W());
			return 0.2f;
		}));
		return S;
	}
}

// The commands sit inside the namespace: a file-scope using directive would reach the next file of a unity build.
namespace LookDebug
{

static FAutoConsoleCommandWithWorld GTlsLookReportCommand(
	TEXT("tls.Look.Report"),
	TEXT("tls.Look.Report: the map's surface variant and the values pushed into MPC_Surface; every mesh with the material of each slot (its family) and its count; each material opaque, and with no emissive, world position offset, pixel depth offset or displacement."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World) { Report(World); }));

static FAutoConsoleCommandWithWorldAndArgs GTlsLookSetCommand(
	TEXT("tls.Look.Set"),
	TEXT("tls.Look.Set <Home|Twin> <Field> <Value...>: sets a value of SurfaceLook for the session and pushes it (Tint R G B, BaseTint R G B, AccentStrength, AccentLevel, Tone, Wear, Dirt, PanelVariation)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 3)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Look.Set <Home|Twin> <Field> <Value...>"));
			return;
		}
		FSurfaceLook& L = MutableSettings()->SurfaceLook;
		const bool bHome = Args[0].Equals(TEXT("Home"), ESearchCase::IgnoreCase);
		TArray<FString> Values(Args.GetData() + 2, Args.Num() - 2);
		if (!SetField(bHome ? L.Home : L.Twin, Args[1], Values))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Look.Set: unknown field %s or missing values."), *Args[1]);
			return;
		}
		const bool bPushed = ApplyNow(TimeStateDebug::ResolveGameWorld(World) ? TimeStateDebug::ResolveGameWorld(World) : World);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Look.Set: %s %s, pushed %d."), bHome ? TEXT("Home") : TEXT("Twin"), *VariantText(bHome ? L.Home : L.Twin), bPushed ? 1 : 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLookFlashlightCommand(
	TEXT("tls.Look.Flashlight"),
	TEXT("tls.Look.Flashlight [<Intensity|InverseSquared|Exponent|Radius|Inner|Outer> <Value>]...: sets the flashlight's values for the session (pairs, any number) and applies them to every pawn; no argument: logs them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		FFlashlightTuning& F = MutableSettings()->Flashlight;
		for (int32 I = 0; I + 1 < Args.Num(); I += 2)
		{
			const FString& K = Args[I];
			const float V = FCString::Atof(*Args[I + 1]);
			if (K.Equals(TEXT("Intensity"), ESearchCase::IgnoreCase)) { F.Intensity = V; }
			else if (K.Equals(TEXT("InverseSquared"), ESearchCase::IgnoreCase)) { F.bInverseSquaredFalloff = V != 0.f; }
			else if (K.Equals(TEXT("Exponent"), ESearchCase::IgnoreCase)) { F.FalloffExponent = V; }
			else if (K.Equals(TEXT("Radius"), ESearchCase::IgnoreCase)) { F.AttenuationRadius = V; }
			else if (K.Equals(TEXT("Inner"), ESearchCase::IgnoreCase)) { F.InnerConeAngle = V; }
			else if (K.Equals(TEXT("Outer"), ESearchCase::IgnoreCase)) { F.OuterConeAngle = V; }
			else { UE_LOG(LogTheLastShift, Warning, TEXT("tls.Look.Flashlight: unknown field %s."), *K); }
		}
		const int32 N = Args.Num() >= 2 ? ApplyFlashlightEverywhere() : 0;
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Look.Flashlight: Flashlight=%s, applied to %d pawns."), *FlashlightText(F), N);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLookFlashlightLevelsCommand(
	TEXT("tls.Look.FlashlightLevels"),
	TEXT("tls.Look.FlashlightLevels [Label] [Quit]: from where the pawn stands, the longest of 8 lines to a wall; the wall shot with the flashlight from 3, 10 and 25 m (those it allows): the centre's mean level and the share of pixels at 250 or above. Shots to Saved/Screenshots/Brief21."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		StartRun(TEXT("tls.Look.FlashlightLevels"), Args.Num() > 0 ? Args[0] : TEXT("Flashlight"), Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0, BuildFlashlightLevels());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLookTextCommand(
	TEXT("tls.Look.Text"),
	TEXT("tls.Look.Text <HomeAI|Engineer|TwinAI|EngineerParadise> R G B [A]: sets a text colour of TextColors for the session (what is shown next)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		if (Args.Num() < 4)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Look.Text <HomeAI|Engineer|TwinAI|EngineerParadise> R G B [A]"));
			return;
		}
		FSuitTextColors& T = MutableSettings()->TextColors;
		FLinearColor* C = Args[0] == TEXT("HomeAI") ? &T.HomeAI : (Args[0] == TEXT("Engineer") ? &T.Engineer : (Args[0] == TEXT("TwinAI") ? &T.TwinAI : (Args[0] == TEXT("EngineerParadise") ? &T.EngineerParadise : nullptr)));
		if (!C)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Look.Text: unknown voice %s."), *Args[0]);
			return;
		}
		*C = FLinearColor(FCString::Atof(*Args[1]), FCString::Atof(*Args[2]), FCString::Atof(*Args[3]), Args.Num() > 4 ? FCString::Atof(*Args[4]) : C->A);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Look.Text: %s = %s."), *Args[0], *ColorText(*C));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLookEngineerToneCommand(
	TEXT("tls.Look.EngineerTone"),
	TEXT("tls.Look.EngineerTone <Share>: the engineer's colour for the session: the home AI's lightened toward white by Share (0 to 1), with the home AI's alpha."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		const float Share = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 0.f, 1.f) : 0.5f;
		FSuitTextColors& T = MutableSettings()->TextColors;
		const FLinearColor H = T.HomeAI;
		T.Engineer = FLinearColor(FMath::Lerp(H.R, 1.f, Share), FMath::Lerp(H.G, 1.f, Share), FMath::Lerp(H.B, 1.f, Share), H.A);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.Look.EngineerTone %.2f: Engineer = %s."), Share, *ColorText(T.Engineer));
	}));

static FAutoConsoleCommand GTlsLookPrintCommand(
	TEXT("tls.Look.Print"),
	TEXT("tls.Look.Print: the ini lines of SurfaceLook and TextColors as they are now."),
	FConsoleCommandDelegate::CreateStatic([]() { Print(); }));

static FAutoConsoleCommandWithWorldAndArgs GTlsLookCheckCommand(
	TEXT("tls.Look.Check"),
	TEXT("tls.Look.Check [Label] [Quit]: the accent of every terminal (2 m), of the first gate frame and curb or door frame (3 m) measured against the map's tint; the ship's variant with test hues; a Stopped frame before the first scan black. Shots to Saved/Screenshots/Brief21."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		StartRun(TEXT("tls.Look.Check"), Args.Num() > 0 ? Args[0] : TEXT("Look"), Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0, BuildCheck());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLookTextShotsCommand(
	TEXT("tls.Look.TextShots"),
	TEXT("tls.Look.TextShots [Label] [Quit]: with fixture rows, a home AI row, an engineer's row and a twin AI part on the channel, each shot typed out and its glyphs measured against TextColors (14 levels), the engineer's bar and tone. Shots to Saved/Screenshots/Brief21."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		StartRun(TEXT("tls.Look.TextShots"), Args.Num() > 0 ? Args[0] : TEXT("Text"), Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0, BuildTextShots());
	}));

} // namespace LookDebug

#endif // !UE_BUILD_SHIPPING

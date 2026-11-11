// SPDX-License-Identifier: MIT

// Debug console commands for readability (Briefs 5b, 2l): the interactables' scan colour (a lidar colour channel
// chosen by the custom depth stencil of their meshes) and the Interact prompt.
// tls.ReadabilityTest [all|scan|colors|prompt|area], tls.Readability.Spawn, tls.Readability.Clear,
// tls.Readability.Tag, tls.Readability.Channels. Development builds only.

#include "Containers/Ticker.h"
#include "Debug/TlsDebugCommon.h"
#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Input/StdInputSubsystem.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "LidarScanComponent.h"
#include "Residue/Residue.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanCueWidget.h"
#include "Scan/ScanDirectorComponent.h"
#include "TextureResource.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/InteractPromptWidget.h"
#include "UI/StdRadialProgress.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

#if !UE_BUILD_SHIPPING

namespace ReadabilityDebug
{
	using TimeStateDebug::ResolveGameWorld;

	// --- Step runner on the core ticker ---

	struct FStep
	{
		FString Label;
		/** Runs the step; returns seconds to wait before the next one (< 0 aborts the run). */
		TFunction<float()> Do;
		/** If set, the step waits until this returns true (or Timeout real seconds pass) before running Do. */
		TFunction<bool()> Until;
		float Timeout = 0.f;
	};

	struct FRun
	{
		FString Name;
		TArray<FStep> Steps;
		int32 Next = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		double WaitUntilReal = 0.0;
		double ConditionStartReal = -1.0;
		bool bRunning = false;
		/** Exit the game when the run ends (packaged -ExecCmds runs). */
		bool bQuit = false;
		/** Take a screenshot with the UI at the moments that show the colour and the prompts. */
		bool bShots = false;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FRun GRun;

	static void Shot(const FString& What)
	{
		UWorld* World = ResolveGameWorld(nullptr);
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		if (GRun.bShots && Controller)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  screenshot: %s"), *GRun.Name, *What);
			// The viewport client handles screenshots, so it goes through the player's console; showui keeps the prompts.
			Controller->ConsoleCommand(TEXT("Shot showui"));
		}
	}

	static void Check(bool bOk, const FString& What)
	{
		(bOk ? GRun.Passed : GRun.Failed) += 1;
		UE_LOG(LogTheLastShift, Log, TEXT("%s   %s  %s"), *GRun.Name, bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
	}

	static void Note(const FString& What)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s   ..  %s"), *GRun.Name, *What);
	}

	static void Finish(const TCHAR* Why);

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
		FStep& Step = GRun.Steps[GRun.Next];
		if (Step.Until)
		{
			if (GRun.ConditionStartReal < 0.0)
			{
				GRun.ConditionStartReal = Now;
			}
			const bool bMet = Step.Until();
			const bool bTimedOut = Now - GRun.ConditionStartReal > Step.Timeout;
			if (!bMet && !bTimedOut)
			{
				return true;
			}
			Check(bMet, FString::Printf(TEXT("%s (waited %.2f s%s)"), *Step.Label, Now - GRun.ConditionStartReal, bTimedOut && !bMet ? TEXT(", TIMEOUT") : TEXT("")));
			GRun.ConditionStartReal = -1.0;
		}
		++GRun.Next;
		const float Delay = Step.Do ? Step.Do() : 0.f;
		if (Delay < 0.f)
		{
			Finish(*FString::Printf(TEXT(", aborted at step '%s'"), *Step.Label));
			return false;
		}
		GRun.WaitUntilReal = Now + Delay;
		return true;
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

	// --- World access ---

	static UWorld* W() { return ResolveGameWorld(nullptr); }

	static APlayerController* PC()
	{
		UWorld* World = W();
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	static APawn* Pawn()
	{
		const APlayerController* Controller = PC();
		return Controller ? Controller->GetPawn() : nullptr;
	}

	static UTimeStateSubsystem* TS()
	{
		UWorld* World = W();
		return World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	}

	static UScanDirectorComponent* Director()
	{
		UWorld* World = W();
		return World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
	}

	static ULidarScanComponent* Lidar()
	{
		const UScanDirectorComponent* D = Director();
		return D ? D->GetLidarScan() : nullptr;
	}

	static UEnergySubsystem* Energy() { return UEnergySubsystem::Get(W()); }

	template <class T>
	static TArray<T*> Actors()
	{
		TArray<T*> Out;
		if (UWorld* World = W())
		{
			for (TActorIterator<T> It(World); It; ++It)
			{
				Out.Add(*It);
			}
		}
		Out.Sort([](const T& A, const T& B) { return A.GetName() < B.GetName(); });
		return Out;
	}

	static UInteractPromptWidget* Prompt()
	{
		UWorld* World = W();
		for (TObjectIterator<UInteractPromptWidget> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsInViewport())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static FString PromptState()
	{
		const UInteractPromptWidget* P = Prompt();
		if (!P)
		{
			return TEXT("<no prompt>");
		}
		return FString::Printf(TEXT("%s \"%s\""), *UEnum::GetValueAsString(P->GetMode()), *P->GetShownText());
	}

	static EInteractPromptMode PromptMode()
	{
		const UInteractPromptWidget* P = Prompt();
		return P ? P->GetMode() : EInteractPromptMode::Hidden;
	}

	static FKey PromptKey()
	{
		const UInteractPromptWidget* P = Prompt();
		return P ? P->GetShownKey() : FKey();
	}

	// --- Actions ---

	static float CapsuleHalfHeight()
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn());
		return Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	}

	/** Puts the pawn at Location (Z = floor + capsule) looking at LookAt. */
	static void PlacePawn(const FVector& FloorLocation, const FVector& LookAt)
	{
		APawn* P = Pawn();
		APlayerController* Controller = PC();
		if (!P)
		{
			return;
		}
		const FVector Location = FloorLocation + FVector(0.f, 0.f, CapsuleHalfHeight() + 2.f);
		P->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		if (const ACharacter* Character = Cast<ACharacter>(P))
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		const FRotator Look = (LookAt - P->GetPawnViewLocation()).Rotation();
		if (Controller)
		{
			Controller->SetControlRotation(Look);
		}
	}

	static void Teleport2D(const FVector& Location)
	{
		if (APawn* P = Pawn())
		{
			P->SetActorLocation(FVector(Location.X, Location.Y, P->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
			if (const ACharacter* Character = Cast<ACharacter>(P))
			{
				Character->GetCharacterMovement()->StopMovementImmediately();
			}
		}
	}

	static void Key(const FKey& K, EInputEvent Event)
	{
		if (APlayerController* Controller = PC())
		{
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(K, Event, Event == IE_Pressed ? 1.f : 0.f));
		}
	}

	/** Stops time and restarts the cumulative stopped count, so the entity stays dormant. */
	static void StopTime()
	{
		if (UTimeStateSubsystem* T = TS())
		{
			if (!T->IsStopped())
			{
				T->Stop();
			}
			T->ResetCumulativeStoppedSeconds();
		}
	}

	static void ResumeTime()
	{
		if (UTimeStateSubsystem* T = TS(); T && T->IsStopped())
		{
			T->Resume(EStopExitReason::Resumed);
		}
	}

	static bool IsStopped()
	{
		const UTimeStateSubsystem* T = TS();
		return T && T->IsStopped();
	}

	static int32 ScanCount()
	{
		const ULidarScanComponent* L = Lidar();
		return L ? L->GetScanCount() : 0;
	}

	/** Captured and injected particles of the slot holding scan Index (-1 when no slot holds it). */
	static bool SlotParticles(int32 Index, int32& OutCaptured, int32& OutInjected)
	{
		OutCaptured = OutInjected = -1;
		const ULidarScanComponent* L = Lidar();
		if (!L)
		{
			return false;
		}
		for (const FLidarScanSlotStats& Stats : L->GetScanSlotStats())
		{
			if (Stats.ScanIndex == Index)
			{
				OutCaptured = Stats.CapturedParticles;
				OutInjected = Stats.InjectedParticles;
				return true;
			}
		}
		return false;
	}

	static int32 TotalParticles()
	{
		const ULidarScanComponent* L = Lidar();
		int32 Total = 0;
		if (L)
		{
			for (const FLidarScanSlotStats& Stats : L->GetScanSlotStats())
			{
				Total += FMath::Max(0, Stats.CapturedParticles) + FMath::Max(0, Stats.InjectedParticles);
			}
		}
		return Total;
	}

	// --- Spawned test scene ---

	struct FScene
	{
		TArray<TWeakObjectPtr<AActor>> Actors;
		/** The mesh of each spawned terminal and door: what shows in the interactable channel. */
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Targets;
		TArray<FString> Labels;
	};
	static FScene GScene;

	static void ClearScene()
	{
		for (const TWeakObjectPtr<AActor>& Actor : GScene.Actors)
		{
			if (AActor* A = Actor.Get())
			{
				A->Destroy();
			}
		}
		GScene = FScene();
	}

	/** The mesh a terminal or a door is shown by (the terminal's body, the door's panel). */
	static UStaticMeshComponent* InteractableMesh(const AActor* Actor)
	{
		return Actor && (Actor->IsA<APowerTerminal>() || Actor->IsA<ADoorDevice>()) ? Actor->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	}

	/** Tags (or untags) the scene's meshes for the interactable channel, as the game does at BeginPlay. */
	static void SetSceneTagged(bool bTagged)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& Target : GScene.Targets)
		{
			if (UStaticMeshComponent* Mesh = Target.Get())
			{
				Mesh->SetRenderCustomDepth(bTagged);
			}
		}
	}

	// --- Channel readback (diagnostics: the newest scan's capture target, depth in R and channel in G) ---

	struct FChannelImage
	{
		TArray<float> Depth;
		TArray<uint8> Channel;
		int32 Width = 0;
		int32 Height = 0;
		FLidarScanContext Context;
		/** The newest scan's target carried channels (RGBA32f) and was found. */
		bool bHasChannels = false;
		/** Raw statistics of the target that was read: min/max of R, G, B, A over all texels. */
		FString Raw;
	};

	/**
	 * Reads the depth of the newest alive scan through the lidar's diagnostics, then the channel of every texel from the
	 * same render target. The target is found through the component's reflected Slots array (FLidarScanSlot::DepthTarget,
	 * a UPROPERTY) at the position of the newest scan in GetScanSlotStats(), which lists the slots in the same order: the
	 * slot itself, not a guess (two scans from the same pose have identical depth). Its R must equal the depth the lidar
	 * returned (checked).
	 */
	static bool ReadNewestChannels(FChannelImage& Out)
	{
		Out = FChannelImage();
		ULidarScanComponent* L = Lidar();
		if (!L || !L->ReadNewestScanDepth(Out.Depth, Out.Width, Out.Height, Out.Context))
		{
			return false;
		}
		Out.Channel.Init(0, Out.Width * Out.Height);

		const TArray<FLidarScanSlotStats> Slots = L->GetScanSlotStats();
		int32 NewestSlot = INDEX_NONE;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (Slots[Index].ScanIndex == Out.Context.ScanIndex)
			{
				NewestSlot = Index;
			}
		}
		TArray<UTextureRenderTarget2D*> Candidates;
		const FArrayProperty* SlotsProperty = FindFProperty<FArrayProperty>(ULidarScanComponent::StaticClass(), TEXT("Slots"));
		const FStructProperty* SlotProperty = SlotsProperty ? CastField<FStructProperty>(SlotsProperty->Inner) : nullptr;
		const FObjectPropertyBase* TargetProperty = SlotProperty ? FindFProperty<FObjectPropertyBase>(SlotProperty->Struct, TEXT("DepthTarget")) : nullptr;
		if (TargetProperty && NewestSlot != INDEX_NONE)
		{
			FScriptArrayHelper Array(SlotsProperty, SlotsProperty->ContainerPtrToValuePtr<void>(L));
			if (Array.IsValidIndex(NewestSlot))
			{
				Candidates.Add(Cast<UTextureRenderTarget2D>(TargetProperty->GetObjectPropertyValue_InContainer(Array.GetRawPtr(NewestSlot))));
			}
		}
		for (UTextureRenderTarget2D* Target : Candidates)
		{
			if (!Target || Target->RenderTargetFormat != RTF_RGBA32f)
			{
				continue;
			}
			FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
			TArray<FLinearColor> Pixels;
			if (!Resource || !Resource->ReadLinearColorPixels(Pixels) || Pixels.Num() < Out.Width * Out.Height)
			{
				continue;
			}
			bool bMatch = true;
			for (int32 Index = 0; Index < Out.Width * Out.Height && bMatch; ++Index)
			{
				bMatch = Pixels[Index].R == Out.Depth[Index];
			}
			if (!bMatch)
			{
				continue;
			}
			for (int32 Index = 0; Index < Out.Width * Out.Height; ++Index)
			{
				Out.Channel[Index] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Pixels[Index].G), 0, 255));
			}
			Out.bHasChannels = true;
			FLinearColor Lo(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX), Hi(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (int32 Index = 0; Index < Out.Width * Out.Height; ++Index)
			{
				const FLinearColor& Px = Pixels[Index];
				Lo = FLinearColor(FMath::Min(Lo.R, Px.R), FMath::Min(Lo.G, Px.G), FMath::Min(Lo.B, Px.B), FMath::Min(Lo.A, Px.A));
				Hi = FLinearColor(FMath::Max(Hi.R, Px.R), FMath::Max(Hi.G, Px.G), FMath::Max(Hi.B, Px.B), FMath::Max(Hi.A, Px.A));
			}
			const FLinearColor& Mid = Pixels[(Out.Height / 2) * Out.Width + Out.Width / 2];
			Out.Raw = FString::Printf(TEXT("min %s, max %s, centre %s"), *Lo.ToString(), *Hi.ToString(), *Mid.ToString());
			break;
		}
		return true;
	}

	/** Texel counts of one scan: on a target mesh's surface (in the expected channel / in another / default), and overall. */
	struct FChannelCounts
	{
		int32 Valid = 0;
		int32 InChannel = 0;
		int32 OnTarget = 0;
		int32 OnTargetInChannel = 0;
		int32 OnTargetDefault = 0;
		int32 InChannelOffTarget = 0;
		FString Detail;
	};

	/**
	 * Classifies every texel of Image with a depth in (1, MaxRange): its point (texel-centre ray, no SurfaceBias) lies on
	 * a target when it is inside the target mesh's box grown by ToleranceCm. Channel = the expected stencil value.
	 */
	static FChannelCounts CountChannels(const FChannelImage& Image, const TArray<UStaticMeshComponent*>& Targets, const TArray<FString>& Labels, int32 Channel, float ToleranceCm = 3.f)
	{
		FChannelCounts C;
		const FLidarScanContext& X = Image.Context;
		TArray<FIntVector> PerTarget;
		PerTarget.Init(FIntVector::ZeroValue, Targets.Num());
		for (int32 Y = 0; Y < Image.Height; ++Y)
		{
			for (int32 Xi = 0; Xi < Image.Width; ++Xi)
			{
				const int32 Index = Y * Image.Width + Xi;
				const float Depth = Image.Depth[Index];
				if (Depth <= 1.f || Depth >= X.MaxRange)
				{
					continue;
				}
				++C.Valid;
				const uint8 Ch = Image.Channel[Index];
				const bool bIn = Ch == Channel;
				C.InChannel += bIn ? 1 : 0;
				const double U = ((Xi + 0.5) / Image.Width) * 2.0 - 1.0;
				const double V = 1.0 - ((Y + 0.5) / Image.Height) * 2.0;
				const FVector P = X.CaptureOrigin + (X.CaptureForward + U * X.TanHalfFOV.X * X.CaptureRight + V * X.TanHalfFOV.Y * X.CaptureUp) * Depth;
				bool bOnAny = false;
				for (int32 T = 0; T < Targets.Num(); ++T)
				{
					const UStaticMeshComponent* Mesh = Targets[T];
					if (!Mesh || !Mesh->GetStaticMesh())
					{
						continue;
					}
					const FTransform& Xf = Mesh->GetComponentTransform();
					const FVector Scale = Xf.GetScale3D().GetAbs();
					const FVector Grow(ToleranceCm / FMath::Max(Scale.X, 0.001), ToleranceCm / FMath::Max(Scale.Y, 0.001), ToleranceCm / FMath::Max(Scale.Z, 0.001));
					const FBox Box = Mesh->GetStaticMesh()->GetBoundingBox().ExpandBy(Grow, Grow);
					if (Box.IsInside(Xf.InverseTransformPosition(P)))
					{
						bOnAny = true;
						PerTarget[T].X += 1;
						PerTarget[T].Y += bIn ? 1 : 0;
						PerTarget[T].Z += Ch == 0 ? 1 : 0;
					}
				}
				if (bOnAny)
				{
					++C.OnTarget;
					C.OnTargetInChannel += bIn ? 1 : 0;
					C.OnTargetDefault += Ch == 0 ? 1 : 0;
				}
				else
				{
					C.InChannelOffTarget += bIn ? 1 : 0;
				}
			}
		}
		for (int32 T = 0; T < Targets.Num(); ++T)
		{
			C.Detail += FString::Printf(TEXT("%s%s: %d texels, %d in channel, %d default"), C.Detail.IsEmpty() ? TEXT("") : TEXT("; "),
				Labels.IsValidIndex(T) ? *Labels[T] : TEXT("?"), PerTarget[T].X, PerTarget[T].Y, PerTarget[T].Z);
		}
		return C;
	}

	static TArray<UStaticMeshComponent*> SceneTargets()
	{
		TArray<UStaticMeshComponent*> Out;
		for (const TWeakObjectPtr<UStaticMeshComponent>& T : GScene.Targets)
		{
			Out.Add(T.Get());
		}
		return Out;
	}

	/** Per target: texels on it and in the channel, from the last CountChannels call's detail order. */
	static TArray<FIntVector> PerTargetCounts(const FChannelImage& Image, const TArray<UStaticMeshComponent*>& Targets, int32 Channel)
	{
		TArray<FIntVector> Out;
		for (UStaticMeshComponent* T : Targets)
		{
			const FChannelCounts C = CountChannels(Image, { T }, { TEXT("") }, Channel);
			Out.Add(FIntVector(C.OnTarget, C.OnTargetInChannel, C.OnTargetDefault));
		}
		return Out;
	}

	/** Floor point under (X, Y) near Z, or Z - capsule if none is found. */
	static FVector FloorAt(const FVector& Above)
	{
		UWorld* World = W();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ReadabilityFloor), false, Pawn());
		if (World && World->LineTraceSingleByChannel(Hit, Above + FVector(0.f, 0.f, 100.f), Above - FVector(0.f, 0.f, 600.f), ECC_Visibility, Params))
		{
			return Hit.ImpactPoint;
		}
		return Above - FVector(0.f, 0.f, CapsuleHalfHeight());
	}

	/** Free distance along Dir from the pawn's eye (max MaxDist). */
	static float FreeDistance(const FVector& Dir, float MaxDist)
	{
		UWorld* World = W();
		const APawn* P = Pawn();
		if (!World || !P)
		{
			return 0.f;
		}
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ReadabilityFree), false, P);
		const FVector Eye = P->GetPawnViewLocation();
		return World->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * MaxDist, ECC_Visibility, Params) ? Hit.Distance : MaxDist;
	}

	/** Turns the pawn (yaw only) towards the direction with the longest free line of sight; returns that distance. */
	static float FaceLongestClearDirection()
	{
		float Best = -1.f;
		float BestYaw = 0.f;
		for (int32 I = 0; I < 24; ++I)
		{
			const float Yaw = I * 15.f;
			const float Free = FreeDistance(FRotator(0.f, Yaw, 0.f).Vector(), 4000.f);
			if (Free > Best)
			{
				Best = Free;
				BestYaw = Yaw;
			}
		}
		if (APlayerController* Controller = PC())
		{
			Controller->SetControlRotation(FRotator(0.f, BestYaw, 0.f));
		}
		return Best;
	}

	template <class T>
	static T* SpawnFacingPawn(TSubclassOf<T> Class, const FVector& FloorLocation, const FString& Label)
	{
		UWorld* World = W();
		const APawn* P = Pawn();
		if (!World || !P || !Class)
		{
			return nullptr;
		}
		FVector ToPawn = P->GetActorLocation() - FloorLocation;
		ToPawn.Z = 0.f;
		const FRotator Rotation(0.f, ToPawn.Rotation().Yaw, 0.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		T* Actor = World->SpawnActor<T>(Class, FloorLocation, Rotation, Params);
		if (Actor)
		{
			GScene.Actors.Add(Actor);
			if (UStaticMeshComponent* Mesh = InteractableMesh(Actor))
			{
				GScene.Targets.Add(Mesh);
				GScene.Labels.Add(Label);
			}
		}
		return Actor;
	}

	/** Spawns a terminal and a closed door at each distance along the pawn's view, offset to each side. */
	static void SpawnSet(const TArray<float>& Distances)
	{
		const APawn* P = Pawn();
		const APlayerController* Controller = PC();
		if (!P || !Controller)
		{
			return;
		}
		const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Forward = Yaw.Vector();
		const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
		const FVector Base = P->GetActorLocation();
		for (const float Distance : Distances)
		{
			// The terminal to the left, the door (4 m wide) to the right, both inside the view at both distances.
			const float Side = Distance < 1000.f ? 1.f : 2.f;
			const FVector TerminalAt = FloorAt(Base + Forward * Distance - Right * (80.f * Side));
			const FVector DoorAt = FloorAt(Base + Forward * Distance + Right * (260.f * Side));
			SpawnFacingPawn<APowerTerminal>(APowerTerminal::StaticClass(), TerminalAt, FString::Printf(TEXT("terminal %.0f m"), Distance / 100.f));
			SpawnFacingPawn<ADoorDevice>(ADoorDevice::StaticClass(), DoorAt, FString::Printf(TEXT("door %.0f m"), Distance / 100.f));
		}
	}

	/** A 30 cm thick, 3 m by 3 m wall at Distance along the view, and a terminal 2 m behind it. */
	static void SpawnOccluded(float Distance)
	{
		UWorld* World = W();
		const APawn* P = Pawn();
		const APlayerController* Controller = PC();
		if (!World || !P || !Controller)
		{
			return;
		}
		const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Forward = Yaw.Vector();
		const FVector WallFloor = FloorAt(P->GetActorLocation() + Forward * Distance);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WallFloor + FVector(0.f, 0.f, 150.f), Yaw, Params);
		if (Wall)
		{
			Wall->SetMobility(EComponentMobility::Movable);
			Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Wall->SetActorScale3D(FVector(0.3f, 3.f, 3.f));
			GScene.Actors.Add(Wall);
		}
		SpawnFacingPawn<APowerTerminal>(APowerTerminal::StaticClass(), FloorAt(P->GetActorLocation() + Forward * (Distance + 200.f)), TEXT("terminal behind wall"));
	}

	static AStaticMeshActor* SpawnBlock(const FVector& Center, const FRotator& Rotation, const FVector& SizeCm)
	{
		UWorld* World = W();
		if (!World)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Center, Rotation, Params);
		if (Block)
		{
			Block->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Block->SetActorScale3D(SizeCm / 100.f);
			GScene.Actors.Add(Block);
		}
		return Block;
	}

	/**
	 * A test lane outside the TestLidar room (which is only 16 m across): a 40 m by 16 m floor from Origin along +X and a
	 * 4 m wall across it 34 m from the start. Puts the pawn at the start looking along the lane.
	 */
	static void BuildLane(const FVector& Origin)
	{
		SpawnBlock(Origin + FVector(2000.f, 0.f, -10.f), FRotator::ZeroRotator, FVector(4000.f, 1600.f, 20.f));
		SpawnBlock(Origin + FVector(3400.f, 0.f, 200.f), FRotator::ZeroRotator, FVector(30.f, 1600.f, 400.f));
		PlacePawn(Origin + FVector(100.f, 0.f, 0.f), Origin + FVector(3000.f, 0.f, 154.f));
		if (APlayerController* Controller = PC())
		{
			Controller->SetControlRotation(FRotator::ZeroRotator);
		}
	}

	/** Lane origin: well outside the TestLidar room, any other map uses the longest clear line from the pawn instead. */
	static const FVector LaneOrigin(4000.f, 0.f, 0.f);

	static bool IsTestLidar()
	{
		const UWorld* World = W();
		return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()).EndsWith(TEXT("/TestLidar"));
	}

	/** In TestLidar, builds the lane and stands on it; elsewhere faces the longest clear line. */
	static void PrepareLine()
	{
		if (IsTestLidar())
		{
			BuildLane(LaneOrigin);
			Note(TEXT("lane: 40 m test lane built outside the TestLidar room, pawn at its start"));
		}
		else
		{
			const float Free = FaceLongestClearDirection();
			Note(FString::Printf(TEXT("clear line of sight %.1f m"), Free / 100.f));
		}
	}

	// --- Shared step blocks ---

	/**
	 * Stop, automatic scan A with the scene's meshes tagged for the interactable channel, then scan B (after the
	 * cooldown) with them untagged, from the same pose. Each scan's channel image is read back right after it.
	 */
	struct FAB
	{
		int32 ScanA = -1;
		int32 ScanB = -1;
		int32 InjectedA = -1;
		int32 InjectedB = -1;
		int32 CapturedA = -1;
		int32 CapturedB = -1;
		FChannelImage ImageA;
		FChannelImage ImageB;
		FChannelCounts CountsA;
		FChannelCounts CountsB;
	};
	static FAB GAB;

	static void AddScanAB(TArray<FStep>& S, const FString& What)
	{
		S.Add(Do(What + TEXT(": stop time"), []()
		{
			GAB = FAB();
			SetSceneTagged(true);
			GAB.ScanA = ScanCount();
			StopTime();
			return 0.f;
		}));
		S.Add(WaitFor(What + TEXT(": automatic scan A completed"), []() { return ScanCount() > GAB.ScanA; }, 3.f, []() { return 0.5f; }));
		S.Add(Do(What + TEXT(": read scan A, untag"), [What]()
		{
			SlotParticles(GAB.ScanA, GAB.CapturedA, GAB.InjectedA);
			const FLidarInjectionStats Stats = Lidar() ? Lidar()->GetLastInjectionStats() : FLidarInjectionStats();
			ReadNewestChannels(GAB.ImageA);
			const int32 Channel = UTheLastShiftSettings::GetInteractableScanChannel();
			GAB.CountsA = CountChannels(GAB.ImageA, SceneTargets(), GScene.Labels, Channel);
			Note(FString::Printf(TEXT("%s: scan A %d (tagged): captured %d, injected %d (%d sources, %d gathered, %d uploaded, %d over cap); texels: %d with depth, %d in channel %d (%d off the targets); %s"),
				*What, GAB.ScanA, GAB.CapturedA, GAB.InjectedA, Stats.Sources, Stats.Gathered, Stats.Uploaded, Stats.OverCap,
				GAB.CountsA.Valid, GAB.CountsA.InChannel, Channel, GAB.CountsA.InChannelOffTarget, *GAB.CountsA.Detail));
			Check(GAB.ImageA.bHasChannels, FString::Printf(TEXT("%s: scan A carries channels (its target was read back)"), *What));
			Check(Stats.OverCap == 0, FString::Printf(TEXT("%s: no injected point over the lidar's cap"), *What));
			Shot(What + TEXT(" scan A"));
			SetSceneTagged(false);
			return 0.f;
		}));
		S.Add(WaitFor(What + TEXT(": cooldown over"), []() { const UScanDirectorComponent* D = Director(); return D && D->IsScanReady(); }, 6.f, []()
		{
			GAB.ScanB = ScanCount();
			Director()->RequestScan();
			return 0.f;
		}));
		S.Add(WaitFor(What + TEXT(": scan B completed"), []() { return ScanCount() > GAB.ScanB; }, 3.f, []() { return 0.5f; }));
		S.Add(Do(What + TEXT(": read scan B"), [What]()
		{
			SlotParticles(GAB.ScanB, GAB.CapturedB, GAB.InjectedB);
			ReadNewestChannels(GAB.ImageB);
			GAB.CountsB = CountChannels(GAB.ImageB, SceneTargets(), GScene.Labels, UTheLastShiftSettings::GetInteractableScanChannel());
			int32 CapturedAgain = 0, InjectedAgain = 0;
			SlotParticles(GAB.ScanA, CapturedAgain, InjectedAgain);
			Note(FString::Printf(TEXT("%s: scan B %d (untagged): captured %d, injected %d; texels: %d with depth, %d in channel; scan A now: captured %d, injected %d"),
				*What, GAB.ScanB, GAB.CapturedB, GAB.InjectedB, GAB.CountsB.Valid, GAB.CountsB.InChannel, CapturedAgain, InjectedAgain));
			// Only the scene's own meshes are untagged: other interactables in view (a generated area's) keep their channel.
			Check(GAB.CountsB.OnTargetInChannel == 0 && GAB.CountsB.InChannel == GAB.CountsA.InChannelOffTarget,
				FString::Printf(TEXT("%s: untagged, none of their texels is in the channel (%d on them; %d elsewhere, as tagged: %d)"),
					*What, GAB.CountsB.OnTargetInChannel, GAB.CountsB.InChannel, GAB.CountsA.InChannelOffTarget));
			Check(GAB.CountsA.Valid == GAB.CountsB.Valid, FString::Printf(TEXT("%s: tagging changes no depth sample (%d / %d texels with depth)"), *What, GAB.CountsA.Valid, GAB.CountsB.Valid));
			Check(GAB.InjectedA == GAB.InjectedB, FString::Printf(TEXT("%s: the interactables add no injected point (injected %d tagged / %d untagged)"), *What, GAB.InjectedA, GAB.InjectedB));
			Check(InjectedAgain == GAB.InjectedA && CapturedAgain == GAB.CapturedA,
				FString::Printf(TEXT("%s: scan A keeps its points while it is alive (captured %d -> %d)"), *What, GAB.CapturedA, CapturedAgain));
			SetSceneTagged(true);
			return 0.f;
		}));
	}

	static void AddResumeAndCleared(TArray<FStep>& S, const FString& What)
	{
		S.Add(Do(What + TEXT(": resume"), []() { ResumeTime(); return 0.f; }));
		S.Add(WaitFor(What + TEXT(": every point gone with its scan after Resume"), []() { return TotalParticles() == 0; }, 2.f));
	}

	// --- Phases ---

	static void AddScanPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("scan: face the longest clear line and spawn a terminal and a closed door at 5 m and 30 m"), []()
		{
			ResumeTime();
			ClearScene();
			PrepareLine();
			SpawnSet({ 500.f, 3000.f });
			return 0.5f;
		}));
		AddScanAB(S, TEXT("scan"));
		S.Add(Do(TEXT("scan: check"), []()
		{
			const int32 Channel = UTheLastShiftSettings::GetInteractableScanChannel();
			const TArray<FIntVector> Per = PerTargetCounts(GAB.ImageA, SceneTargets(), Channel);
			for (int32 T = 0; T < Per.Num(); ++T)
			{
				Check(Per[T].Y > 0 && Per[T].Y * 10 >= Per[T].X * 9, FString::Printf(TEXT("scan: %s reads in channel %d: %d of its %d texels (%d default: its outline)"),
					*GScene.Labels[T], Channel, Per[T].Y, Per[T].X, Per[T].Z));
			}
			Check(GAB.CountsA.InChannelOffTarget * 100 <= FMath::Max(GAB.CountsA.InChannel, 1) * 2,
				FString::Printf(TEXT("scan: channel texels lie on the targets (%d of %d off them, at most 2 %%: edges)"), GAB.CountsA.InChannelOffTarget, GAB.CountsA.InChannel));
			return 0.f;
		}));
		AddResumeAndCleared(S, TEXT("scan"));

		S.Add(Do(TEXT("occlusion: a terminal 2 m behind a wall at 8 m"), []()
		{
			ClearScene();
			PrepareLine();
			SpawnOccluded(800.f);
			return 0.3f;
		}));
		AddScanAB(S, TEXT("occlusion"));
		S.Add(Do(TEXT("occlusion: check"), []()
		{
			const ULidarScanComponent* L = Lidar();
			const AActor* Hidden = GScene.Targets.Num() > 0 && GScene.Targets[0].IsValid() ? GScene.Targets[0]->GetOwner() : nullptr;
			double Depth = 0.0;
			const bool bInCoverage = Hidden && L && L->GetAliveScans().Num() > 0 && L->GetAliveScans()[0].IsInCoverage(Hidden->GetActorLocation() + FVector(0.f, 0.f, 60.f), Depth);
			Check(bInCoverage, TEXT("occlusion: the hidden terminal is inside the capture's coverage"));
			Check(GAB.CountsA.InChannel == 0, FString::Printf(TEXT("occlusion: it adds no channel texel (%d)"), GAB.CountsA.InChannel));
			return 0.f;
		}));
		AddResumeAndCleared(S, TEXT("occlusion"));
		S.Add(Do(TEXT("scan: clear the test scene"), []() { ClearScene(); return 0.f; }));
	}

	static FScanColors GSavedColors;
	static int32 GSavedChannel = 1;

	static bool SameColor(const FLinearColor& A, const FLinearColor& B) { return A.Equals(B, 1.e-4f); }

	static void AddColorsPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("colors: the ini values"), []()
		{
			ResumeTime();
			const FScanColors& C = UTheLastShiftSettings::GetScanColors();
			GSavedColors = C;
			Note(FString::Printf(TEXT("colors: scan %s, ready cue %s, residue %s, entity %s, interactable %s"), *C.Scan.ToString(), *C.ReadyCue.ToString(),
				*C.Residue.ToString(), *C.Entity.ToString(), *C.Interactable.ToString()));
			Check(SameColor(C.Scan, FLinearColor(0.4f, 0.85f, 1.f, 1.f)) && SameColor(C.ReadyCue, FLinearColor(1.f, 1.f, 1.f, 0.9f))
				&& SameColor(C.Residue, FLinearColor(0.75f, 0.25f, 1.f, 1.f)) && SameColor(C.Entity, FLinearColor(1.f, 0.16f, 0.05f, 1.f)),
				TEXT("colors: scan, ready cue, residue and entity carry the values from before Brief 5b"));

			// As if each colour had been edited in the ini (tls.Settings.Reload writes the same object).
			FScanColors& M = GetMutableDefault<UTheLastShiftSettings>()->ScanColors;
			M.Scan = FLinearColor(0.9f, 0.9f, 0.2f, 1.f);
			M.ReadyCue = FLinearColor(0.2f, 0.4f, 1.f, 0.8f);
			M.Residue = FLinearColor(1.f, 0.5f, 0.f, 1.f);
			M.Entity = FLinearColor(0.f, 1.f, 1.f, 1.f);
			M.Interactable = FLinearColor(1.f, 0.f, 1.f, 1.f);
			// And the interactables moved to another channel: the meshes spawned below read it at BeginPlay.
			GSavedChannel = GetDefault<UTheLastShiftSettings>()->InteractableScanChannel;
			GetMutableDefault<UTheLastShiftSettings>()->InteractableScanChannel = GSavedChannel == 2 ? 3 : 2;
			ClearScene();
			PrepareLine(); // the scan below reads them back: they need a floor and a clear view
			SpawnSet({ 500.f });
			return 0.3f;
		}));
		S.Add(Do(TEXT("colors: stop"), []() { GAB = FAB(); GAB.ScanA = ScanCount(); StopTime(); return 0.f; }));
		S.Add(WaitFor(TEXT("colors: automatic scan completed"), []() { return ScanCount() > GAB.ScanA; }, 3.f, []() { return 0.2f; }));
		S.Add(WaitFor(TEXT("colors: scan cue ready"), []() { const UScanDirectorComponent* D = Director(); return D && D->IsScanReady(); }, 6.f, []() { return 0.1f; }));
		S.Add(Do(TEXT("colors: check every reader"), []()
		{
			const FScanColors& C = UTheLastShiftSettings::GetScanColors();
			const ULidarScanComponent* L = Lidar();
			Check(L && SameColor(L->PointColor, C.Scan), FString::Printf(TEXT("colors: the lidar draws captured points in the new scan colour (%s)"), L ? *L->PointColor.ToString() : TEXT("-")));

			int32 Residues = 0, ResiduesOk = 0;
			for (AResidue* R : Actors<AResidue>())
			{
				++Residues;
				ResiduesOk += SameColor(ILidarPointSource::Execute_GetLidarSourceSettings(R).Color, C.Residue) ? 1 : 0;
			}
			Check(Residues == ResiduesOk, FString::Printf(TEXT("colors: %d of %d residues use the new residue colour"), ResiduesOk, Residues));
			int32 Entities = 0, EntitiesOk = 0;
			for (AEntity* E : Actors<AEntity>())
			{
				++Entities;
				EntitiesOk += SameColor(ILidarPointSource::Execute_GetLidarSourceSettings(E).Color, C.Entity) ? 1 : 0;
			}
			Check(Entities == EntitiesOk, FString::Printf(TEXT("colors: %d of %d entities use the new entity colour"), EntitiesOk, Entities));
			const int32 Channel = UTheLastShiftSettings::GetInteractableScanChannel();
			Check(L && L->AreChannelsActive() && SameColor(L->GetChannel(Channel).Color, C.Interactable),
				FString::Printf(TEXT("colors: the lidar's channel %d (the moved InteractableScanChannel) has the new interactable colour (%s)"), Channel, L ? *L->GetChannel(Channel).Color.ToString() : TEXT("-")));
			int32 Meshes = 0, MeshesOk = 0;
			for (const TWeakObjectPtr<UStaticMeshComponent>& T : GScene.Targets)
			{
				if (const UStaticMeshComponent* Mesh = T.Get())
				{
					++Meshes;
					MeshesOk += Mesh->bRenderCustomDepth && Mesh->CustomDepthStencilValue == Channel ? 1 : 0;
				}
			}
			Check(Meshes > 0 && Meshes == MeshesOk, FString::Printf(TEXT("colors: %d of %d interactable meshes carry stencil value %d"), MeshesOk, Meshes, Channel));
			FChannelImage Image;
			ReadNewestChannels(Image);
			const FChannelCounts Counts = CountChannels(Image, SceneTargets(), GScene.Labels, Channel);
			Check(Counts.OnTargetInChannel > 0 && Counts.InChannelOffTarget * 50 <= FMath::Max(Counts.InChannel, 1),
				FString::Printf(TEXT("colors: the scan shows them in channel %d (%s)"), Channel, *Counts.Detail));

			bool bCueOk = false;
			FString CueColor = TEXT("-");
			for (TObjectIterator<UScanCueWidget> It; It; ++It)
			{
				if (It->GetWorld() == W() && It->WidgetTree)
				{
					if (const UStdRadialProgress* Ring = Cast<UStdRadialProgress>(It->WidgetTree->FindWidget(TEXT("Ring"))))
					{
						CueColor = Ring->ReadyColor.ToString();
						bCueOk = SameColor(Ring->ReadyColor, C.ReadyCue) && Ring->IsFull();
					}
				}
			}
			Check(bCueOk, FString::Printf(TEXT("colors: the full scan cue ring shows the new ready colour (%s)"), *CueColor));
			Check(SameColor(AResidue::GetResidueColor(), C.Residue), TEXT("colors: the collect ring reads the new residue colour"));
			return 0.f;
		}));
		S.Add(Do(TEXT("colors: restore the ini values"), []()
		{
			GetMutableDefault<UTheLastShiftSettings>()->ScanColors = GSavedColors;
			GetMutableDefault<UTheLastShiftSettings>()->InteractableScanChannel = GSavedChannel;
			ResumeTime();
			ClearScene();
			return 0.6f;
		}));
	}

	// --- Prompt phase ---

	static TWeakObjectPtr<AResidue> GResidue;
	static TWeakObjectPtr<APowerTerminal> GTerminal;

	static AResidue* AvailableResidue(const AResidue* Except = nullptr)
	{
		for (AResidue* R : Actors<AResidue>())
		{
			if (R != Except && R->GetState() == EResidueState::Available)
			{
				return R;
			}
		}
		return nullptr;
	}

	static APowerTerminal* TerminalWithTargets()
	{
		for (APowerTerminal* T : Actors<APowerTerminal>())
		{
			if (T->Targets.Num() > 0)
			{
				return T;
			}
		}
		return nullptr;
	}

	static void PowerOffTargets(APowerTerminal* T)
	{
		if (T)
		{
			for (APoweredDevice* D : T->Targets)
			{
				if (D)
				{
					D->PowerOff();
				}
			}
		}
	}

	static bool AnyTargetPowered(const APowerTerminal* T)
	{
		if (T)
		{
			for (const APoweredDevice* D : T->Targets)
			{
				if (D && D->IsPowered())
				{
					return true;
				}
			}
		}
		return false;
	}

	/** In front of a terminal (its +X), within reach, looking at it. */
	static void GoToTerminal(const APowerTerminal* T)
	{
		if (T)
		{
			PlacePawn(T->GetActorLocation() + T->GetActorForwardVector() * 90.f, T->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
	}

	static void GoNextToResidue(const AResidue* R)
	{
		if (R)
		{
			PlacePawn(FloorAt(R->GetActorLocation() - FVector(70.f, 0.f, 0.f)), R->GetActorLocation());
		}
	}

	static FString KeyName(const FKey& K) { return K.IsValid() ? K.GetDisplayName(false).ToString() : TEXT("<none>"); }

	static void AddPromptPhase(TArray<FStep>& S, const FKey& InteractKey, const FString& Tag)
	{
		const FString P = TEXT("prompt") + Tag;
		S.Add(Do(P + TEXT(": next to a residue in Normal time"), []()
		{
			ResumeTime();
			GResidue = AvailableResidue();
			GoNextToResidue(GResidue.Get());
			return 0.3f;
		}));
		S.Add(Do(P + TEXT(": check Normal"), [P]()
		{
			Check(GResidue.IsValid(), P + TEXT(": the map has an Available residue"));
			Check(PromptMode() != EInteractPromptMode::Collect, FString::Printf(TEXT("%s: no collect prompt in Normal time (%s)"), *P, *PromptState()));
			StopTime();
			return 0.4f;
		}));
		S.Add(Do(P + TEXT(": check Stopped"), [P, InteractKey]()
		{
			Check(PromptMode() == EInteractPromptMode::Collect && PromptKey() == InteractKey,
				FString::Printf(TEXT("%s: Stopped within reach, before any scan matters: %s, key %s"), *P, *PromptState(), *KeyName(InteractKey)));
			Check(Prompt() && Prompt()->GetShownText().StartsWith(TEXT("Hold [")) && Prompt()->GetShownText().EndsWith(TEXT("] Collect")),
				P + TEXT(": the labels are Hold and Collect"));
			Shot(P + TEXT(" collect prompt"));
			return GRun.bShots ? 0.3f : 0.f;
		}));
		S.Add(Do(P + TEXT(": hold Interact"), [InteractKey]()
		{
			Key(InteractKey, IE_Pressed);
			return 0.4f;
		}));
		S.Add(Do(P + TEXT(": check holding"), [P, InteractKey]()
		{
			Check(GResidue.IsValid() && GResidue->IsCollecting(), P + TEXT(": the hold started"));
			Check(PromptMode() == EInteractPromptMode::Hidden, FString::Printf(TEXT("%s: hidden while the ring fills (%s)"), *P, *PromptState()));
			Key(InteractKey, IE_Released);
			return 0.2f;
		}));
		S.Add(Do(P + TEXT(": check released"), [P, InteractKey]()
		{
			Check(PromptMode() == EInteractPromptMode::Collect, FString::Printf(TEXT("%s: back after an unfinished hold (%s)"), *P, *PromptState()));
			Key(InteractKey, IE_Pressed);
			const AResidue* R = GResidue.Get();
			return R ? R->CollectSeconds + 0.4f : 3.f;
		}));
		S.Add(Do(P + TEXT(": check taken"), [P, InteractKey]()
		{
			Key(InteractKey, IE_Released);
			Check(GResidue.IsValid() && GResidue->GetState() == EResidueState::Collected, P + TEXT(": the residue was collected"));
			const AResidue* Other = AResidue::FindCollectable(Pawn());
			Check(Other != nullptr || PromptMode() == EInteractPromptMode::Hidden, FString::Printf(TEXT("%s: hidden once the residue is taken (%s)"), *P, *PromptState()));
			// 4 m from any residue.
			if (const APawn* Pw = Pawn())
			{
				Teleport2D(Pw->GetActorLocation() + FVector(0.f, 400.f, 0.f));
			}
			return 0.3f;
		}));
		S.Add(Do(P + TEXT(": check out of reach"), [P]()
		{
			const AResidue* Near = AResidue::FindCollectable(Pawn());
			Check(Near != nullptr || PromptMode() == EInteractPromptMode::Hidden, FString::Printf(TEXT("%s: hidden out of reach (%s)"), *P, *PromptState()));
			ResumeTime();
			return 0.6f;
		}));

		// Terminal.
		S.Add(Do(P + TEXT(": at a terminal with 2 banked energy"), [P]()
		{
			GTerminal = TerminalWithTargets();
			PowerOffTargets(GTerminal.Get());
			if (UEnergySubsystem* E = Energy())
			{
				E->RestoreBankedEnergy(2);
			}
			GoToTerminal(GTerminal.Get());
			return 0.3f;
		}));
		S.Add(Do(P + TEXT(": check Power"), [P, InteractKey]()
		{
			const APowerTerminal* T = GTerminal.Get();
			Check(T != nullptr, P + TEXT(": the map has a terminal with targets"));
			const FString Expected = FString::Printf(TEXT("[%s] Power — %d · energy 2"), *KeyName(InteractKey), T ? T->GetCost() : -1);
			Check(PromptMode() == EInteractPromptMode::Power && Prompt() && Prompt()->GetShownText() == Expected,
				FString::Printf(TEXT("%s: in reach in Normal time: %s (expected \"%s\")"), *P, *PromptState(), *Expected));
			Shot(P + TEXT(" power prompt"));
			return GRun.bShots ? 0.3f : 0.f;
		}));
		S.Add(Do(P + TEXT(": one more banked energy"), []()
		{
			if (UEnergySubsystem* E = Energy())
			{
				E->AddBankedEnergy(1);
			}
			return 0.1f;
		}));
		S.Add(Do(P + TEXT(": check the number follows"), [P]()
		{
			Check(Prompt() && Prompt()->GetShownText().EndsWith(TEXT("energy 3")), FString::Printf(TEXT("%s: banked energy 3 shows at once (%s)"), *P, *PromptState()));
			StopTime();
			return 0.3f;
		}));
		S.Add(Do(P + TEXT(": check Stopped at the terminal"), [P]()
		{
			Check(PromptMode() != EInteractPromptMode::Power, FString::Printf(TEXT("%s: no terminal prompt while Stopped (%s)"), *P, *PromptState()));
			ResumeTime();
			return 0.6f;
		}));
		S.Add(Do(P + TEXT(": pay"), [InteractKey]()
		{
			GoToTerminal(GTerminal.Get());
			Key(InteractKey, IE_Pressed);
			return 0.05f;
		}));
		S.Add(Do(P + TEXT(": check powered"), [P, InteractKey]()
		{
			Key(InteractKey, IE_Released);
			Check(AnyTargetPowered(GTerminal.Get()), P + TEXT(": the press powered the targets"));
			return 0.2f;
		}));
		S.Add(Do(P + TEXT(": check hidden when powered"), [P]()
		{
			Check(PromptMode() == EInteractPromptMode::Hidden, FString::Printf(TEXT("%s: nothing when the targets are powered (%s)"), *P, *PromptState()));
			PowerOffTargets(GTerminal.Get());
			if (UEnergySubsystem* E = Energy())
			{
				E->RestoreBankedEnergy(0);
			}
			return 0.2f;
		}));
		S.Add(Do(P + TEXT(": press with zero energy"), [P, InteractKey]()
		{
			Check(PromptMode() == EInteractPromptMode::Power && Prompt() && Prompt()->GetShownText().EndsWith(TEXT("energy 0")),
				FString::Printf(TEXT("%s: unpowered again, zero energy (%s)"), *P, *PromptState()));
			Key(InteractKey, IE_Pressed);
			return 0.05f;
		}));
		S.Add(Do(P + TEXT(": check refusal"), [P, InteractKey]()
		{
			Key(InteractKey, IE_Released);
			Check(PromptMode() == EInteractPromptMode::Refusal && Prompt() && Prompt()->GetShownText() == TEXT("Not enough energy"),
				FString::Printf(TEXT("%s: the refusal shows (%s)"), *P, *PromptState()));
			Shot(P + TEXT(" refusal"));
			Check(!AnyTargetPowered(GTerminal.Get()), P + TEXT(": nothing was powered"));
			Check(Energy() && Energy()->GetBankedEnergy() == 0, P + TEXT(": no energy spent"));
			return 1.8f;
		}));
		S.Add(Do(P + TEXT(": check refusal at 1.85 s"), [P]()
		{
			Check(PromptMode() == EInteractPromptMode::Refusal, FString::Printf(TEXT("%s: still shown at 1.85 s (%s)"), *P, *PromptState()));
			return 0.35f;
		}));
		S.Add(Do(P + TEXT(": check refusal over"), [P]()
		{
			Check(PromptMode() == EInteractPromptMode::Power, FString::Printf(TEXT("%s: back to the prompt after 2 s (%s)"), *P, *PromptState()));
			return 0.f;
		}));
	}

	static UStdInputSubsystem* InputSubsystem()
	{
		const APlayerController* Controller = PC();
		const ULocalPlayer* LP = Controller ? Controller->GetLocalPlayer() : nullptr;
		return LP ? LP->GetSubsystem<UStdInputSubsystem>() : nullptr;
	}

	static void AddRebindPhase(TArray<FStep>& S)
	{
		S.Add(Do(TEXT("rebind: Interact to R in StdCore's key settings"), []()
		{
			FText Message;
			UStdInputSubsystem* Input = InputSubsystem();
			const EStdRemapResult Result = Input ? Input->RemapKey(TEXT("Interact"), EPlayerMappableKeySlot::First, EKeys::R, Message) : EStdRemapResult::Rejected;
			Check(Result == EStdRemapResult::Remapped || Result == EStdRemapResult::Swapped,
				FString::Printf(TEXT("rebind: RemapKey(Interact, R) = %s %s"), *UEnum::GetValueAsString(Result), *Message.ToString()));
			if (UEnergySubsystem* E = Energy())
			{
				E->RestoreBankedEnergy(2);
			}
			PowerOffTargets(GTerminal.Get());
			GoToTerminal(GTerminal.Get());
			return 0.3f;
		}));
		S.Add(Do(TEXT("rebind: check the terminal prompt"), []()
		{
			Check(PromptMode() == EInteractPromptMode::Power && PromptKey() == EKeys::R, FString::Printf(TEXT("rebind: the terminal prompt shows R without a restart (%s)"), *PromptState()));
			return 0.f;
		}));
		AddPromptPhase(S, EKeys::R, TEXT("[R]"));
		S.Add(Do(TEXT("rebind: reset keys to defaults"), []()
		{
			if (UStdInputSubsystem* Input = InputSubsystem())
			{
				Input->ResetKeysToDefaults();
			}
			PowerOffTargets(GTerminal.Get());
			if (UEnergySubsystem* E = Energy())
			{
				E->RestoreBankedEnergy(2);
			}
			GoToTerminal(GTerminal.Get());
			return 0.3f;
		}));
		S.Add(Do(TEXT("rebind: check the reset"), []()
		{
			Check(PromptKey() == EKeys::E, FString::Printf(TEXT("rebind: back to E after the reset (%s)"), *PromptState()));
			if (UEnergySubsystem* E = Energy())
			{
				E->RestoreBankedEnergy(0);
			}
			return 0.f;
		}));
	}

	// --- Generated area phase ---

	static TArray<TWeakObjectPtr<AActor>> GAreaItems;
	static int32 GAreaItem = 0;

	static void AddAreaItem(TArray<FStep>& S, int32 Index)
	{
		const FString What = FString::Printf(TEXT("area item %d"), Index);
		S.Add(Do(What + TEXT(": go 5 m in front"), [Index, What]()
		{
			ResumeTime();
			GScene = FScene();
			AActor* Item = GAreaItems.IsValidIndex(Index) ? GAreaItems[Index].Get() : nullptr;
			if (!Item)
			{
				return 0.f;
			}
			if (UStaticMeshComponent* Mesh = InteractableMesh(Item))
			{
				GScene.Targets.Add(Mesh);
				GScene.Labels.Add(Item->GetName());
			}
			FVector Front = Item->GetActorForwardVector();
			FVector Target = Item->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			if (const ADoorDevice* Door = Cast<ADoorDevice>(Item))
			{
				Target = Item->GetActorLocation() + FVector(0.f, 0.f, Door->GetDoorSize().Z * 0.5f);
				// The side of the door the terminal stands on, where the player comes from.
				for (const APowerTerminal* T : Actors<APowerTerminal>())
				{
					if (T->Targets.Contains(Door) && FVector::DotProduct(T->GetActorLocation() - Item->GetActorLocation(), Front) < 0.f)
					{
						Front = -Front;
					}
				}
			}
			// The farthest spot in front, from 5 m down to 1.5 m, with floor at the item's level and a clear view of it
			// (catwalks: 5 m out may be over the void).
			UWorld* World = W();
			float Chosen = -1.f;
			FVector Floor = FVector::ZeroVector;
			for (const float Distance : { 500.f, 400.f, 300.f, 200.f, 150.f })
			{
				const FVector Above = Item->GetActorLocation() + Front * Distance + FVector(0.f, 0.f, 150.f);
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ReadabilityAreaFloor), false, Pawn());
				if (!World || !World->LineTraceSingleByChannel(Hit, Above, Above - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params)
					|| FMath::Abs(Hit.ImpactPoint.Z - Item->GetActorLocation().Z) > 60.f)
				{
					continue;
				}
				const FVector Eye = Hit.ImpactPoint + FVector(0.f, 0.f, 150.f);
				FHitResult Block;
				FCollisionQueryParams ViewParams(SCENE_QUERY_STAT(ReadabilityAreaView), false, Pawn());
				ViewParams.AddIgnoredActor(Item);
				if (World->LineTraceSingleByChannel(Block, Eye, Target, ECC_Visibility, ViewParams))
				{
					continue;
				}
				Chosen = Distance;
				Floor = Hit.ImpactPoint;
				break;
			}
			if (Chosen < 0.f)
			{
				Floor = Item->GetActorLocation() + Front * 150.f;
				Chosen = 150.f;
				Note(FString::Printf(TEXT("%s: no clear floor spot found, standing 1.5 m in front"), *What));
			}
			PlacePawn(Floor, Target);
			Note(FString::Printf(TEXT("%s: %s at %s, viewed from %.1f m"), *What, *Item->GetName(), *Item->GetActorLocation().ToCompactString(), Chosen / 100.f));
			return 0.3f;
		}));
		AddScanAB(S, What);
		S.Add(Do(What + TEXT(": check"), [What]()
		{
			const int32 Channel = UTheLastShiftSettings::GetInteractableScanChannel();
			const TArray<FIntVector> Per = PerTargetCounts(GAB.ImageA, SceneTargets(), Channel);
			const FIntVector P = Per.Num() > 0 ? Per[0] : FIntVector::ZeroValue;
			Check(P.Y > 0 && P.Y * 10 >= P.X * 9, FString::Printf(TEXT("%s: shows in the interactable channel: %d of its %d texels (%d default)"), *What, P.Y, P.X, P.Z));
			ResumeTime();
			return 0.6f;
		}));
	}

	static void StartArea(TArray<FStep>& S)
	{
		GAreaItems.Reset();
		for (APowerTerminal* T : Actors<APowerTerminal>())
		{
			GAreaItems.Add(T);
		}
		for (ADoorDevice* D : Actors<ADoorDevice>())
		{
			GAreaItems.Add(D);
		}
		const AAreaGenerator* Generator = AAreaGenerator::Find(W());
		Note(FString::Printf(TEXT("area: %s, layout hash %016llx, %d terminals and doors"), *GetNameSafe(W()),
			Generator ? static_cast<unsigned long long>(Generator->GetLayoutHash()) : 0ull, GAreaItems.Num()));
		for (int32 I = 0; I < GAreaItems.Num(); ++I)
		{
			AddAreaItem(S, I);
		}
		AddPromptPhase(S, EKeys::E, TEXT(" (area)"));
		S.Add(Do(TEXT("area: done"), []() { ResumeTime(); GScene = FScene(); return 0.f; }));
	}

	static void Finish(const TCHAR* Why)
	{
		ResumeTime();
		if (GRun.Name.Contains(TEXT("area")))
		{
			GScene = FScene(); // the area's own actors are not destroyed
		}
		else
		{
			ClearScene();
		}
		GRun.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("%s summary (%s): %d checks passed, %d failed%s."), *GRun.Name,
			GRun.Failed == 0 && GRun.Next >= GRun.Steps.Num() ? TEXT("PASS") : TEXT("FAIL"), GRun.Passed, GRun.Failed, Why);
		if (GRun.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.ReadabilityTest"));
		}
	}

	static void Start(const FString& Mode, bool bQuit, bool bShots);

	/** Area mode in a packaged run: -ExecCmds runs before the area exists, so wait (up to 120 s) for it to be ready. */
	static FTSTicker::FDelegateHandle GWaitTicker;
	static double GWaitUntil = 0.0;

	static void StartWhenAreaReady(bool bQuit, bool bShots)
	{
		GWaitUntil = FPlatformTime::Seconds() + 120.0;
		UE_LOG(LogTheLastShift, Log, TEXT("tls.ReadabilityTest area: waiting for the area to be ready."));
		GWaitTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bQuit, bShots](float)
		{
			UWorld* World = W();
			const AAreaGenerator* Generator = World ? AAreaGenerator::Find(World) : nullptr;
			const bool bReady = Generator && Generator->IsReady() && !Generator->IsHoldingPlayer() && Pawn() && Director();
			if (!bReady && FPlatformTime::Seconds() < GWaitUntil)
			{
				return true;
			}
			// One more second for the travel screen to clear.
			GWaitUntil = FPlatformTime::Seconds() + 1.0;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bQuit, bShots](float)
			{
				if (FPlatformTime::Seconds() < GWaitUntil)
				{
					return true;
				}
				Start(TEXT("area"), bQuit, bShots);
				return false;
			}));
			return false;
		}));
	}

	static void Start(const FString& Mode, bool bQuit, bool bShots)
	{
		if (GRun.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ReadabilityTest: already running."));
			return;
		}
		if (!W() || !Pawn() || !Director() || !Lidar() || !TS())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ReadabilityTest: needs a game world with the player pawn, its scan director and lidar."));
			return;
		}
		GRun = FRun();
		GRun.Name = FString::Printf(TEXT("tls.ReadabilityTest %s"), *Mode);
		GRun.bQuit = bQuit;
		GRun.bShots = bShots;
		TArray<FStep> S;
		const bool bAll = Mode == TEXT("all");
		if (Mode == TEXT("area"))
		{
			StartArea(S);
		}
		else
		{
			if (bAll || Mode == TEXT("scan"))
			{
				AddScanPhase(S);
			}
			if (bAll || Mode == TEXT("colors"))
			{
				AddColorsPhase(S);
			}
			if (bAll || Mode == TEXT("prompt"))
			{
				AddPromptPhase(S, EKeys::E, TEXT(""));
				AddRebindPhase(S);
			}
		}
		GRun.Steps = MoveTemp(S);
		GRun.bRunning = true;
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickRun));
		UE_LOG(LogTheLastShift, Log, TEXT("%s started: %d steps."), *GRun.Name, GRun.Steps.Num());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsReadabilityTestCommand(
	TEXT("tls.ReadabilityTest"),
	TEXT("tls.ReadabilityTest [all|scan|colors|prompt|area]: scan = a terminal and a closed door at 5 m and 30 m (in TestLidar on a 40 m lane built outside the room, elsewhere along the longest clear line) show in the interactable channel (the newest scan's capture is read back: at least 90 % of the texels on each mesh carry the channel; untagged, none does; tagging changes no depth and no injected point), the points stay with their scan and go on Resume; a terminal behind a wall adds no channel texel. colors = every reader of ScanColors follows a change, and a changed InteractableScanChannel moves the interactables to that channel. prompt = residue and terminal prompts, refusal, rebinding Interact in StdCore's settings. area = every terminal and door of a generated area, then the prompts. Options after the mode: wait (area: wait until the area is ready, for -ExecCmds), shots (screenshot with the UI at the colour and prompt moments), quit (exit at the end). On a fresh PIE; keeps the entity dormant."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString Mode = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("all"));
		bool bQuit = false, bShots = false, bWait = false;
		for (int32 I = 1; I < Args.Num(); ++I)
		{
			bQuit |= Args[I].Equals(TEXT("quit"), ESearchCase::IgnoreCase);
			bShots |= Args[I].Equals(TEXT("shots"), ESearchCase::IgnoreCase);
			bWait |= Args[I].Equals(TEXT("wait"), ESearchCase::IgnoreCase);
		}
		if (bWait && Mode == TEXT("area"))
		{
			ReadabilityDebug::StartWhenAreaReady(bQuit, bShots);
			return;
		}
		ReadabilityDebug::Start(Mode, bQuit, bShots);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsReadabilitySpawnCommand(
	TEXT("tls.Readability.Spawn"),
	TEXT("tls.Readability.Spawn [lane] [Distance=5 30 ...]: lane first builds the 40 m test lane outside the TestLidar room and stands on it; then spawns a terminal and a closed door at each distance (m) along the view (default 5 and 30), kept until tls.Readability.Clear. For screenshots and timing."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		TArray<float> Distances;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("lane"), ESearchCase::IgnoreCase))
			{
				ReadabilityDebug::BuildLane(ReadabilityDebug::LaneOrigin);
				continue;
			}
			Distances.Add(FMath::Clamp(FCString::Atof(*Arg), 1.f, 200.f) * 100.f);
		}
		if (Distances.Num() == 0)
		{
			Distances = { 500.f, 3000.f };
		}
		ReadabilityDebug::SpawnSet(Distances);
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Readability.Spawn: %d actors in the test scene."), ReadabilityDebug::GScene.Actors.Num());
	}));

static FAutoConsoleCommand GTlsReadabilityClearCommand(
	TEXT("tls.Readability.Clear"),
	TEXT("tls.Readability.Clear: destroys what tls.Readability.Spawn spawned."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		ReadabilityDebug::ClearScene();
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Readability.Clear: done."));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsReadabilityTagCommand(
	TEXT("tls.Readability.Tag"),
	TEXT("tls.Readability.Tag 0|1: untags (0) or tags again (1) the mesh of every terminal and door in the world for the interactable channel (custom depth on/off), from the next scan on. For A/B checks of the same scene."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace ReadabilityDebug;
		const bool bOn = Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0;
		int32 Count = 0;
		for (AActor* Actor : Actors<AActor>())
		{
			if (UStaticMeshComponent* Mesh = InteractableMesh(Actor))
			{
				Mesh->SetRenderCustomDepth(bOn);
				++Count;
			}
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Readability.Tag: %d interactable meshes %s."), Count, bOn ? TEXT("tagged") : TEXT("untagged"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsReadabilityChannelsCommand(
	TEXT("tls.Readability.Channels"),
	TEXT("tls.Readability.Channels [Channel]: reads back the newest alive scan's capture and logs, for every terminal and door in the world, the texels whose point lies on its mesh (3 cm tolerance) and how many of them are in Channel (default InteractableScanChannel) or in the default channel; then the texels with depth, in the channel, and in the channel off every mesh."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		using namespace ReadabilityDebug;
		const int32 Channel = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : UTheLastShiftSettings::GetInteractableScanChannel();
		FChannelImage Image;
		if (!ReadNewestChannels(Image))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Readability.Channels: no alive scan, or the readback failed."));
			return;
		}
		TArray<UStaticMeshComponent*> Targets;
		TArray<FString> Labels;
		for (AActor* Actor : Actors<AActor>())
		{
			if (UStaticMeshComponent* Mesh = InteractableMesh(Actor))
			{
				Targets.Add(Mesh);
				Labels.Add(FString::Printf(TEXT("%s at %.1f m"), *Actor->GetName(), FVector::Dist(Image.Context.CaptureOrigin, Mesh->Bounds.Origin) / 100.f));
			}
		}
		const FChannelCounts C = CountChannels(Image, Targets, Labels, Channel);
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Readability.Channels: scan %d, channels %s; %d texels with depth, %d in channel %d (%d off every mesh); on the meshes %d texels, %d in the channel, %d default. %s"),
			Image.Context.ScanIndex, Image.bHasChannels ? TEXT("read") : TEXT("NOT FOUND"), C.Valid, C.InChannel, Channel, C.InChannelOffTarget,
			C.OnTarget, C.OnTargetInChannel, C.OnTargetDefault, *C.Detail);
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Readability.Channels: target %s."), *Image.Raw);

		// Texels whose stencil is above the last channel (the default channel): where they are and what they hit.
		const FLidarScanContext& X = Image.Context;
		int32 Above = 0, AboveNoDepth = 0;
		TMap<FString, int32> HitBy;
		UWorld* World = W();
		for (int32 Y = 0; Y < Image.Height; ++Y)
		{
			for (int32 Xi = 0; Xi < Image.Width; ++Xi)
			{
				const int32 Index = Y * Image.Width + Xi;
				if (Image.Channel[Index] <= ULidarScanComponent::NumChannels)
				{
					continue;
				}
				++Above;
				const float Depth = Image.Depth[Index];
				if (Depth <= 1.f || Depth >= X.MaxRange)
				{
					++AboveNoDepth;
					continue;
				}
				const double U = ((Xi + 0.5) / Image.Width) * 2.0 - 1.0;
				const double V = 1.0 - ((Y + 0.5) / Image.Height) * 2.0;
				const FVector Ray = X.CaptureForward + U * X.TanHalfFOV.X * X.CaptureRight + V * X.TanHalfFOV.Y * X.CaptureUp;
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsChannelsAbove), true, Pawn());
				const bool bHit = World && World->LineTraceSingleByChannel(Hit, X.CaptureOrigin, X.CaptureOrigin + Ray * (Depth + 20.f), ECC_Visibility, Params);
				HitBy.FindOrAdd(bHit && Hit.GetComponent() ? FString::Printf(TEXT("%s.%s (stencil %d)"), *GetNameSafe(Hit.GetActor()), *Hit.GetComponent()->GetName(),
					Hit.GetComponent()->CustomDepthStencilValue) : FString(TEXT("no collision hit")))++;
			}
		}
		FString Hits;
		for (const TPair<FString, int32>& Pair : HitBy)
		{
			Hits += FString::Printf(TEXT("%s%s: %d"), Hits.IsEmpty() ? TEXT("") : TEXT(", "), *Pair.Key, Pair.Value);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Readability.Channels: %d texels with a stencil above channel %d: %d with no depth sample (sky or beyond MaxRange)%s%s."),
			Above, ULidarScanComponent::NumChannels, AboveNoDepth, Hits.IsEmpty() ? TEXT("") : TEXT("; the rest hit "), *Hits);
	}));

#endif // !UE_BUILD_SHIPPING

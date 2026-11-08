// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#include "LidarScanComponent.h"

#include "LidarScanModule.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LidarScanComponent)

/**
 * Contract with the point system (NS_LidarPoints). Each scan slot is one system instance.
 *
 * Spawn (once per activation): burst of PointCount particles. Particle i reads depth texel
 * (i % CaptureWidth, i / CaptureWidth) and is placed at
 *   CaptureOrigin + (CaptureForward + u' * TanHalfFov.x * CaptureRight + v' * TanHalfFov.y * CaptureUp) * (Depth - SurfaceBias)
 * with u', v' in [-1, 1]. SceneDepth is planar view depth, so the forward component of the ray is exactly 1.
 * Samples with depth <= 1 or >= MaxRange are killed.
 *
 * Update (every frame): colour and size from the live parameters,
 *   alpha = PointColor.a * lerp(1, AlphaFloor, Age / FadeDuration).
 * ClearScans fades a scan out by scaling PointColor.a on that instance, so the system needs no extra parameter.
 */
namespace LidarScanParams
{
	static const FName DepthTexture(TEXT("User.DepthTextureDI"));
	static const FName PointCount(TEXT("User.PointCount"));
	static const FName CaptureWidth(TEXT("User.CaptureWidth"));
	static const FName CaptureHeight(TEXT("User.CaptureHeight"));
	static const FName CaptureOrigin(TEXT("User.CaptureOrigin"));
	static const FName CaptureForward(TEXT("User.CaptureForward"));
	static const FName CaptureRight(TEXT("User.CaptureRight"));
	static const FName CaptureUp(TEXT("User.CaptureUp"));
	static const FName TanHalfFov(TEXT("User.TanHalfFov"));
	static const FName MaxRange(TEXT("User.MaxRange"));
	static const FName SurfaceBias(TEXT("User.SurfaceBias"));
	static const FName PointSize(TEXT("User.PointSize"));
	static const FName PointColor(TEXT("User.PointColor"));
	static const FName FadeDuration(TEXT("User.FadeDuration"));
	static const FName AlphaFloor(TEXT("User.AlphaFloor"));
}

ULidarScanComponent::ULidarScanComponent()
{
	// Ticks only while a ClearScans fade-out is running.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PointSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/LidarScan/NS_LidarPoints.NS_LidarPoints")));
}

void ULidarScanComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureInitialized();
}

void ULidarScanComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FLidarScanSlot& Slot : Slots)
	{
		DestroySlot(Slot);
	}
	Slots.Reset();
	SetComponentTickEnabled(false);

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}

	if (DepthCapture)
	{
		DepthCapture->DestroyComponent();
		DepthCapture = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ULidarScanComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Lets the Details panel tune alive scans during PIE without recompiling.
	if (const UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		ApplyLiveSettings();
	}
}
#endif

bool ULidarScanComponent::EnsureInitialized()
{
	if (!LoadedPointSystem)
	{
		LoadedPointSystem = PointSystem.LoadSynchronous();
		if (!LoadedPointSystem)
		{
			UE_LOG(LogLidarScan, Warning, TEXT("%s: PointSystem '%s' could not be loaded, scans disabled."),
				*GetPathName(), *PointSystem.ToString());
			return false;
		}
	}

	if (!DepthCapture)
	{
		AActor* Owner = GetOwner();
		if (!Owner)
		{
			return false;
		}

		DepthCapture = NewObject<USceneCaptureComponent2D>(Owner, NAME_None, RF_Transient);
		DepthCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		DepthCapture->bCaptureEveryFrame = false;
		DepthCapture->bCaptureOnMovement = false;
		DepthCapture->bAlwaysPersistRenderingState = false;

		// Depth only: drop everything that cannot change the depth buffer, and never capture our own points.
		FEngineShowFlags& Flags = DepthCapture->ShowFlags;
		Flags.SetParticles(false);
		Flags.SetTranslucency(false);
		Flags.SetDecals(false);
		Flags.SetFog(false);
		Flags.SetVolumetricFog(false);
		Flags.SetAtmosphere(false);
		Flags.SetDynamicShadows(false);
		Flags.SetAmbientOcclusion(false);
		Flags.SetScreenSpaceReflections(false);
		Flags.SetPostProcessing(false);
		Flags.SetBloom(false);
		Flags.SetMotionBlur(false);
		Flags.SetEyeAdaptation(false);
		Flags.SetAntiAliasing(false);
		Flags.SetTemporalAA(false);

		if (bHideOwnerInCapture)
		{
			DepthCapture->HiddenActors.Add(Owner);
		}

		DepthCapture->SetupAttachment(this);
		DepthCapture->RegisterComponent();
	}

	return true;
}

void ULidarScanComponent::Scan()
{
	if (!EnsureInitialized())
	{
		return;
	}

	TrimSlots();

	const int32 SlotIndex = AcquireSlot();
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	FLidarScanSlot& Slot = Slots[SlotIndex];
	UTextureRenderTarget2D* Target = EnsureDepthTarget(Slot);
	UNiagaraComponent* Points = Slot.Points;
	if (!Target || !Points)
	{
		return;
	}

	const float HorizontalFOV = ResolveHorizontalFOV();
	DepthCapture->FOVAngle = HorizontalFOV;
	DepthCapture->MaxViewDistanceOverride = MaxRange;
	DepthCapture->TextureTarget = Target;
	DepthCapture->CaptureScene();

	// Basis of the view that was just rendered: X forward, Y screen right, Z screen up.
	const FTransform CaptureTransform = DepthCapture->GetComponentTransform();
	const FVector Origin = CaptureTransform.GetLocation();
	const double TanX = FMath::Tan(FMath::DegreesToRadians(HorizontalFOV * 0.5));
	const double TanY = TanX * static_cast<double>(Target->SizeY) / static_cast<double>(Target->SizeX);

	// Points are simulated in world space; the component only anchors the fixed bounds.
	Points->SetWorldLocationAndRotation(Origin, FRotator::ZeroRotator);
	Points->SetSystemFixedBounds(FBox(FVector(-MaxRange), FVector(MaxRange)));

	Points->SetVariableInt(LidarScanParams::PointCount, Target->SizeX * Target->SizeY);
	Points->SetVariableInt(LidarScanParams::CaptureWidth, Target->SizeX);
	Points->SetVariableInt(LidarScanParams::CaptureHeight, Target->SizeY);
	Points->SetVariablePosition(LidarScanParams::CaptureOrigin, Origin);
	Points->SetVariableVec3(LidarScanParams::CaptureForward, CaptureTransform.GetUnitAxis(EAxis::X));
	Points->SetVariableVec3(LidarScanParams::CaptureRight, CaptureTransform.GetUnitAxis(EAxis::Y));
	Points->SetVariableVec3(LidarScanParams::CaptureUp, CaptureTransform.GetUnitAxis(EAxis::Z));
	Points->SetVariableVec2(LidarScanParams::TanHalfFov, FVector2D(TanX, TanY));
	Points->SetVariableFloat(LidarScanParams::MaxRange, MaxRange);
	Points->SetVariableFloat(LidarScanParams::SurfaceBias, SurfaceBias);

	// A recycled slot may still be fading out from a previous ClearScans.
	Slot.FadeOutDuration = 0.f;
	Slot.FadeOutElapsed = 0.f;
	PushLiveParameters(Slot);

	// Reset: clears the previous scan held by this slot and re-runs the one-shot spawn burst.
	Points->Activate(true);

	const int32 ScanIndex = ScanCount++;
	Slot.ScanIndex = ScanIndex;

	UWorld* World = GetWorld();
	Slot.StartRealTime = World ? World->GetRealTimeSeconds() : 0.0;

	OnScanStarted.Broadcast(ScanIndex, World ? static_cast<float>(World->GetTimeSeconds()) : 0.f);

	// The GPU spawn burst runs on the system's next tick; report completion once that frame has gone through.
	if (World)
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ULidarScanComponent::BroadcastScanCompleted, ScanIndex));
	}
	else
	{
		BroadcastScanCompleted(ScanIndex);
	}
}

void ULidarScanComponent::BroadcastScanCompleted(int32 ScanIndex)
{
	const UWorld* World = GetWorld();
	OnScanCompleted.Broadcast(ScanIndex, World ? static_cast<float>(World->GetTimeSeconds()) : 0.f);
}

void ULidarScanComponent::ClearScans(float FadeOutSeconds)
{
	const float FadeOut = FMath::Max(FadeOutSeconds, 0.f);
	bool bAnyFading = false;

	for (FLidarScanSlot& Slot : Slots)
	{
		if (!Slot.IsActive())
		{
			continue;
		}

		if (FadeOut <= 0.f)
		{
			ReleaseSlot(Slot);
		}
		else
		{
			if (!Slot.IsFadingOut())
			{
				Slot.FadeOutDuration = FadeOut;
				Slot.FadeOutElapsed = 0.f;
			}
			bAnyFading = true;
		}
	}

	SetComponentTickEnabled(bAnyFading);
}

void ULidarScanComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool bAnyFading = false;
	for (FLidarScanSlot& Slot : Slots)
	{
		if (!Slot.IsFadingOut())
		{
			continue;
		}

		Slot.FadeOutElapsed += DeltaTime;
		if (Slot.FadeOutElapsed >= Slot.FadeOutDuration)
		{
			ReleaseSlot(Slot);
		}
		else
		{
			PushLiveParameters(Slot);
			bAnyFading = true;
		}
	}

	if (!bAnyFading)
	{
		SetComponentTickEnabled(false);
	}
}

void ULidarScanComponent::ReleaseSlot(FLidarScanSlot& Slot)
{
	if (Slot.Points)
	{
		Slot.Points->DeactivateImmediate();
	}
	Slot.ScanIndex = INDEX_NONE;
	Slot.FadeOutDuration = 0.f;
	Slot.FadeOutElapsed = 0.f;
}

void ULidarScanComponent::ApplyLiveSettings()
{
	TrimSlots();
	for (const FLidarScanSlot& Slot : Slots)
	{
		PushLiveParameters(Slot);
	}
}

int32 ULidarScanComponent::GetAliveScanCount() const
{
	int32 Count = 0;
	for (const FLidarScanSlot& Slot : Slots)
	{
		Count += (Slot.IsActive() && !Slot.IsFadingOut()) ? 1 : 0;
	}
	return Count;
}

int32 ULidarScanComponent::AcquireSlot()
{
	// 1. An idle slot.
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (!Slots[Index].IsActive())
		{
			return Index;
		}
	}

	// 2. A new slot while under the cap.
	if (Slots.Num() < FMath::Clamp(MaxAliveScans, 1, 6))
	{
		AActor* Owner = GetOwner();
		if (!Owner || !LoadedPointSystem)
		{
			return INDEX_NONE;
		}

		UNiagaraComponent* Points = NewObject<UNiagaraComponent>(Owner, NAME_None, RF_Transient);
		Points->SetAutoActivate(false);
		Points->SetAutoDestroy(false);
		Points->SetAsset(LoadedPointSystem);
		Points->RegisterComponent();

		FLidarScanSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Points = Points;
		return Slots.Num() - 1;
	}

	// 3. Recycle the oldest scan.
	int32 Oldest = 0;
	for (int32 Index = 1; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].ScanIndex < Slots[Oldest].ScanIndex)
		{
			Oldest = Index;
		}
	}
	return Oldest;
}

void ULidarScanComponent::DestroySlot(FLidarScanSlot& Slot)
{
	if (Slot.Points)
	{
		Slot.Points->DestroyComponent();
		Slot.Points = nullptr;
	}
	Slot.DepthTarget = nullptr;
	Slot.ScanIndex = INDEX_NONE;
	Slot.FadeOutDuration = 0.f;
	Slot.FadeOutElapsed = 0.f;
}

void ULidarScanComponent::TrimSlots()
{
	const int32 Cap = FMath::Clamp(MaxAliveScans, 1, 6);
	while (Slots.Num() > Cap)
	{
		// Drop idle slots first, then the oldest scan.
		int32 Victim = 0;
		for (int32 Index = 1; Index < Slots.Num(); ++Index)
		{
			if (Slots[Index].ScanIndex < Slots[Victim].ScanIndex)
			{
				Victim = Index;
			}
		}
		DestroySlot(Slots[Victim]);
		Slots.RemoveAt(Victim);
	}
}

UTextureRenderTarget2D* ULidarScanComponent::EnsureDepthTarget(FLidarScanSlot& Slot)
{
	const int32 Resolution = FMath::Clamp(CaptureResolution, 64, 1024);
	if (Slot.DepthTarget && Slot.DepthTarget->SizeX == Resolution && Slot.DepthTarget->SizeY == Resolution)
	{
		return Slot.DepthTarget;
	}

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
	Target->RenderTargetFormat = RTF_R32f;
	Target->ClearColor = FLinearColor::Black; // depth 0 -> no point
	Target->bAutoGenerateMips = false;
	Target->Filter = TF_Nearest;
	Target->AddressX = TA_Clamp;
	Target->AddressY = TA_Clamp;
	Target->InitAutoFormat(Resolution, Resolution);
	Target->UpdateResourceImmediate(true);

	Slot.DepthTarget = Target;
	UNiagaraFunctionLibrary::SetTextureObject(Slot.Points, LidarScanParams::DepthTexture.ToString(), Target);
	return Target;
}

float ULidarScanComponent::ResolveHorizontalFOV() const
{
	if (FieldOfViewOverride > 0.f)
	{
		return FieldOfViewOverride;
	}

	for (const USceneComponent* Parent = GetAttachParent(); Parent; Parent = Parent->GetAttachParent())
	{
		if (const UCameraComponent* Camera = Cast<UCameraComponent>(Parent))
		{
			return FMath::Min(Camera->FieldOfView + FieldOfViewPadding, 170.f);
		}
	}
	return FMath::Min(90.f + FieldOfViewPadding, 170.f);
}

void ULidarScanComponent::PushLiveParameters(const FLidarScanSlot& Slot) const
{
	UNiagaraComponent* Points = Slot.Points;
	if (!Points)
	{
		return;
	}

	FLinearColor Emissive = PointColor * PointIntensity;
	Emissive.A = PointColor.A * Slot.GetFadeOutAlpha();

	Points->SetVariableFloat(LidarScanParams::PointSize, PointSize);
	Points->SetVariableLinearColor(LidarScanParams::PointColor, Emissive);
	Points->SetVariableFloat(LidarScanParams::FadeDuration, FadeDuration);
	Points->SetVariableFloat(LidarScanParams::AlphaFloor, AlphaFloor);
}

// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#include "LidarScanComponent.h"

#include "LidarScanModule.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "SceneView.h"
#include "TimerManager.h"
#include "UnrealClient.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LidarScanComponent)

/**
 * Contract with the point system (NS_LidarPoints). Each scan slot is one system instance.
 *
 * Spawn (once per activation): burst of PointCount particles. Particle i takes texel (i % CaptureWidth, i / CaptureWidth),
 * offsets its UV by a hash of (i, JitterSeed) in [-SampleJitter, SampleJitter] texels per axis, reads depth at that
 * UV and is placed at
 *   CaptureOrigin + (CaptureForward + u' * TanHalfFov.x * CaptureRight + v' * TanHalfFov.y * CaptureUp) * (Depth - SurfaceBias)
 * with u', v' in [-1, 1] from the same jittered UV. SceneDepth is planar view depth, so the forward component of the
 * ray is exactly 1. That position is stored as Particles.LidarSurfacePosition (and copied to Particles.Position).
 * Samples with depth <= 1, >= MaxRange or < MinSampleDepth are killed. With NearThinningDistance > 0 the others are
 * kept only if
 *   rand3(i, JitterSeed, 1, 0).x < min(1, (Depth / NearThinningDistance)^2)
 * (a hash of the particle index and the per-scan seed: the same scan keeps the same samples on every frame, and the
 * stream differs from the jitter's). On a surface seen head-on the capture puts 1 / Depth^2 samples per unit area, so
 * the near field ends up with roughly the world spacing of a surface at NearThinningDistance. The raw depth is kept
 * per particle.
 *
 * Spawn also reconstructs a surface normal, Particles.LidarNormal, from the depth gradient. The particle's own texel
 * (the jitter stays inside it, so nearest filtering reads that texel) and its four neighbours (+-1 texel per axis,
 * read at texel centres) are unprojected through their texel-centre rays; per axis the one-sided difference whose
 * depth is closer to the centre's is kept (a neighbour off the target edge, or with depth <= 1 or >= MaxRange, is
 * never kept). If even the kept neighbour differs from the centre depth by more than 0.25 x the centre depth, the
 * sample sits on a depth discontinuity (a one-texel-wide feature) and its normal is the capture ray. On a plane the
 * per-texel depth step only reaches 0.25 x depth at grazing angles under about 2 degrees, beyond MaxRange at eye
 * height. N = normalize(cross(Tangent X, Tangent Y)), then flipped if needed so that dot(N, CaptureOrigin - S) >= 0,
 * with S = LidarSurfacePosition: the normal points to the side of the surface the capture saw (the fallback capture
 * ray is flipped the same way). The angle term below only uses |dot|; the surface offset uses the sign.
 * A particle with LidarNormal = 0 (injected points, which have no normal) gets an angle term of 1.
 * e = 0.02 floors both cosines (same floor on both, so the term is exactly 1 from the scan's own pose): it only
 * guards the division and the pow; at that angle the clamp below already decides the result.
 *
 * Update (every frame): draw position, colour and size from the live parameters,
 *   Position = S + N * SurfaceOffset                                  if |N|^2 > 0.25,
 *   Position = S + normalize(ViewOrigin - S) * min(ViewOffsetMaxCm, 0.5 * |ViewOrigin - S|)   otherwise,
 *   rgb   = PointColor.rgb * clamp(pow(SampleDepth / zs, DensityCompensation) * AngleTerm, DensityBoostMin, DensityBoostMax),
 *   AngleTerm = pow(max(saturate(|dot(N, Vview)|), e) / max(saturate(|dot(N, Vcapture)|), e), AngleCompensation),
 *   Vview = normalize(S - ViewOrigin), Vcapture = normalize(S - CaptureOrigin), e = 0.02,
 *   alpha = PointColor.a * lerp(1, AlphaFloor, Age / FadeDuration),
 *   size  = PointPixelSize * ViewPixelScale * z,
 *   z = max(dot(Position - ViewOrigin, ViewForward), 1), zs = max(dot(S - ViewOrigin, ViewForward), 1),
 *   SampleDepth = max(Depth - SurfaceBias, 1)
 * where ViewOrigin and ViewForward are the current camera, read on the GPU through a Camera Query data interface,
 * and ViewPixelScale = 2 / (Projection[1][1] * ViewHeightPixels) = 2 tan(VerticalFOV / 2) / ViewHeightPixels is
 * pushed by this component every frame. Sprites face the camera plane, so a sprite of world size s at planar view
 * depth z covers exactly s / (z * ViewPixelScale) pixels anywhere on screen: every point is PointPixelSize pixels.
 * The offset is a real displacement of the particle (no renderer camera offset), so the size needs no correction.
 * It lifts sprites off their own surface so grazing views do not clip them; being a fixed world distance it never
 * carries a point through geometry thicker than the offset, whatever the viewing distance.
 * Compensation reads S, not the drawn position: SampleDepth is the depth of S along CaptureForward (raw depth minus
 * the bias it was placed with), so SampleDepth / zs is exactly 1 when the scan is viewed from its own pose.
 * PointColor.rgb already carries PointIntensity; the additive material multiplies rgb by alpha.
 * ClearScans fades a scan out by scaling PointColor.a on that instance, so the system needs no extra parameter.
 *
 * Injected points (not produced by the depth capture) write LidarSurfacePosition and Position, and LidarNormal = 0:
 * they get the toward-viewer offset capped by ViewOffsetMaxCm and an angle term of 1. MinSampleDepth and the near
 * thinning are capture-sampling rules and do not apply to them.
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
	static const FName PointPixelSize(TEXT("User.PointPixelSize"));
	static const FName ViewPixelScale(TEXT("User.ViewPixelScale"));
	static const FName SurfaceOffset(TEXT("User.SurfaceOffset"));
	static const FName ViewOffsetMaxCm(TEXT("User.ViewOffsetMaxCm"));
	static const FName NearThinningDistance(TEXT("User.NearThinningDistance"));
	static const FName MinSampleDepth(TEXT("User.MinSampleDepth"));
	static const FName PointColor(TEXT("User.PointColor"));
	static const FName FadeDuration(TEXT("User.FadeDuration"));
	static const FName AlphaFloor(TEXT("User.AlphaFloor"));
	static const FName SampleJitter(TEXT("User.SampleJitter"));
	static const FName JitterSeed(TEXT("User.JitterSeed"));
	static const FName DensityCompensation(TEXT("User.DensityCompensation"));
	static const FName AngleCompensation(TEXT("User.AngleCompensation"));
	static const FName DensityBoostMin(TEXT("User.DensityBoostMin"));
	static const FName DensityBoostMax(TEXT("User.DensityBoostMax"));
}

ULidarScanComponent::ULidarScanComponent()
{
	// Ticks only while scans are alive: keeps the view pixel scale current and runs ClearScans fade-outs.
	// Post update work: the player camera has been updated for this frame.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
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
	bool bNewTarget = false;
	UTextureRenderTarget2D* Target = EnsureDepthTarget(Slot, bNewTarget);
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
	Points->SetVariableFloat(LidarScanParams::SampleJitter, FMath::Clamp(SampleJitter, 0.f, 1.f));
	Points->SetVariableFloat(LidarScanParams::NearThinningDistance, FMath::Clamp(NearThinningDistance, 0.f, 1000.f));
	Points->SetVariableFloat(LidarScanParams::MinSampleDepth, FMath::Clamp(MinSampleDepth, 0.f, 200.f));

	// Deterministic per scan index: the same scan never changes while alive, consecutive scans fill each other's gaps.
	const int32 ScanIndex = ScanCount++;
	Points->SetVariableInt(LidarScanParams::JitterSeed, static_cast<int32>(HashCombineFast(GetTypeHash(ScanIndex), 0x9E3779B9u) & 0x7FFFFFFF));

	// A recycled slot may still be fading out from a previous ClearScans.
	Slot.FadeOutDuration = 0.f;
	Slot.FadeOutElapsed = 0.f;
	ViewPixelScale = ResolveViewPixelScale();
	PushLiveParameters(Slot);

	Slot.ScanIndex = ScanIndex;
	SetComponentTickEnabled(true);

	UWorld* World = GetWorld();
	Slot.StartRealTime = World ? World->GetRealTimeSeconds() : 0.0;

	// A depth target created by this call is bound to the point system's texture interface on the render thread
	// only when the system next ticks; spawning in that same tick can sample the unbound texture and kill every
	// point (seen on the first scan of a slot). The capture is already issued, so spawn one frame later instead.
	if (bNewTarget && World)
	{
		Points->DeactivateImmediate();
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ULidarScanComponent::ActivateScanPoints, ScanIndex));
	}
	else
	{
		ActivateScanPoints(ScanIndex);
	}

	OnScanStarted.Broadcast(ScanIndex, World ? static_cast<float>(World->GetTimeSeconds()) : 0.f);
}

void ULidarScanComponent::ActivateScanPoints(int32 ScanIndex)
{
	for (const FLidarScanSlot& Slot : Slots)
	{
		if (Slot.ScanIndex == ScanIndex && Slot.Points)
		{
			// Reset: clears the previous scan held by this slot and re-runs the one-shot spawn burst.
			Slot.Points->Activate(true);
			break;
		}
	}

	// The GPU spawn burst runs on the system's next tick; report completion once that frame has gone through.
	if (UWorld* World = GetWorld())
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
	bool bAnyActive = false;

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
			bAnyActive = true;
		}
	}

	SetComponentTickEnabled(bAnyActive);
}

void ULidarScanComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool bAnyActive = false;
	for (FLidarScanSlot& Slot : Slots)
	{
		if (!Slot.IsFadingOut())
		{
			bAnyActive |= Slot.IsActive();
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
			bAnyActive = true;
		}
	}

	if (!bAnyActive)
	{
		SetComponentTickEnabled(false);
		return;
	}

	// FOV or viewport size may have changed since the last frame (zoom, window resize).
	UpdateViewPixelScale();
}

void ULidarScanComponent::UpdateViewPixelScale()
{
	const float NewScale = ResolveViewPixelScale();
	if (FMath::IsNearlyEqual(NewScale, ViewPixelScale, ViewPixelScale * 1.e-4f))
	{
		return;
	}

	ViewPixelScale = NewScale;
	for (const FLidarScanSlot& Slot : Slots)
	{
		if (Slot.IsActive() && Slot.Points)
		{
			Slot.Points->SetVariableFloat(LidarScanParams::ViewPixelScale, ViewPixelScale);
		}
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

UTextureRenderTarget2D* ULidarScanComponent::EnsureDepthTarget(FLidarScanSlot& Slot, bool& bOutCreated)
{
	bOutCreated = false;
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
	bOutCreated = true;
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

float ULidarScanComponent::ResolveViewPixelScale() const
{
	// Exact path: the projection the renderer builds for the local player viewing this world. M[1][1] is
	// 1 / tan(VerticalFOV / 2) whatever the aspect-ratio axis constraint, and the constrained rect excludes letterboxing.
	const UWorld* World = GetWorld();
	const APlayerController* Controller = nullptr;
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Controller = Cast<APlayerController>(Pawn->GetController());
	}
	if (!Controller && World)
	{
		Controller = World->GetFirstPlayerController();
	}

	if (const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr)
	{
		FViewport* Viewport = LocalPlayer->ViewportClient ? LocalPlayer->ViewportClient->Viewport : nullptr;
		FSceneViewProjectionData Projection;
		if (Viewport && LocalPlayer->GetProjectionData(Viewport, Projection))
		{
			const double ProjectionY = Projection.ProjectionMatrix.M[1][1];
			const int32 ViewHeight = Projection.GetConstrainedViewRect().Height();
			if (ProjectionY > UE_KINDA_SMALL_NUMBER && ViewHeight > 0)
			{
				return static_cast<float>(2.0 / (ProjectionY * ViewHeight));
			}
		}
	}

	// Fallback (no local player, e.g. editor preview): attached camera FOV across the game viewport width,
	// which is what the default horizontal-FOV constraint gives.
	FVector2D ViewportSize(1920.0, 1080.0);
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		if (Size.X > 0.0 && Size.Y > 0.0)
		{
			ViewportSize = Size;
		}
	}

	float CameraFOV = 90.f;
	for (const USceneComponent* Parent = GetAttachParent(); Parent; Parent = Parent->GetAttachParent())
	{
		if (const UCameraComponent* Camera = Cast<UCameraComponent>(Parent))
		{
			CameraFOV = Camera->FieldOfView;
			break;
		}
	}
	return static_cast<float>(2.0 * FMath::Tan(FMath::DegreesToRadians(CameraFOV * 0.5)) / ViewportSize.X);
}

void ULidarScanComponent::PushLiveParameters(const FLidarScanSlot& Slot) const
{
	UNiagaraComponent* Points = Slot.Points;
	if (!Points)
	{
		return;
	}

	FLinearColor Emissive = PointColor * FMath::Clamp(PointIntensity, 0.f, 4.f);
	Emissive.A = PointColor.A * Slot.GetFadeOutAlpha();

	Points->SetVariableFloat(LidarScanParams::PointPixelSize, FMath::Clamp(PointPixelSize, 0.5f, 8.f));
	Points->SetVariableFloat(LidarScanParams::ViewPixelScale, ViewPixelScale);
	Points->SetVariableFloat(LidarScanParams::SurfaceOffset, FMath::Clamp(SurfaceOffset, 0.f, 5.f));
	Points->SetVariableFloat(LidarScanParams::ViewOffsetMaxCm, FMath::Clamp(ViewOffsetMaxCm, 0.f, 10.f));
	Points->SetVariableLinearColor(LidarScanParams::PointColor, Emissive);
	Points->SetVariableFloat(LidarScanParams::FadeDuration, FadeDuration);
	Points->SetVariableFloat(LidarScanParams::AlphaFloor, AlphaFloor);
	Points->SetVariableFloat(LidarScanParams::DensityCompensation, FMath::Clamp(DensityCompensation, 0.f, 3.f));
	Points->SetVariableFloat(LidarScanParams::AngleCompensation, FMath::Clamp(AngleCompensation, 0.f, 1.f));
	Points->SetVariableFloat(LidarScanParams::DensityBoostMin, FMath::Clamp(DensityBoostMin, 0.01f, 1.f));
	Points->SetVariableFloat(LidarScanParams::DensityBoostMax, FMath::Clamp(DensityBoostMax, 1.f, 16.f));
}

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
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "SceneView.h"
#include "TextureResource.h"
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
 * Channels: the particle also reads G of the same texel (the jitter stays inside it and the texture is sampled nearest,
 * so it is the texel the depth came from, never filtered) and stores Particles.LidarChannel = G rounded to the nearest
 * integer if that is in [1, NumChannels], else 0 (the default channel). With channels off the target is R32f, G reads
 * 0 and every particle is in the default channel. With channels on the target is RGBA32f, filled by the capture's
 * post-process material ChannelCaptureMaterial (it replaces the tonemapper, so it is the last pass and writes the target
 * directly; the engine applies post-process materials only to a capture with a view state, so the channel capture keeps
 * one and marks every scan a camera cut): R = SceneDepth, the same planar depth as the depth-only capture, and G = CustomStencil where the custom
 * depth surface is the surface the sample hit (CustomDepth <= SceneDepth + max(1 cm, 0.1% of SceneDepth)), else 0.
 *
 * Spawn also reconstructs a surface normal, Particles.LidarNormal, from the depth gradient. The particle's own texel
 * (the jitter stays inside it, so nearest filtering reads that texel) and its four neighbours (+-1 texel per axis,
 * read at texel centres) are unprojected through their texel-centre rays; per axis the one-sided difference whose
 * depth is closer to the centre's is kept (a neighbour off the target edge, or with depth <= 1 or >= MaxRange, is
 * never kept). If even the kept neighbour differs from the centre depth by more than 0.25 x the centre depth, the
 * sample sits on a depth discontinuity (a one-texel-wide feature) and its normal is the capture ray. On a plane the
 * per-texel depth step only reaches 0.25 x depth at grazing angles under about 2 degrees (at a 256 capture over
 * about 100 degrees): a floor seen from eye height (1.6 m) farther than about 45 m, so with a long MaxRange the far
 * floor takes the capture ray as its normal. N =normalize(cross(Tangent X, Tangent Y)), then flipped if needed so that dot(N, CaptureOrigin - S) >= 0,
 * with S = LidarSurfacePosition: the normal points to the side of the surface the capture saw (the fallback capture
 * ray is flipped the same way). The angle term below only uses |dot|; the surface offset uses the sign.
 * A particle with LidarNormal = 0 (injected points, which have no normal) gets an angle term of 1.
 * e = 0.02 floors both cosines (same floor on both, so the term is exactly 1 from the scan's own pose): it only
 * guards the division and the pow; at that angle the clamp below already decides the result.
 *
 * Update (every frame): draw position, colour and size from the live parameters,
 *   Position = S + N * SurfaceOffset                                  if |N|^2 > 0.25,
 *   Position = S + normalize(ViewOrigin - S) * min(ViewOffsetMaxCm, 0.5 * |ViewOrigin - S|)   otherwise,
 *   rgb   = BaseColor.rgb * clamp(pow(SampleDepth / zs, DensityCompensation) * AngleTerm, DensityBoostMin, DensityBoostMax),
 *   BaseColor = ChannelColorK if LidarChannel = K in [1, NumChannels], PointColor otherwise (ChannelColor1..4, pushed
 *           live: rgb = Channels[K-1].Color.rgb * PointIntensity * Channels[K-1].IntensityMultiplier, a unused),
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
 * Both cosines of AngleTerm are absolute values, so a point seen from behind its own surface (through it, when points
 * behind geometry are shown) gets the same term as from the same angle in front. Size, compensation and offsets are
 * computed by the emitters, so every renderer and every point material of the system shows the same values.
 * It lifts sprites off their own surface so grazing views do not clip them; being a fixed world distance it never
 * carries a point through geometry thicker than the offset, whatever the viewing distance.
 * Compensation reads S, not the drawn position: SampleDepth is the depth of S along CaptureForward (raw depth minus
 * the bias it was placed with), so SampleDepth / zs is exactly 1 when the scan is viewed from its own pose.
 * PointColor.rgb already carries PointIntensity; the additive material multiplies rgb by alpha. The alpha of every
 * captured point, channel or not, is PointColor.a * fade: a channel changes the colour and intensity only.
 * ClearScans fades a scan out by scaling PointColor.a on that instance, so the system needs no extra parameter.
 *
 * Occlusion display. Each emitter has two sprite renderers with the same bindings. The first is the one described
 * above: material M_LidarPoint, depth tested, so a point behind real geometry is hidden; it never changes. The second
 * draws with User.PointMaterial (MaterialUserParamBinding; M_LidarPointOccluded when unset) and is enabled by the user
 * bool User.OcclusionDisplay (RendererEnabledBinding), false by default: disabled, it generates and draws nothing, so
 * with OccludedIntensity = 0 (the component never sets either parameter) the points, the materials and the cost are
 * those of a component without occlusion display. With OccludedIntensity > 0 every slot gets User.OcclusionDisplay =
 * true and User.PointMaterial = one dynamic instance of OccludedPointMaterial (M_LidarPointOccluded) shared by all slots;
 * both go back to false / unset when OccludedIntensity returns to 0. That material is unlit, additive, two-sided, used
 * with Niagara sprites and has no depth test; with Base = the M_LidarPoint emissive (Particles.Color.rgb *
 * Particles.Color.a * disc), Zp the sprite's pixel depth (sprites face the camera plane: the particle's planar view
 * depth) and Zs(uv) the scene depth (opaque geometry), with F = OccludedFactor and W = F * BehindWeight, it adds
 *   0                                      if W <= 0,
 * at F = 1 (OccludedIntensity 1, or any pulse during its link time):
 *   Base * BehindWeight                    if the fragment's device Z < the scene's device Z at the same pixel,
 *   0                                      otherwise,
 * the exact complement of the first renderer's depth test (reversed Z: it keeps device Z >= scene device Z), so the
 * two renderers together draw every sprite pixel of a BehindWeight-1 point once, whatever covers it: as if the points
 * had no depth test, with no margin and no edge band; and between 0 and 1 (F < 1):
 *   0                                      if Zp <= Zs(pixel) * 1.001 + 2 cm   (in front, or sunk into its own surface:
 *                                                                               left to the first renderer),
 *   Base * W                               if Zp >  Zmax + max(5 cm, 0.01 * Zp)          (behind geometry),
 *   0                                      otherwise,
 *   Zmax = max of Zs at the pixel and at +-3 and +-8 pixels along each screen axis,
 * so the pixels of points in view are exactly those of the first renderer (it never removes light), a point sitting on
 * a surface in view (whose surface reaches farther than the point within 8 pixels) is never treated as hidden, and
 * behind a wall every neighbour sees the wall. Below 1, points within 8 pixels of an occluder's edge, and points behind
 * geometry by less than max(5 cm, 1% of depth) (a wall thinner than that margin), are drawn by neither renderer: they
 * stay hidden. OccludedFactor
 * is a scalar parameter of the instance, pushed by the component every frame of the pulse: 1 for LinkSeconds after the
 * newest scan started (every alive scan uses the one instance, so the older scans pulse with the new one), then
 * linearly to OccludedIntensity over OcclusionFadeSeconds, on world time (the clock of the scans' age fade: it stands
 * still while the game is paused). BehindWeight is Particles.DynamicMaterialParameter.x: the Points emitter writes no
 * dynamic parameter, so the material reads its default 1; the Injected emitter writes (InjectedBehindWeight[i], 1, 1, 1)
 * at spawn, 1 for sources with FLidarSourceSettings::bShowBehindGeometry and 0 otherwise, so other injected points stay
 * hidden behind geometry at every moment of the pulse. A channel only changes Base, so a hidden point keeps its
 * channel's colour.
 *
 * Slots and the ghost cap: one instance holds exactly one scan. At most MaxAliveScans instances exist; when a scan
 * needs a slot and none is idle, the oldest scan's slot is reused and that scan is evicted: the component completes
 * the instance (DeactivateImmediate, which kills every particle of both emitters) and re-activates it for the new
 * scan in the same frame, so the evicted scan is gone by the frame the new one spawns. The system must not rely on a
 * reset (Activate(true)) to clear particles: a Niagara reset restarts the emitters but keeps live particles. Every
 * per-scan value (Capture*, TanHalfFov, ScanRealAge, the Injected arrays) therefore always describes the only scan
 * in the instance. Neither emitter needs anything for this: removal is the same rule for both.
 * All MaxAliveScans slots are created at BeginPlay and each is activated once with PointCount = InjectedPointCount = 0
 * for a short warm-up, then released: the instance and its texture binding exist before the first scan uses the slot.
 * The system must therefore spawn nothing when both counts are 0.
 *
 * Injected points (not produced by the depth capture) are never in a channel: their colour is their source's.
 * Sources are gathered by priority, higher first, ties in registration order; once MaxInjectedPointsPerScan points
 * are taken the rest are dropped (counted in FLidarInjectionStats::OverCap, one warning per scan that drops any).
 *
 * Injected points (not produced by the depth capture) write LidarSurfacePosition and Position, and LidarNormal = 0:
 * they get the toward-viewer offset capped by ViewOffsetMaxCm and an angle term of 1. MinSampleDepth and the near
 * thinning are capture-sampling rules and do not apply to them.
 *
 * Injected emitter (second emitter of the same system, so it shares the instance: ghost cap, ClearScans, eviction).
 * Spawn (once per activation): burst of InjectedPointCount particles; particle i reads InjectedPositions[i] (P),
 * InjectedColors[i], InjectedParams[i] = (FadeWeight, OcclusionTolerance), InjectedTransient[i] = (Hold, Vanish) and
 * InjectedBehindWeight[i] (stored as Particles.LidarBehindWeight; Particles.DynamicMaterialParameter = (it, 1, 1, 1),
 * see Occlusion display above).
 * With d = dot(P - CaptureOrigin,
 * CaptureForward), u' = dot(P - CaptureOrigin, CaptureRight) / (d * TanHalfFov.x), v' = dot(P - CaptureOrigin,
 * CaptureUp) / (d * TanHalfFov.y), the point reads the depth texel at UV = ((u' + 1) / 2, (1 - v') / 2) (same
 * texel-to-ray convention as the captured emitter, nearest texel) and is killed if OcclusionTolerance >= 0,
 * 1 < Depth < MaxRange and Depth < d - OcclusionTolerance: the capture saw a surface in front of it. Points with
 * OcclusionTolerance < 0 skip the test. (Points outside the coverage of an occluding source are already removed on the
 * CPU, so u', v' are in [-1, 1] whenever the test runs.) The particle stores LidarSurfacePosition = Position = P,
 * LidarNormal = 0 and a raw depth of d + SurfaceBias, so SampleDepth = d and the update's depth term is exactly 1
 * from the scan's own pose, as for captured points.
 * Update: as above, except
 *   rgb   = InjectedColors[i].rgb * InjectedIntensity * clamp(compensation, DensityBoostMin, DensityBoostMax),
 *   alpha = InjectedColors[i].a * FadeOutAlpha * lerp(1, lerp(1, AlphaFloor, Age / FadeDuration), FadeWeight)
 *           * TransientAlpha,
 *   TransientAlpha = saturate(1 - (ScanRealAge - Hold) / max(Vanish, 0.001))  if Hold >= 0     (Transient),
 *   TransientAlpha = saturate(1 - SupersededAge / max(Vanish, 0.001))         if Hold < -1.5   (UntilNextScan),
 *   TransientAlpha = 1                                                        otherwise (Hold = -1),
 * and a particle with Hold < -1.5 is killed (DataInstance.Alive = false) once SupersededAge >= max(Vanish, 0.001).
 * FadeWeight 0 skips the age fade but not the ClearScans fade-out. ELidarFadeMode maps to
 * ScanFade = (FadeWeight 1, Hold -1), None = (0, -1), Transient = (0, HoldSeconds) with Vanish = VanishSeconds,
 * UntilNextScan = (0, -2) with Vanish = VanishSeconds.
 * ScanRealAge is the real time since the scan, pushed live, so a transient point is timed from its own scan only.
 * SupersededAge is the real time since a later scan of the same component started, pushed live; it is -1 while the
 * instance holds the newest scan (TransientAlpha 1, never killed). Captured points read neither value.
 * InjectedIntensity is PointIntensity and FadeOutAlpha the ClearScans multiplier, both pushed live.
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

	static const FName InjectedPointCount(TEXT("User.InjectedPointCount"));
	static const FName InjectedPositions(TEXT("User.InjectedPositions"));
	static const FName InjectedColors(TEXT("User.InjectedColors"));
	static const FName InjectedParams(TEXT("User.InjectedParams"));
	static const FName InjectedTransient(TEXT("User.InjectedTransient"));
	static const FName ScanRealAge(TEXT("User.ScanRealAge"));
	static const FName SupersededAge(TEXT("User.SupersededAge"));
	static const FName InjectedIntensity(TEXT("User.InjectedIntensity"));
	static const FName FadeOutAlpha(TEXT("User.FadeOutAlpha"));
	static const FName InjectedBehindWeight(TEXT("User.InjectedBehindWeight"));
	static const FName PointMaterial(TEXT("User.PointMaterial"));
	static const FName OcclusionDisplay(TEXT("User.OcclusionDisplay"));

	/** Scalar parameter of OccludedPointMaterial that carries the pulse factor. */
	static const FName OccludedFactorParameter(TEXT("OccludedFactor"));

	/** Colour of channels 1 to 4 (ULidarScanComponent::NumChannels). */
	static const FName ChannelColors[] = {
		FName(TEXT("User.ChannelColor1")), FName(TEXT("User.ChannelColor2")),
		FName(TEXT("User.ChannelColor3")), FName(TEXT("User.ChannelColor4")) };
	static_assert(UE_ARRAY_COUNT(ChannelColors) == ULidarScanComponent::NumChannels, "One user parameter per channel.");

	/** Emitter names in the point system, used only by GetScanSlotStats. */
	static const FName CapturedEmitterName(TEXT("Points"));
	static const FName InjectedEmitterName(TEXT("Injected"));
}

bool FLidarScanContext::IsInCoverage(const FVector& WorldPosition, double& OutDepth) const
{
	const FVector Offset = WorldPosition - CaptureOrigin;
	OutDepth = FVector::DotProduct(Offset, CaptureForward);
	if (OutDepth <= 0.0 || OutDepth >= MaxRange)
	{
		return false;
	}
	return FMath::Abs(FVector::DotProduct(Offset, CaptureRight)) <= OutDepth * TanHalfFOV.X
		&& FMath::Abs(FVector::DotProduct(Offset, CaptureUp)) <= OutDepth * TanHalfFOV.Y;
}

ULidarScanComponent::ULidarScanComponent()
{
	// Ticks only while scans are alive: keeps the view pixel scale current and runs ClearScans fade-outs.
	// Post update work: the player camera has been updated for this frame.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PointSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/LidarScan/NS_LidarPoints.NS_LidarPoints")));
	ChannelCaptureMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/LidarScan/M_LidarDepthStencil.M_LidarDepthStencil")));
	OccludedPointMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/LidarScan/M_LidarPointOccluded.M_LidarPointOccluded")));
	Channels.SetNum(NumChannels);
}

namespace LidarScanCapture
{
	/** Show flags of the depth-only capture: drop everything that cannot change the depth buffer, never our own points. */
	static void SetDepthOnlyFlags(FEngineShowFlags& Flags)
	{
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
	}

	/**
	 * Show flags of the channel capture: the depth-only flags, unlit, and post-processing on only so that the channel
	 * material (which replaces the tonemapper) runs; every other post-process pass that can be turned off is off.
	 */
	static void SetChannelFlags(FEngineShowFlags& Flags)
	{
		SetDepthOnlyFlags(Flags);
		Flags.SetLighting(false);
		Flags.SetPostProcessing(true);
		Flags.SetDepthOfField(false);
		Flags.SetLocalExposure(false);
		Flags.SetVignette(false);
		Flags.SetToneCurve(false);
	}

	/** r.CustomDepth = 3: custom depth with stencil writes. */
	static bool IsCustomStencilEnabled()
	{
		static const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
		return CVar && CVar->GetInt() == 3;
	}
}

void ULidarScanComponent::BeginPlay()
{
	Super::BeginPlay();
	if (EnsureInitialized())
	{
		PrepareSlots();
	}
}

namespace LidarScanWarmUp
{
	/** Real seconds a new slot stays activated with nothing to spawn before it is released. */
	static constexpr double Seconds = 0.25;
}

void ULidarScanComponent::PrepareSlots()
{
	// A point system instance created in the frame of its scan binds the depth texture too late for its one-shot spawn
	// burst when frames are short (seen in packaged builds at several hundred frames per second): the burst samples an
	// unbound texture and every captured point dies, while injected points (which do not need the texture to spawn)
	// still show. Activating each slot once with nothing to spawn creates its instance and binds its texture ahead of
	// time; later activations reuse that instance, which is already bound.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetRealTimeSeconds() : 0.0;
	bool bWarming = false;
	while (Slots.Num() < FMath::Clamp(MaxAliveScans, 1, MaxAliveScansLimit))
	{
		const int32 Index = CreateSlot();
		if (!Slots.IsValidIndex(Index))
		{
			break;
		}
		FLidarScanSlot& Slot = Slots[Index];
		Slot.Points->SetVariableInt(LidarScanParams::PointCount, 0);
		Slot.Points->SetVariableInt(LidarScanParams::InjectedPointCount, 0);
		Slot.Points->Activate(true);
		Slot.WarmUntilRealTime = Now + LidarScanWarmUp::Seconds;
		bWarming = true;
	}
	if (bWarming)
	{
		SetComponentTickEnabled(true);
	}
}

int32 ULidarScanComponent::CreateSlot()
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

	const int32 Index = Slots.AddDefaulted();
	Slots[Index].Points = Points;
	bool bCreated = false;
	EnsureDepthTarget(Slots[Index], ResolveChannels(), bCreated);
	return Index;
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
			if (!bReportedMissingPointSystem)
			{
				bReportedMissingPointSystem = true;
				UE_LOG(LogLidarScan, Error, TEXT("%s: PointSystem '%s' could not be loaded, scans disabled."),
					*GetPathName(), *PointSystem.ToString());
			}
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
		LidarScanCapture::SetDepthOnlyFlags(DepthCapture->ShowFlags);
		bCaptureHasChannels = false;

		if (bHideOwnerInCapture)
		{
			DepthCapture->HiddenActors.Add(Owner);
		}

		DepthCapture->SetupAttachment(this);
		DepthCapture->RegisterComponent();
	}

	return true;
}

bool ULidarScanComponent::AreChannelsActive() const
{
	return bUseChannels && LidarScanCapture::IsCustomStencilEnabled() && (LoadedChannelMaterial || ChannelCaptureMaterial.Get());
}

bool ULidarScanComponent::ResolveChannels()
{
	if (!bUseChannels)
	{
		return false;
	}
	if (!LoadedChannelMaterial)
	{
		LoadedChannelMaterial = ChannelCaptureMaterial.LoadSynchronous();
		if (LoadedChannelMaterial)
		{
			// Shaders may be missing right after load (the editor compiles on demand; cooked builds ship them): compile
			// them now, once, rather than draw the capture with a fallback material.
			LoadedChannelMaterial->EnsureIsComplete();
		}
	}
	const bool bStencil = LidarScanCapture::IsCustomStencilEnabled();
	if (!bStencil || !LoadedChannelMaterial)
	{
		if (!bReportedChannelsUnavailable)
		{
			bReportedChannelsUnavailable = true;
			UE_LOG(LogLidarScan, Warning, TEXT("%s: channels are on but %s; scanning without channels."), *GetPathName(),
				!bStencil ? TEXT("the project has no custom depth with stencil (set r.CustomDepth=3)")
				: *FString::Printf(TEXT("ChannelCaptureMaterial '%s' could not be loaded"), *ChannelCaptureMaterial.ToString()));
		}
		return false;
	}
	// With missing shaders the engine would draw the capture with a fallback material and the scan would have no depth
	// at all: scan without channels until they exist.
	if (!LoadedChannelMaterial->IsComplete())
	{
		UE_LOG(LogLidarScan, Log, TEXT("%s: the channel material's shaders are not ready yet; this scan has no channels."), *GetName());
		return false;
	}
	return true;
}

void ULidarScanComponent::ConfigureCapture(bool bChannels)
{
	if (!DepthCapture || bChannels == bCaptureHasChannels)
	{
		return;
	}
	bCaptureHasChannels = bChannels;
	FPostProcessSettings& Settings = DepthCapture->PostProcessSettings;
	Settings.WeightedBlendables.Array.Reset();
	if (bChannels)
	{
		DepthCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
		LidarScanCapture::SetChannelFlags(DepthCapture->ShowFlags);
		Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, LoadedChannelMaterial));
		DepthCapture->PostProcessBlendWeight = 1.f;
		// The engine applies a post-process material only to a view with a view state; every channel scan is a camera
		// cut (see Scan), so the state never carries occlusion or history from one scan pose to the next.
		DepthCapture->bAlwaysPersistRenderingState = true;
	}
	else
	{
		// Exactly the capture a component without channels makes (its view state is released at the next capture).
		DepthCapture->bAlwaysPersistRenderingState = false;
		DepthCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		DepthCapture->ShowFlags = GetDefault<USceneCaptureComponent2D>()->ShowFlags;
		LidarScanCapture::SetDepthOnlyFlags(DepthCapture->ShowFlags);
	}
}

void ULidarScanComponent::SetChannel(int32 Channel, FLinearColor Color, float IntensityMultiplier)
{
	if (Channel < 1 || Channel > NumChannels)
	{
		return;
	}
	if (Channels.Num() != NumChannels)
	{
		Channels.SetNum(NumChannels);
	}
	Channels[Channel - 1].Color = Color;
	Channels[Channel - 1].IntensityMultiplier = FMath::Clamp(IntensityMultiplier, 0.f, 16.f);
	for (const FLidarScanSlot& Slot : Slots)
	{
		if (Slot.IsActive())
		{
			PushLiveParameters(Slot);
		}
	}
}

FLidarChannel ULidarScanComponent::GetChannel(int32 Channel) const
{
	return Channel >= 1 && Channel <= NumChannels && Channels.IsValidIndex(Channel - 1) ? Channels[Channel - 1] : FLidarChannel();
}

void ULidarScanComponent::Scan()
{
	if (!EnsureInitialized())
	{
		return;
	}

	TrimSlots();
	// Normally a no-op: the slots were prepared at BeginPlay. Tops the pool up after MaxAliveScans was raised.
	PrepareSlots();

	const int32 SlotIndex = AcquireSlot();
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	FLidarScanSlot& Slot = Slots[SlotIndex];
	// A warming slot is already activated with nothing to spawn: the activation below re-runs it for this scan.
	Slot.WarmUntilRealTime = -1.0;
	if (Slot.IsActive())
	{
		// Reusing the slot of a live scan: that scan is evicted. Re-activating with a reset alone would keep its
		// particles (a Niagara system reset restarts the emitters but keeps live particles), so the slot would keep
		// every scan it ever held. Completing the instance kills the particles of both emitters now; the new scan's
		// spawn below re-activates it.
		ReleaseSlot(Slot);
	}

	const bool bChannels = ResolveChannels();
	ConfigureCapture(bChannels);
	bool bNewTarget = false;
	UTextureRenderTarget2D* Target = EnsureDepthTarget(Slot, bChannels, bNewTarget);
	UNiagaraComponent* Points = Slot.Points;
	if (!Target || !Points)
	{
		return;
	}

	const float Range = FMath::Clamp(MaxRange, 100.f, 30000.f);
	const float HorizontalFOV = ResolveHorizontalFOV();
	DepthCapture->FOVAngle = HorizontalFOV;
	DepthCapture->MaxViewDistanceOverride = Range;
	DepthCapture->TextureTarget = Target;
	if (bCaptureHasChannels)
	{
		DepthCapture->bCameraCutThisFrame = true;
	}
	DepthCapture->CaptureScene();

	// Basis of the view that was just rendered: X forward, Y screen right, Z screen up.
	const FTransform CaptureTransform = DepthCapture->GetComponentTransform();
	const FVector Origin = CaptureTransform.GetLocation();
	const double TanX = FMath::Tan(FMath::DegreesToRadians(HorizontalFOV * 0.5));
	const double TanY = TanX * static_cast<double>(Target->SizeY) / static_cast<double>(Target->SizeX);

	// Points are simulated in world space; the component only anchors the fixed bounds.
	Points->SetWorldLocationAndRotation(Origin, FRotator::ZeroRotator);
	Points->SetSystemFixedBounds(FBox(FVector(-Range), FVector(Range)));

	Points->SetVariableInt(LidarScanParams::PointCount, Target->SizeX * Target->SizeY);
	Points->SetVariableInt(LidarScanParams::CaptureWidth, Target->SizeX);
	Points->SetVariableInt(LidarScanParams::CaptureHeight, Target->SizeY);
	Points->SetVariablePosition(LidarScanParams::CaptureOrigin, Origin);
	Points->SetVariableVec3(LidarScanParams::CaptureForward, CaptureTransform.GetUnitAxis(EAxis::X));
	Points->SetVariableVec3(LidarScanParams::CaptureRight, CaptureTransform.GetUnitAxis(EAxis::Y));
	Points->SetVariableVec3(LidarScanParams::CaptureUp, CaptureTransform.GetUnitAxis(EAxis::Z));
	Points->SetVariableVec2(LidarScanParams::TanHalfFov, FVector2D(TanX, TanY));
	Points->SetVariableFloat(LidarScanParams::MaxRange, Range);
	Points->SetVariableFloat(LidarScanParams::SurfaceBias, SurfaceBias);
	Points->SetVariableFloat(LidarScanParams::SampleJitter, FMath::Clamp(SampleJitter, 0.f, 1.f));
	Points->SetVariableFloat(LidarScanParams::NearThinningDistance, FMath::Clamp(NearThinningDistance, 0.f, 1000.f));
	Points->SetVariableFloat(LidarScanParams::MinSampleDepth, FMath::Clamp(MinSampleDepth, 0.f, 200.f));

	// Deterministic per scan index: the same scan never changes while alive, consecutive scans fill each other's gaps.
	const int32 ScanIndex = ScanCount++;
	Points->SetVariableInt(LidarScanParams::JitterSeed, static_cast<int32>(HashCombineFast(GetTypeHash(ScanIndex), 0x9E3779B9u) & 0x7FFFFFFF));

	UWorld* World = GetWorld();
	Slot.Context.CaptureOrigin = Origin;
	Slot.Context.CaptureForward = CaptureTransform.GetUnitAxis(EAxis::X);
	Slot.Context.CaptureRight = CaptureTransform.GetUnitAxis(EAxis::Y);
	Slot.Context.CaptureUp = CaptureTransform.GetUnitAxis(EAxis::Z);
	Slot.Context.TanHalfFOV = FVector2D(TanX, TanY);
	Slot.Context.MaxRange = Range;
	Slot.Context.ScanIndex = ScanIndex;
	Slot.Context.Timestamp = World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;
	Slot.StartRealTime = World ? World->GetRealTimeSeconds() : 0.0;
	Slot.SupersededRealTime = -1.0;

	// Every other scan is no longer the newest: points that last until the next scan start vanishing now.
	for (FLidarScanSlot& Other : Slots)
	{
		if (&Other != &Slot && Other.IsActive() && Other.SupersededRealTime < 0.0)
		{
			Other.SupersededRealTime = Slot.StartRealTime;
			PushLiveParameters(Other);
		}
	}

	// Sources are gathered right after the capture, so their points describe the same instant as the depth samples.
	InjectPoints(Slot, Target);

	// A recycled slot may still be fading out from a previous ClearScans.
	Slot.FadeOutDuration = 0.f;
	Slot.FadeOutElapsed = 0.f;
	ViewPixelScale = ResolveViewPixelScale();
	PushLiveParameters(Slot);

	// The pulse: every alive scan shares the occluded material instance, so all of them restart it with this scan.
	PulseStartTime = World ? World->GetTimeSeconds() : 0.0;
	RefreshOcclusion();

	Slot.ScanIndex = ScanIndex;
	SetComponentTickEnabled(true);

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

	OnScanStarted.Broadcast(ScanIndex, Slot.Context.Timestamp);
}

void ULidarScanComponent::InjectPoints(const FLidarScanSlot& Slot, UTextureRenderTarget2D* Target)
{
	const FLidarScanContext& Context = Slot.Context;
	FLidarInjectionStats Stats;
	Stats.ScanIndex = Context.ScanIndex;

	InjectedPositions.Reset();
	InjectedColors.Reset();
	InjectedParams.Reset();
	InjectedTransient.Reset();
	InjectedBehindWeight.Reset();

	const int32 Cap = FMath::Clamp(MaxInjectedPointsPerScan, 0, 65536);
	bool bAnyOccluding = false;

	// A source may (un)register sources while gathering: iterate over a copy, highest priority first, ties in
	// registration order, so that the cap cuts the lowest priorities.
	PointSources.RemoveAll([](const FLidarPointSourceEntry& Entry) { return !Entry.Source.IsValid(); });
	TArray<FLidarPointSourceEntry> Sources = PointSources;
	Sources.StableSort([](const FLidarPointSourceEntry& A, const FLidarPointSourceEntry& B)
	{
		return A.Priority != B.Priority ? A.Priority > B.Priority : A.Order < B.Order;
	});

	TArray<FLidarInjectedPoint> Gathered;
	for (const FLidarPointSourceEntry& Entry : Sources)
	{
		UObject* Source = Entry.Source.Get();
		if (!Source || !Source->GetClass()->ImplementsInterface(ULidarPointSource::StaticClass()))
		{
			continue;
		}
		++Stats.Sources;

		const FLidarSourceSettings Settings = ILidarPointSource::Execute_GetLidarSourceSettings(Source);
		Gathered.Reset();
		ILidarPointSource::Execute_GatherLidarPoints(Source, Context, Gathered);
		Stats.Gathered += Gathered.Num();

		const float Multiplier = FMath::Clamp(Settings.IntensityMultiplier, 0.f, 16.f);
		const FVector2D Params(Settings.FadeMode == ELidarFadeMode::ScanFade ? 1.0 : 0.0,
			Settings.bOccludeByCapture ? FMath::Clamp(Settings.OcclusionTolerance, 0.f, 200.f) : -1.0);
		// (Hold, Vanish): Hold >= 0 Transient, -2 UntilNextScan, -1 neither (see the contract above).
		const float Vanish = FMath::Clamp(Settings.VanishSeconds, 0.f, 30.f);
		const FVector2D Transient = Settings.FadeMode == ELidarFadeMode::Transient
			? FVector2D(FMath::Clamp(Settings.HoldSeconds, 0.f, 30.f), Vanish)
			: Settings.FadeMode == ELidarFadeMode::UntilNextScan ? FVector2D(-2.0, Vanish) : FVector2D(-1.0, 0.0);
		const float BehindWeight = Settings.bShowBehindGeometry ? 1.f : 0.f;

		for (const FLidarInjectedPoint& Point : Gathered)
		{
			double Depth = 0.0;
			if (Settings.bOccludeByCapture && !Context.IsInCoverage(Point.WorldPosition, Depth))
			{
				++Stats.OutsideCoverage;
				continue;
			}
			if (InjectedPositions.Num() >= Cap)
			{
				++Stats.OverCap;
				continue;
			}

			FLinearColor Color = Settings.Color * (Multiplier * FMath::Clamp(Point.Intensity, 0.f, 16.f));
			Color.A = Settings.Color.A;
			InjectedPositions.Add(Point.WorldPosition);
			InjectedColors.Add(Color);
			InjectedParams.Add(Params);
			InjectedTransient.Add(Transient);
			InjectedBehindWeight.Add(BehindWeight);
			bAnyOccluding |= Settings.bOccludeByCapture;
		}
	}
	Stats.Uploaded = InjectedPositions.Num();

	if (UNiagaraComponent* Points = Slot.Points)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(Points, LidarScanParams::InjectedPositions, InjectedPositions);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColor(Points, LidarScanParams::InjectedColors, InjectedColors);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector2D(Points, LidarScanParams::InjectedParams, InjectedParams);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector2D(Points, LidarScanParams::InjectedTransient, InjectedTransient);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(Points, LidarScanParams::InjectedBehindWeight, InjectedBehindWeight);
		Points->SetVariableInt(LidarScanParams::InjectedPointCount, Stats.Uploaded);
	}

	if (bVerifyInjectedOcclusion && bAnyOccluding && Target)
	{
		Stats.BehindCapturedDepth = CountBehindCapturedDepth(Target, Context);
	}

	LastInjectionStats = Stats;
	if (Stats.OverCap > 0)
	{
		UE_LOG(LogLidarScan, Warning, TEXT("%s: scan %d dropped %d injected points over the cap of %d per scan (lowest priorities cut)."),
			*GetName(), Stats.ScanIndex, Stats.OverCap, Cap);
	}
	if (Stats.Gathered > 0)
	{
		UE_LOG(LogLidarScan, Log, TEXT("%s: scan %d injected points: %d sources, %d gathered, %d outside coverage, %d over cap, %d uploaded, %d behind captured depth%s."),
			*GetName(), Stats.ScanIndex, Stats.Sources, Stats.Gathered, Stats.OutsideCoverage, Stats.OverCap, Stats.Uploaded,
			Stats.BehindCapturedDepth, Stats.BehindCapturedDepth < 0 ? TEXT(" (not verified)") : TEXT(""));
	}
}

int32 ULidarScanComponent::CountBehindCapturedDepth(UTextureRenderTarget2D* Target, const FLidarScanContext& Context) const
{
	FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
	TArray<FLinearColor> Pixels;
	if (!Resource || !Resource->ReadLinearColorPixels(Pixels))
	{
		return -1;
	}

	const int32 Width = Target->SizeX;
	const int32 Height = Target->SizeY;
	if (Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
	{
		return -1;
	}

	// Same rule as the GPU spawn of the injected emitter (see the contract at the top of this file).
	int32 Behind = 0;
	for (int32 Index = 0; Index < InjectedPositions.Num(); ++Index)
	{
		const double Tolerance = InjectedParams[Index].Y;
		if (Tolerance < 0.0)
		{
			continue;
		}
		const FVector Offset = InjectedPositions[Index] - Context.CaptureOrigin;
		const double PointDepth = FVector::DotProduct(Offset, Context.CaptureForward);
		if (PointDepth <= 0.0)
		{
			continue;
		}
		const double U = FVector::DotProduct(Offset, Context.CaptureRight) / (PointDepth * Context.TanHalfFOV.X);
		const double V = FVector::DotProduct(Offset, Context.CaptureUp) / (PointDepth * Context.TanHalfFOV.Y);
		const int32 X = FMath::Clamp(FMath::FloorToInt32((U + 1.0) * 0.5 * Width), 0, Width - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt32((1.0 - V) * 0.5 * Height), 0, Height - 1);
		const double Captured = Pixels[Y * Width + X].R;
		if (Captured > 1.0 && Captured < Context.MaxRange && Captured < PointDepth - Tolerance)
		{
			++Behind;
		}
	}
	return Behind;
}

void ULidarScanComponent::RegisterPointSource(TScriptInterface<ILidarPointSource> Source, int32 Priority)
{
	UObject* Object = Source.GetObject();
	if (!Object || !Object->GetClass()->ImplementsInterface(ULidarPointSource::StaticClass()))
	{
		return;
	}
	PointSources.RemoveAll([](const FLidarPointSourceEntry& Existing) { return !Existing.Source.IsValid(); });
	for (FLidarPointSourceEntry& Existing : PointSources)
	{
		if (Existing.Source.Get() == Object)
		{
			Existing.Priority = Priority;
			return;
		}
	}
	FLidarPointSourceEntry& Entry = PointSources.AddDefaulted_GetRef();
	Entry.Source = Object;
	Entry.Priority = Priority;
	Entry.Order = NextSourceOrder++;
}

void ULidarScanComponent::UnregisterPointSource(TScriptInterface<ILidarPointSource> Source)
{
	const UObject* Object = Source.GetObject();
	PointSources.RemoveAll([Object](const FLidarPointSourceEntry& Existing) { return !Existing.Source.IsValid() || Existing.Source.Get() == Object; });
}

int32 ULidarScanComponent::GetPointSourceCount() const
{
	int32 Count = 0;
	for (const FLidarPointSourceEntry& Entry : PointSources)
	{
		Count += Entry.Source.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 ULidarScanComponent::GetPointSourcePriority(TScriptInterface<ILidarPointSource> Source) const
{
	const UObject* Object = Source.GetObject();
	for (const FLidarPointSourceEntry& Entry : PointSources)
	{
		if (Object && Entry.Source.Get() == Object)
		{
			return Entry.Priority;
		}
	}
	return 0;
}

TArray<FLidarScanContext> ULidarScanComponent::GetAliveScans() const
{
	TArray<FLidarScanContext> Result;
	for (const FLidarScanSlot& Slot : Slots)
	{
		if (Slot.IsActive() && !Slot.IsFadingOut())
		{
			Result.Add(Slot.Context);
		}
	}
	Result.Sort([](const FLidarScanContext& A, const FLidarScanContext& B) { return A.ScanIndex < B.ScanIndex; });
	return Result;
}

TArray<FLidarScanSlotStats> ULidarScanComponent::GetScanSlotStats() const
{
	TArray<FLidarScanSlotStats> Result;
	Result.Reserve(Slots.Num());
	for (const FLidarScanSlot& Slot : Slots)
	{
		FLidarScanSlotStats& Stats = Result.AddDefaulted_GetRef();
		Stats.ScanIndex = Slot.ScanIndex;
		Stats.bFadingOut = Slot.IsFadingOut();

		const UNiagaraComponent* Points = Slot.Points;
		const FNiagaraSystemInstanceControllerConstPtr Controller = Points ? Points->GetSystemInstanceController() : nullptr;
		const FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
		if (!Instance)
		{
			continue;
		}
		for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
		{
			const FName Name = Emitter->GetEmitterHandle().GetName();
			const int32 Count = Emitter->IsComplete() ? 0 : Emitter->GetNumParticles();
			if (Name == LidarScanParams::InjectedEmitterName)
			{
				Stats.InjectedParticles += Count;
			}
			else if (Name == LidarScanParams::CapturedEmitterName)
			{
				Stats.CapturedParticles += Count;
			}
		}
	}
	return Result;
}

bool ULidarScanComponent::ReadNewestScanDepth(TArray<float>& OutDepth, int32& OutWidth, int32& OutHeight, FLidarScanContext& OutContext) const
{
	OutDepth.Reset();
	OutWidth = 0;
	OutHeight = 0;
	const FLidarScanSlot* Newest = nullptr;
	for (const FLidarScanSlot& Slot : Slots)
	{
		if (Slot.IsActive() && !Slot.IsFadingOut() && Slot.DepthTarget && (!Newest || Slot.ScanIndex > Newest->ScanIndex))
		{
			Newest = &Slot;
		}
	}
	FTextureRenderTargetResource* Resource = Newest ? Newest->DepthTarget->GameThread_GetRenderTargetResource() : nullptr;
	TArray<FLinearColor> Pixels;
	if (!Resource || !Resource->ReadLinearColorPixels(Pixels))
	{
		return false;
	}
	const int32 Width = Newest->DepthTarget->SizeX;
	const int32 Height = Newest->DepthTarget->SizeY;
	if (Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
	{
		return false;
	}
	OutDepth.SetNumUninitialized(Width * Height);
	for (int32 Index = 0; Index < Width * Height; ++Index)
	{
		OutDepth[Index] = Pixels[Index].R;
	}
	OutWidth = Width;
	OutHeight = Height;
	OutContext = Newest->Context;
	return true;
}

void ULidarScanComponent::ActivateScanPoints(int32 ScanIndex)
{
	for (const FLidarScanSlot& Slot : Slots)
	{
		if (Slot.ScanIndex == ScanIndex && Slot.Points)
		{
			// Re-runs the one-shot spawn burst. The slot holds no particles here: it is new, was released by ClearScans,
			// or its previous scan was evicted by Scan(); a reset alone would not remove them.
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
			// A warming slot still needs the tick that releases it.
			bAnyActive |= Slot.IsWarming();
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

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetRealTimeSeconds() : 0.0;

	bool bAnyActive = false;
	for (FLidarScanSlot& Slot : Slots)
	{
		if (Slot.IsWarming())
		{
			if (Now >= Slot.WarmUntilRealTime)
			{
				ReleaseSlot(Slot);
			}
			else
			{
				bAnyActive = true;
			}
			continue;
		}
		if (!Slot.IsFadingOut())
		{
			// Transient injected points are timed from their own scan, points that last until the next scan from the
			// scan that superseded theirs, both on real time: the only live values a steady scan needs every frame.
			if (Slot.IsActive() && Slot.Points)
			{
				Slot.Points->SetVariableFloat(LidarScanParams::ScanRealAge, static_cast<float>(FMath::Max(Now - Slot.StartRealTime, 0.0)));
				Slot.Points->SetVariableFloat(LidarScanParams::SupersededAge, Slot.SupersededRealTime < 0.0
					? -1.f : static_cast<float>(FMath::Max(Now - Slot.SupersededRealTime, 0.0)));
			}
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
	PushOccludedFactor();
}

void ULidarScanComponent::SetOcclusion(float InOccludedIntensity, float InLinkSeconds, float InFadeSeconds)
{
	OccludedIntensity = FMath::Clamp(InOccludedIntensity, 0.f, 1.f);
	LinkSeconds = FMath::Clamp(InLinkSeconds, 0.f, 10.f);
	OcclusionFadeSeconds = FMath::Clamp(InFadeSeconds, 0.f, 10.f);
	RefreshOcclusion();
}

bool ULidarScanComponent::IsOcclusionDisplayActive() const
{
	return OccludedIntensity > 0.f && OccludedMaterialInstance != nullptr;
}

float ULidarScanComponent::GetOccludedFactor() const
{
	const float Intensity = FMath::Clamp(OccludedIntensity, 0.f, 1.f);
	if (Intensity <= 0.f)
	{
		return 0.f;
	}
	const UWorld* World = GetWorld();
	if (PulseStartTime < 0.0 || !World)
	{
		return Intensity;
	}
	const double Age = World->GetTimeSeconds() - PulseStartTime;
	const double Link = FMath::Clamp(LinkSeconds, 0.f, 10.f);
	const double Fade = FMath::Clamp(OcclusionFadeSeconds, 0.f, 10.f);
	if (Age < Link)
	{
		return 1.f;
	}
	if (Fade <= 0.0)
	{
		return Intensity;
	}
	const float Alpha = static_cast<float>(FMath::Clamp((Age - Link) / Fade, 0.0, 1.0));
	return FMath::Lerp(1.f, Intensity, Alpha);
}

int32 ULidarScanComponent::GetOccludedMaterialSlotCount() const
{
	int32 Count = 0;
	for (const FLidarScanSlot& Slot : Slots)
	{
		Count += Slot.bOccludedMaterial ? 1 : 0;
	}
	return Count;
}

bool ULidarScanComponent::ResolveOcclusion()
{
	if (OccludedIntensity <= 0.f)
	{
		return false;
	}
	if (!OccludedMaterialInstance)
	{
		if (!LoadedOccludedMaterial)
		{
			LoadedOccludedMaterial = OccludedPointMaterial.LoadSynchronous();
			if (LoadedOccludedMaterial)
			{
				// As for the channel material: the editor compiles shaders on demand, so compile them once now.
				LoadedOccludedMaterial->EnsureIsComplete();
			}
		}
		if (!LoadedOccludedMaterial)
		{
			if (!bReportedOcclusionUnavailable)
			{
				bReportedOcclusionUnavailable = true;
				UE_LOG(LogLidarScan, Warning, TEXT("%s: OccludedIntensity is above 0 but OccludedPointMaterial '%s' could not be loaded; points behind geometry stay hidden."),
					*GetPathName(), *OccludedPointMaterial.ToString());
			}
			return false;
		}
		OccludedMaterialInstance = UMaterialInstanceDynamic::Create(LoadedOccludedMaterial, this);
		PushedOccludedFactor = -1.f;
	}
	return OccludedMaterialInstance != nullptr;
}

void ULidarScanComponent::RefreshOcclusion()
{
	const bool bActive = ResolveOcclusion();
	for (FLidarScanSlot& Slot : Slots)
	{
		if (Slot.Points && Slot.bOccludedMaterial != bActive)
		{
			// Off: the second renderer is disabled and its material unset, exactly as without occlusion display.
			Slot.Points->SetVariableMaterial(LidarScanParams::PointMaterial, bActive ? OccludedMaterialInstance.Get() : nullptr);
			Slot.Points->SetVariableBool(LidarScanParams::OcclusionDisplay, bActive);
			Slot.bOccludedMaterial = bActive;
		}
	}
	PushOccludedFactor();
}

void ULidarScanComponent::PushOccludedFactor()
{
	if (!OccludedMaterialInstance || OccludedIntensity <= 0.f)
	{
		return;
	}
	const float Factor = GetOccludedFactor();
	if (Factor != PushedOccludedFactor)
	{
		OccludedMaterialInstance->SetScalarParameterValue(LidarScanParams::OccludedFactorParameter, Factor);
		PushedOccludedFactor = Factor;
	}
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
	Slot.SupersededRealTime = -1.0;
	Slot.FadeOutDuration = 0.f;
	Slot.FadeOutElapsed = 0.f;
	Slot.WarmUntilRealTime = -1.0;
}

void ULidarScanComponent::ApplyLiveSettings()
{
	TrimSlots();
	// An idle slot whose target does not match the next scan (channels switched) gets its new target now and is
	// warmed up (again) like a new slot, so its first scan never binds a target in its own frame (in packaged builds that
	// loses every captured point of that scan). Slots still warming from BeginPlay are converted too: channels are often
	// switched on by another component's BeginPlay, right after this one's.
	if (LoadedPointSystem && GetWorld() && GetWorld()->IsGameWorld())
	{
		const bool bChannels = ResolveChannels();
		const double Now = GetWorld()->GetRealTimeSeconds();
		bool bWarming = false;
		for (FLidarScanSlot& Slot : Slots)
		{
			if (Slot.IsActive() || !Slot.Points || Slot.bDepthTargetHasChannels == bChannels)
			{
				continue;
			}
			bool bCreated = false;
			EnsureDepthTarget(Slot, bChannels, bCreated);
			Slot.Points->SetVariableInt(LidarScanParams::PointCount, 0);
			Slot.Points->SetVariableInt(LidarScanParams::InjectedPointCount, 0);
			Slot.Points->Activate(true);
			Slot.WarmUntilRealTime = Now + LidarScanWarmUp::Seconds;
			bWarming = true;
		}
		if (bWarming)
		{
			SetComponentTickEnabled(true);
		}
	}
	for (const FLidarScanSlot& Slot : Slots)
	{
		PushLiveParameters(Slot);
	}
	RefreshOcclusion();
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

	// 2. A new slot while under the cap (only when PrepareSlots could not create it).
	if (Slots.Num() < FMath::Clamp(MaxAliveScans, 1, MaxAliveScansLimit))
	{
		return CreateSlot();
	}

	// 3. Reuse the slot of the oldest scan (Scan() evicts it).
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
	Slot.WarmUntilRealTime = -1.0;
}

void ULidarScanComponent::TrimSlots()
{
	const int32 Cap = FMath::Clamp(MaxAliveScans, 1, MaxAliveScansLimit);
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

UTextureRenderTarget2D* ULidarScanComponent::EnsureDepthTarget(FLidarScanSlot& Slot, bool bChannels, bool& bOutCreated)
{
	bOutCreated = false;
	const int32 Resolution = FMath::Clamp(CaptureResolution, 64, 1024);
	if (Slot.DepthTarget && Slot.DepthTarget->SizeX == Resolution && Slot.DepthTarget->SizeY == Resolution
		&& Slot.bDepthTargetHasChannels == bChannels)
	{
		return Slot.DepthTarget;
	}

	// Depth alone in R (R32f), or depth in R and the channel in G (RGBA32f, written by the channel material).
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
	Target->RenderTargetFormat = bChannels ? RTF_RGBA32f : RTF_R32f;
	Target->ClearColor = FLinearColor::Black; // depth 0 -> no point
	Target->bAutoGenerateMips = false;
	Target->Filter = TF_Nearest;
	Target->AddressX = TA_Clamp;
	Target->AddressY = TA_Clamp;
	Target->InitAutoFormat(Resolution, Resolution);
	Target->UpdateResourceImmediate(true);

	Slot.DepthTarget = Target;
	Slot.bDepthTargetHasChannels = bChannels;
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
	Points->SetVariableFloat(LidarScanParams::InjectedIntensity, FMath::Clamp(PointIntensity, 0.f, 4.f));
	Points->SetVariableFloat(LidarScanParams::FadeOutAlpha, Slot.GetFadeOutAlpha());
	for (int32 Index = 0; Index < NumChannels; ++Index)
	{
		const FLidarChannel Channel = Channels.IsValidIndex(Index) ? Channels[Index] : FLidarChannel();
		FLinearColor ChannelEmissive = Channel.Color * (FMath::Clamp(PointIntensity, 0.f, 4.f) * FMath::Clamp(Channel.IntensityMultiplier, 0.f, 16.f));
		ChannelEmissive.A = 1.f;
		Points->SetVariableLinearColor(LidarScanParams::ChannelColors[Index], ChannelEmissive);
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetRealTimeSeconds() : Slot.StartRealTime;
	Points->SetVariableFloat(LidarScanParams::ScanRealAge, static_cast<float>(FMath::Max(Now - Slot.StartRealTime, 0.0)));
	Points->SetVariableFloat(LidarScanParams::SupersededAge, Slot.SupersededRealTime < 0.0
		? -1.f : static_cast<float>(FMath::Max(Now - Slot.SupersededRealTime, 0.0)));
}

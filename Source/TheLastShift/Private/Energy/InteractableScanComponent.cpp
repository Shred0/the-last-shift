// SPDX-License-Identifier: MIT

#include "Energy/InteractableScanComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InteractableScanComponent)

namespace InteractableScan
{
	// Mirrors of the UPROPERTY clamps: Blueprint and C++ writes bypass ClampMin/ClampMax.
	static float Factor(float V) { return FMath::Clamp(V, 0.2f, 2.f); }
	static float Spacing(float V) { return FMath::Clamp(V, 1.f, 50.f); }
	static int32 MaxPoints(int32 V) { return FMath::Clamp(V, 0, 4096); }
	static float Jitter(float V) { return FMath::Clamp(V, 0.f, 0.5f); }
	static float Facing(float V) { return FMath::Clamp(V, 0.f, 0.9f); }
	static float Share(float V) { return FMath::Clamp(V, 0.f, 1.f); }

	/** Outline points sit this far inside the face, so the capture sees them on the face rather than past its edge. */
	static constexpr double OutlineInsetCm = 1.0;

	/** One box face facing the capture: centre, half-size vectors along its two axes, and the spacing chosen for it. */
	struct FFace
	{
		FVector Center;
		FVector HalfU;
		FVector HalfV;
		double Spacing = 0.0;
	};

	/** True when every corner lies on the outer side of one plane of the capture's coverage (the face cannot be seen). */
	static bool IsOutsideCoverage(const FLidarScanContext& Context, const FVector (&Corners)[4])
	{
		bool bBehind = true, bBeyond = true, bRight = true, bLeft = true, bUp = true, bDown = true;
		for (const FVector& Corner : Corners)
		{
			const FVector Rel = Corner - Context.CaptureOrigin;
			const double D = FVector::DotProduct(Rel, Context.CaptureForward);
			const double X = FVector::DotProduct(Rel, Context.CaptureRight);
			const double Y = FVector::DotProduct(Rel, Context.CaptureUp);
			bBehind &= D <= 0.0;
			bBeyond &= D >= Context.MaxRange;
			bRight &= X > D * Context.TanHalfFOV.X;
			bLeft &= -X > D * Context.TanHalfFOV.X;
			bUp &= Y > D * Context.TanHalfFOV.Y;
			bDown &= -Y > D * Context.TanHalfFOV.Y;
		}
		return bBehind || bBeyond || bRight || bLeft || bUp || bDown;
	}
}

UInteractableScanComponent::UInteractableScanComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UInteractableScanComponent::AddTarget(UPrimitiveComponent* Target)
{
	if (Target)
	{
		Targets.AddUnique(Target);
	}
}

TArray<UPrimitiveComponent*> UInteractableScanComponent::GetTargets() const
{
	TArray<UPrimitiveComponent*> Out;
	for (UPrimitiveComponent* Target : Targets)
	{
		if (Target)
		{
			Out.Add(Target);
		}
	}
	return Out;
}

void UInteractableScanComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureRegistered();
	// The player pawn may not be possessed yet on the first frame.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { EnsureRegistered(); }));
	}
}

void UInteractableScanComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unregister();
	Super::EndPlay(EndPlayReason);
}

void UInteractableScanComponent::EnsureRegistered()
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UScanDirectorComponent* NewDirector = Pawn ? Pawn->FindComponentByClass<UScanDirectorComponent>() : nullptr;
	if (NewDirector && NewDirector != Director.Get())
	{
		Unregister();
		Director = NewDirector;
		NewDirector->RegisterPointSource(TScriptInterface<ILidarPointSource>(this));
	}
}

void UInteractableScanComponent::Unregister()
{
	if (UScanDirectorComponent* Current = Director.Get())
	{
		Current->UnregisterPointSource(TScriptInterface<ILidarPointSource>(this));
	}
	Director.Reset();
}

void UInteractableScanComponent::GatherLidarPoints_Implementation(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints)
{
	using namespace InteractableScan;

	LastGatheredPoints = 0;
	LastGatheredScan = Context.ScanIndex;

	const UScanDirectorComponent* Dir = Director.Get();
	const AActor* Owner = GetOwner();
	const int32 Budget = MaxPoints(MaxPointsPerScan);
	if (!Dir || !Owner || Owner->IsHidden() || Budget <= 0)
	{
		return;
	}

	const float Factor = InteractableScan::Factor(SpacingFactor);
	const float Floor = Spacing(MinSpacing);
	const float MinCos = Facing(MinFacingCosine);

	// Faces of every visible target's bounding box that face the capture and may be in its coverage.
	TArray<FFace, TInlineAllocator<16>> Faces;
	double Estimated = 0.0;
	for (const UPrimitiveComponent* Target : Targets)
	{
		if (!Target || !Target->IsRegistered() || !Target->IsVisible() || Target->bHiddenInGame)
		{
			continue;
		}
		const FBox LocalBox = Target->CalcBounds(FTransform::Identity).GetBox();
		if (!LocalBox.IsValid)
		{
			continue;
		}
		const FTransform& ToWorld = Target->GetComponentTransform();
		const FVector LocalCenter = LocalBox.GetCenter();
		const FVector LocalExtent = LocalBox.GetExtent();

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 AxisU = (Axis + 1) % 3;
			const int32 AxisV = (Axis + 2) % 3;
			FVector HalfULocal = FVector::ZeroVector;
			FVector HalfVLocal = FVector::ZeroVector;
			HalfULocal[AxisU] = LocalExtent[AxisU];
			HalfVLocal[AxisV] = LocalExtent[AxisV];
			const FVector HalfU = ToWorld.TransformVector(HalfULocal);
			const FVector HalfV = ToWorld.TransformVector(HalfVLocal);

			for (const double Sign : { 1.0, -1.0 })
			{
				FVector Offset = FVector::ZeroVector;
				Offset[Axis] = Sign * LocalExtent[Axis];
				const FVector Center = ToWorld.TransformPosition(LocalCenter + Offset);
				const FVector Normal = ToWorld.TransformVector(Offset).GetSafeNormal();
				const FVector ToCapture = Context.CaptureOrigin - Center;
				const double Distance = ToCapture.Size();
				if (Normal.IsNearlyZero() || Distance < UE_KINDA_SMALL_NUMBER
					|| FVector::DotProduct(Normal, ToCapture) < MinCos * Distance)
				{
					continue;
				}

				const FVector Corners[4] = { Center + HalfU + HalfV, Center + HalfU - HalfV, Center - HalfU + HalfV, Center - HalfU - HalfV };
				if (IsOutsideCoverage(Context, Corners))
				{
					continue;
				}

				const double Depth = FVector::DotProduct(Center - Context.CaptureOrigin, Context.CaptureForward);
				FFace& Face = Faces.AddDefaulted_GetRef();
				Face.Center = Center;
				Face.HalfU = HalfU;
				Face.HalfV = HalfV;
				Face.Spacing = FMath::Max<double>(Floor, Factor * Dir->GetCaptureSampleSpacing(Context, static_cast<float>(FMath::Max(Depth, 1.0))));
				Estimated += 4.0 * HalfU.Size() * HalfV.Size() / FMath::Square(Face.Spacing);
			}
		}
	}
	if (Faces.Num() == 0)
	{
		return;
	}

	const float JitterAmount = InteractableScan::Jitter(Jitter);
	FRandomStream Rng(static_cast<int32>(HashCombine(GetTypeHash(GetUniqueID()), GetTypeHash(Context.ScanIndex))));
	const int32 Start = OutPoints.Num();
	const auto Used = [&OutPoints, Start]() { return OutPoints.Num() - Start; };

	// Outlines first: the four edges of every face, inset a little, at the face's fine spacing (widened evenly to fit).
	const int32 OutlineBudget = FMath::FloorToInt(Budget * Share(OutlineShare));
	if (OutlineBudget > 0)
	{
		double OutlineEstimate = 0.0;
		for (const FFace& Face : Faces)
		{
			OutlineEstimate += 4.0 * (Face.HalfU.Size() + Face.HalfV.Size()) / Face.Spacing;
		}
		const double OutlineWiden = OutlineEstimate > OutlineBudget ? OutlineEstimate / OutlineBudget : 1.0;
		for (const FFace& Face : Faces)
		{
			const double Spacing = Face.Spacing * OutlineWiden;
			const FVector InsetU = Face.HalfU.GetSafeNormal() * FMath::Min(OutlineInsetCm, Face.HalfU.Size());
			const FVector InsetV = Face.HalfV.GetSafeNormal() * FMath::Min(OutlineInsetCm, Face.HalfV.Size());
			const FVector HalfU = Face.HalfU - InsetU;
			const FVector HalfV = Face.HalfV - InsetV;
			// Edges along U at V = +-1, then edges along V at U = +-1.
			const FVector EdgeAxes[2] = { HalfU, HalfV };
			const FVector EdgeOffsets[2] = { HalfV, HalfU };
			for (int32 Edge = 0; Edge < 2; ++Edge)
			{
				const int32 Count = FMath::Max(1, FMath::RoundToInt(2.0 * EdgeAxes[Edge].Size() / Spacing));
				for (const double Side : { 1.0, -1.0 })
				{
					for (int32 I = 0; I < Count && Used() < OutlineBudget; ++I)
					{
						const double T = -1.0 + (2.0 * I + 1.0) / Count;
						OutPoints.Emplace(Face.Center + EdgeAxes[Edge] * T + EdgeOffsets[Edge] * Side, 1.f);
					}
				}
			}
		}
	}

	// Fill with the rest: widen every face's spacing by the same factor, so the density stays even across faces.
	const int32 FillBudget = Budget - Used();
	if (FillBudget <= 0)
	{
		LastGatheredPoints = Used();
		return;
	}
	const double Widen = Estimated > FillBudget ? FMath::Sqrt(Estimated / FillBudget) : 1.0;
	OutPoints.Reserve(Start + Budget);
	for (const FFace& Face : Faces)
	{
		const double Spacing = Face.Spacing * Widen;
		const int32 CountU = FMath::Max(1, FMath::RoundToInt(2.0 * Face.HalfU.Size() / Spacing));
		const int32 CountV = FMath::Max(1, FMath::RoundToInt(2.0 * Face.HalfV.Size() / Spacing));
		for (int32 I = 0; I < CountU; ++I)
		{
			for (int32 J = 0; J < CountV; ++J)
			{
				if (Used() >= Budget)
				{
					LastGatheredPoints = Used();
					return;
				}
				// Cell centres in [-1, 1], jittered inside the cell.
				const double U = -1.0 + (2.0 * I + 1.0 + 2.0 * JitterAmount * Rng.FRandRange(-1.f, 1.f)) / CountU;
				const double V = -1.0 + (2.0 * J + 1.0 + 2.0 * JitterAmount * Rng.FRandRange(-1.f, 1.f)) / CountV;
				OutPoints.Emplace(Face.Center + Face.HalfU * U + Face.HalfV * V, 1.f);
			}
		}
	}
	LastGatheredPoints = Used();
}

FLidarSourceSettings UInteractableScanComponent::GetLidarSourceSettings_Implementation() const
{
	FLidarSourceSettings Settings;
	Settings.Color = UTheLastShiftSettings::GetScanColors().Interactable;
	Settings.IntensityMultiplier = FMath::Clamp(IntensityMultiplier, 0.f, 16.f);
	// Same life as the captured points of the scan that showed them.
	Settings.FadeMode = ELidarFadeMode::ScanFade;
	Settings.bOccludeByCapture = true;
	Settings.OcclusionTolerance = FMath::Clamp(OcclusionTolerance, 0.f, 200.f);
	return Settings;
}

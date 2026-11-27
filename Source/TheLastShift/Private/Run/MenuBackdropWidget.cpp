// SPDX-License-Identifier: MIT

#include "Run/MenuBackdropWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "Math/RandomStream.h"
#include "Rendering/DrawElements.h"
#include "Run/RunActListWidget.h"
#include "Run/RunEndingWidget.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/RunSubsystem.h"
#include "Styling/CoreStyle.h"
#include "UI/StdScreenWidget.h"
#include "UI/StdTextSceneWidget.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MenuBackdropWidget)

namespace MenuBackdropDetail
{
	/** A direction on the unit sphere for a slot's life: the same values give the same field. */
	static FVector3f DirectionOf(int32 Seed, int32 Slot, int32 Life)
	{
		FRandomStream Stream(static_cast<int32>(HashCombineFast(HashCombineFast(GetTypeHash(Seed), GetTypeHash(Slot)), GetTypeHash(Life * 7919 + 13))));
		const float Z = Stream.FRandRange(-1.f, 1.f);
		const float Phi = Stream.FRandRange(0.f, 2.f * UE_PI);
		const float R = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z));
		return FVector3f(R * FMath::Cos(Phi), R * FMath::Sin(Phi), Z);
	}

	/** Quick at first, then settling. */
	static float EaseOut(float U)
	{
		U = FMath::Clamp(U, 0.f, 1.f);
		const float V = 1.f - U;
		return 1.f - V * V * V;
	}

	/** 1 at Age 0, falling to 0 at Seconds. */
	static float Glow(double Age, float Seconds)
	{
		if (Age < 0.0 || Age >= Seconds)
		{
			return 0.f;
		}
		const float U = 1.f - static_cast<float>(Age / Seconds);
		return U * U;
	}

	/** The structure's local point P turned by Spin about its upright axis (Y), leaned by Tilt, turned in the picture by Roll. */
	static FVector3f Turn(const FVector3f& P, double SpinRad, float TiltRad, float RollRad)
	{
		const float CS = static_cast<float>(FMath::Cos(SpinRad));
		const float SS = static_cast<float>(FMath::Sin(SpinRad));
		const FVector3f A(P.X * CS + P.Z * SS, P.Y, -P.X * SS + P.Z * CS);
		const float CT = FMath::Cos(TiltRad), ST = FMath::Sin(TiltRad);
		const FVector3f B(A.X, A.Y * CT - A.Z * ST, A.Y * ST + A.Z * CT);
		const float CR = FMath::Cos(RollRad), SR = FMath::Sin(RollRad);
		return FVector3f(B.X * CR - B.Y * SR, B.X * SR + B.Y * CR, B.Z);
	}
}

double MenuBackdropPicture::PulseStart(const FMenuBackdropTuning& Tuning, int32 PulseIndex)
{
	return Tuning.FirstPulseSeconds + static_cast<double>(PulseIndex - 1) * Tuning.PulseSeconds;
}

int32 MenuBackdropPicture::PulsesBy(const FMenuBackdropTuning& Tuning, double Seconds)
{
	if (Seconds < Tuning.FirstPulseSeconds)
	{
		return 0;
	}
	return FMath::FloorToInt32((Seconds - Tuning.FirstPulseSeconds) / Tuning.PulseSeconds) + 1;
}

FVector2f MenuBackdropPicture::StructureVertex(const FMenuBackdropTuning& Tuning, double SpinDegrees)
{
	const FVector3f P = MenuBackdropDetail::Turn(FVector3f(1.f, 0.f, 0.f), FMath::DegreesToRadians(SpinDegrees),
		FMath::DegreesToRadians(Tuning.TiltDegrees), FMath::DegreesToRadians(Tuning.RollDegrees));
	return FVector2f(P.X, P.Y);
}

double MenuBackdropPicture::SpinFromVertex(const FMenuBackdropTuning& Tuning, const FVector2f& Vertex)
{
	// Undo the roll, then the lean: the vertex (1, 0, 0) turned by Spin is (cos, 0, -sin), leaned: y = sin(Spin) sin(Tilt).
	const float Roll = FMath::DegreesToRadians(Tuning.RollDegrees);
	const float X = Vertex.X * FMath::Cos(Roll) + Vertex.Y * FMath::Sin(Roll);
	const float Y = -Vertex.X * FMath::Sin(Roll) + Vertex.Y * FMath::Cos(Roll);
	const float ST = FMath::Sin(FMath::DegreesToRadians(Tuning.TiltDegrees));
	const double SinSpin = FMath::Abs(ST) > 1.e-3f ? Y / ST : 0.0;
	return FMath::RadiansToDegrees(FMath::Atan2(SinSpin, static_cast<double>(X)));
}

void MenuBackdropPicture::Evaluate(const FMenuBackdropTuning& T, double Seconds, FMenuBackdropFrame& Out)
{
	using namespace MenuBackdropDetail;
	Out.Seconds = Seconds;
	Out.Points.Reset();
	Out.Particles.Reset();
	Out.ShownParticles = 0;

	const int32 Pulses = PulsesBy(T, Seconds);
	Out.PulseIndex = Pulses;
	Out.PulseStartSeconds = Pulses > 0 ? PulseStart(T, Pulses) : -1.0;

	// --- the particles: each slot's shells, from the field's edge inward, one step a pulse ---
	const float Step = T.StepShare;
	const float Edge = T.FieldRadius;
	const float Wave = T.WaveSpeed;
	for (int32 Slot = 0; Slot < T.ParticleCount; ++Slot)
	{
		FRandomStream Stream(static_cast<int32>(HashCombineFast(GetTypeHash(T.Seed), GetTypeHash(Slot) ^ 0x9E3779B9u)));
		const float Offset = Stream.FRand() * FMath::Min(Step, Edge - 1.f);
		const int32 Last = FMath::Max(0, FMath::FloorToInt32((Edge - Offset - 1.f) / Step));
		const int32 Cycle = Last + 1;
		const int32 Phase = Stream.RandRange(0, Cycle - 1);
		const auto Shell = [Edge, Offset, Step](int32 J) { return Edge - Offset - static_cast<float>(J) * Step; };

		int32 Hits = 0;
		if (Pulses > 0)
		{
			const int32 Before = (Pulses - 1 + Phase) % Cycle;
			const double Reached = PulseStart(T, Pulses) + Shell(Before) / Wave;
			Hits = Pulses - 1 + (Seconds >= Reached ? 1 : 0);
		}
		const int32 State = Hits + Phase;
		const int32 J = State % Cycle;
		const int32 Life = State / Cycle;

		FMenuBackdropParticle Particle;
		Particle.Life = Life;
		Particle.Hits = Hits;
		if (Hits == 0)
		{
			Particle.Distance = Shell(J);
			Particle.Position = DirectionOf(T.Seed, Slot, Life) * Particle.Distance;
			Particle.Level = T.ParticleRestLevel;
			FMenuBackdropPoint& P = Out.Points.AddDefaulted_GetRef();
			P.Position = FVector2f(Particle.Position.X, Particle.Position.Y);
			P.Distance = Particle.Distance;
			P.Level = Particle.Level;
			P.Slot = Slot;
		}
		else
		{
			const int32 PrevJ = (State - 1) % Cycle;
			const double Reached = PulseStart(T, Hits) + Shell(PrevJ) / Wave;
			const double Age = Seconds - Reached;
			const float Level = T.ParticleRestLevel + (T.ParticlePeakLevel - T.ParticleRestLevel) * Glow(Age, T.ParticleGlowSeconds);
			Particle.LastHitSeconds = Reached;
			if (PrevJ == Last)
			{
				// Its step would have taken it into the structure: it fades out where it stands, its successor fades in at the edge.
				Particle.bCameBack = true;
				const float U = FMath::Clamp(static_cast<float>(Age / T.FadeSeconds), 0.f, 1.f);
				const FVector3f OldAt = DirectionOf(T.Seed, Slot, Life - 1) * Shell(Last);
				const FVector3f NewAt = DirectionOf(T.Seed, Slot, Life) * Shell(0);
				if (U < 1.f)
				{
					FMenuBackdropPoint& P = Out.Points.AddDefaulted_GetRef();
					P.Position = FVector2f(OldAt.X, OldAt.Y);
					P.Distance = Shell(Last);
					P.Level = Level * (1.f - U);
					P.Slot = Slot;
					P.Weight = 1.f - U;
				}
				if (U > 0.f)
				{
					FMenuBackdropPoint& P = Out.Points.AddDefaulted_GetRef();
					P.Position = FVector2f(NewAt.X, NewAt.Y);
					P.Distance = Shell(0);
					P.Level = T.ParticleRestLevel * U;
					P.Slot = Slot;
					P.Weight = U;
					P.bArriving = true;
				}
				const bool bNewShown = U >= 0.5f;
				Particle.Position = bNewShown ? NewAt : OldAt;
				Particle.Distance = bNewShown ? Shell(0) : Shell(Last);
				Particle.Weight = bNewShown ? U : 1.f - U;
				Particle.Level = bNewShown ? T.ParticleRestLevel * U : Level * (1.f - U);
			}
			else
			{
				Particle.Distance = Shell(PrevJ) - Step * EaseOut(static_cast<float>(Age / T.StepSeconds));
				Particle.Position = DirectionOf(T.Seed, Slot, Life) * Particle.Distance;
				Particle.Level = Level;
				FMenuBackdropPoint& P = Out.Points.AddDefaulted_GetRef();
				P.Position = FVector2f(Particle.Position.X, Particle.Position.Y);
				P.Distance = Particle.Distance;
				P.Level = Level;
				P.Slot = Slot;
			}
		}
		Out.ShownParticles += Particle.Weight >= 0.5f ? 1 : 0;
		Out.Particles.Add(Particle);
	}

	// --- the structure: an octahedron of points along its twelve edges, turning about its upright axis ---
	static const FVector3f Vertices[6] = { FVector3f(1, 0, 0), FVector3f(-1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, -1, 0), FVector3f(0, 0, 1), FVector3f(0, 0, -1) };
	static const int32 Edges[12][2] = { {0, 2}, {0, 3}, {0, 4}, {0, 5}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {2, 4}, {2, 5}, {3, 4}, {3, 5} };
	Out.SpinDegrees = static_cast<double>(T.SpinDegreesPerSecond) * Seconds;
	const double SpinRad = FMath::DegreesToRadians(FMath::Fmod(Out.SpinDegrees, 360.0));
	const float TiltRad = FMath::DegreesToRadians(T.TiltDegrees);
	const float RollRad = FMath::DegreesToRadians(T.RollDegrees);
	Out.StructureLevel = T.StructureRestLevel
		+ (T.StructurePeakLevel - T.StructureRestLevel) * (Pulses > 0 ? Glow(Seconds - Out.PulseStartSeconds, T.StructureGlowSeconds) : 0.f);
	const int32 First = Out.Points.Num();
	const auto AddStructure = [&](const FVector3f& Local)
	{
		const FVector3f W = Turn(Local, SpinRad, TiltRad, RollRad);
		FMenuBackdropPoint& P = Out.Points.AddDefaulted_GetRef();
		P.Position = FVector2f(W.X, W.Y);
		P.Distance = W.Size();
		P.Level = Out.StructureLevel;
	};
	for (const FVector3f& V : Vertices)
	{
		AddStructure(V);
	}
	for (const auto& E : Edges)
	{
		for (int32 K = 1; K < T.PointsPerEdge - 1; ++K)
		{
			const float U = static_cast<float>(K) / static_cast<float>(T.PointsPerEdge - 1);
			AddStructure(FMath::Lerp(Vertices[E[0]], Vertices[E[1]], U));
		}
	}
	Out.StructurePoints = Out.Points.Num() - First;
}

// =====================================================================================================================

void UMenuBackdropWidget::SetMenu(UStdScreenWidget* InMenu)
{
	Menu = InMenu;
}

void UMenuBackdropWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	const UWorld* World = GetWorld();
	StartRealSeconds = World ? World->GetRealTimeSeconds() : 0.0;
}

void UMenuBackdropWidget::NativeDestruct()
{
	GiveScreensBack();
	Super::NativeDestruct();
}

double UMenuBackdropWidget::GetPictureSeconds() const
{
	if (bTimeOverride)
	{
		return OverrideSeconds;
	}
	const UWorld* World = GetWorld();
	return World && StartRealSeconds >= 0.0 ? FMath::Max(0.0, World->GetRealTimeSeconds() - StartRealSeconds) : 0.0;
}

void UMenuBackdropWidget::SetTimeOverride(double Seconds)
{
	OverrideSeconds = FMath::Max(0.0, Seconds);
	bTimeOverride = true;
}

void UMenuBackdropWidget::ClearTimeOverride()
{
	bTimeOverride = false;
}

int32 UMenuBackdropWidget::GetSeeThroughCount() const
{
	int32 Count = 0;
	for (const FSeeThrough& S : SeeThrough)
	{
		Count += S.Border.IsValid() ? 1 : 0;
	}
	return Count;
}

FString UMenuBackdropWidget::FindCover() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (const UStdFlowSubsystem* Flow = GameInstance ? GameInstance->GetSubsystem<UStdFlowSubsystem>() : nullptr; Flow && Flow->GetOverlayAlpha() >= 1.f)
	{
		return TEXT("travel screen");
	}
	if (const URunSubsystem* Run = URunSubsystem::Get(this); Run && Run->GetLossScreen() && Run->GetLossScreen()->IsInViewport())
	{
		return TEXT("loss screen");
	}
	UStdScreenWidget* Root = Menu.Get();
	if (!Root || !Root->IsInViewport())
	{
		return TEXT("no menu");
	}
	TArray<const UStdScreenWidget*, TInlineAllocator<8>> Stack;
	for (const UStdScreenWidget* Screen = Root->GetTopScreen(); Screen; Screen = Screen->GetParentScreen())
	{
		if (Screen->IsA<UStdTextSceneWidget>())
		{
			return TEXT("scene");
		}
		if (Screen->IsA<URunEndingWidget>())
		{
			return TEXT("ending");
		}
		Stack.Add(Screen);
		if (Screen == Root)
		{
			break;
		}
	}
	if (const URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(this); Scenes && Scenes->IsSceneScreenOpen())
	{
		return TEXT("scene");
	}
	// Any other screen shown on its own (a scene or the loss screen shown as a root, a dialog) covers the menu.
	for (TObjectIterator<UStdScreenWidget> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsInViewport() && !Stack.Contains(*It))
		{
			return It->IsA<UStdTextSceneWidget>() ? FString(TEXT("scene")) : FString::Printf(TEXT("screen %s"), *It->GetClass()->GetName());
		}
	}
	return FString();
}

void UMenuBackdropWidget::HoldScreensSeeThrough()
{
	UStdScreenWidget* Root = Menu.Get();
	if (!Root)
	{
		return;
	}
	for (UStdScreenWidget* Screen = Root->GetTopScreen(); Screen; Screen = Screen->GetParentScreen())
	{
		UBorder* Border = Screen->WidgetTree ? Cast<UBorder>(Screen->WidgetTree->FindWidget(TEXT("Border_Background"))) : nullptr;
		if (Border && Border->GetBrushColor().A != 0.f)
		{
			FSeeThrough* Held = SeeThrough.FindByPredicate([Border](const FSeeThrough& S) { return S.Border.Get() == Border; });
			if (!Held)
			{
				Held = &SeeThrough.AddDefaulted_GetRef();
				Held->Border = Border;
			}
			Held->Before = Border->GetBrushColor();
			FLinearColor Clear = Held->Before;
			Clear.A = 0.f;
			Border->SetBrushColor(Clear);
		}
		if (Screen == Root)
		{
			break;
		}
	}
}

void UMenuBackdropWidget::GiveScreensBack()
{
	for (const FSeeThrough& S : SeeThrough)
	{
		if (UBorder* Border = S.Border.Get())
		{
			Border->SetBrushColor(S.Before);
		}
	}
	SeeThrough.Reset();
}

void UMenuBackdropWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	CoverReason = FindCover();
	if (CoverReason.IsEmpty())
	{
		HoldScreensSeeThrough();
	}
}

int32 UMenuBackdropWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	++PaintCount;
	LastPaintFrame = GFrameCounter;
	// Asked again here: a widget just added can be painted once before its first tick (a menu shown under the full
	// cover of a travel screen).
	LastPaintCover = FindCover();
	LastPaintedPoints = 0;
	if (!LastPaintCover.IsEmpty())
	{
		return LayerId;
	}
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	const FMenuBackdropTuning Tuning = Settings->GetMenuBackdropTuning();
	MenuBackdropPicture::Evaluate(Tuning, GetPictureSeconds(), Frame);

	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	// The void: black, whatever the menu's world shows.
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor::Black);

	const float Scale = Tuning.StructureScreenShare * Size.Y;
	const float Side = FMath::Max(1.f, FMath::RoundToFloat(Tuning.PointSize * Size.Y / 1080.f));
	const FVector2f Centre = Size * 0.5f;
	const FLinearColor Tint = Settings->GetSurfaceVariant(true).Tint;
	const int32 PointLayer = LayerId + 1;
	int32 Drawn = 0;
	for (const FMenuBackdropPoint& Point : Frame.Points)
	{
		if (Point.Level <= 0.f)
		{
			continue;
		}
		const FVector2f At(FMath::RoundToFloat(Centre.X + Point.Position.X * Scale - 0.5f * Side), FMath::RoundToFloat(Centre.Y - Point.Position.Y * Scale - 0.5f * Side));
		if (At.X < -Side || At.Y < -Side || At.X > Size.X || At.Y > Size.Y)
		{
			continue;
		}
		const FLinearColor Color(Tint.R * Point.Level, Tint.G * Point.Level, Tint.B * Point.Level, 1.f);
		FSlateDrawElement::MakeBox(OutDrawElements, PointLayer, AllottedGeometry.ToPaintGeometry(FVector2f(Side, Side), FSlateLayoutTransform(At)), White,
			ESlateDrawEffect::None, Color);
		++Drawn;
	}
	LastPaintedPoints = Drawn;
	return PointLayer;
}

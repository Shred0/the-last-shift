// SPDX-License-Identifier: MIT

#include "Prologue/PrologueMonitorRoom.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Epilogue/ChamberBattery.h"
#include "Epilogue/EpilogueChamber.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Look/ScreenPlate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueMonitorRoom)

namespace MonitorRoomLayout
{
	// SM_MonitorWall, in its own frame (front +X): 24 monitors in 3 rows of 8, each screen the face at X -20 inside a 6 cm
	// bezel, 75.5 x 78 cm.
	static const float MonitorFaceX = -20.f;
	static const float MonitorColumnsY[] = { -346.25f, -253.75f, -146.25f, -53.75f, 53.75f, 146.25f, 253.75f, 346.25f };
	static const float MonitorRowsZ[] = { 140.f, 240.f, 340.f };
	static const FVector2D MonitorSize(75.3f, 77.8f);

	// SM_ComputeRack, in its own frame (front +X): six servers, each front the face at X 50, 30 cm tall, 62 cm wide; its
	// vent ribs and its handle leave a clear strip from Y -31 to -26 for the lights, one above the other.
	static const float ServerFaceX = 50.f;
	static const float ServerCentresZ[] = { 31.f, 68.f, 105.f, 142.f, 179.f, 216.f };
	static const float LightY = -28.5f;
	static const float LightRowSpacing = 5.f;
	static const float LightGap = 0.2f;

	// Brief 36B: SM_ControlBank, in its own frame (front +X; from its Blender source): two recessed screens 240 cm across,
	// the slanted desk's (60 cm along its slope) and the upright's (80 cm tall).
	static const FVector DeskCentre(4.28f, 0.f, 104.69f);
	static const FVector DeskNormal(0.4831f, 0.f, 0.8756f);
	static const FVector DeskUp(-0.8755f, 0.f, 0.4832f);
	static const FVector2D DeskSize(239.5f, 59.5f);
	static const FVector UprightCentre(-25.5f, 0.f, 167.f);
	static const FVector2D UprightSize(239.5f, 79.5f);

	/** A lit plate stands this far in front of its black plate (both in front of the screen's face). */
	static const float LitGapCm = 0.25f;

	/** The layouts of the lit screens: 0 a bar, 1 columns, 2 a gauge across, 3 a gauge up. */
	static const int32 LayoutCount = 4;

	/** A small deterministic hash in [0, 1) (the lights' periods, phases and colours: the same every run). */
	static float Hash01(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return static_cast<float>(X & 0xFFFFFF) / static_cast<float>(0x1000000);
	}
}

APrologueMonitorRoom::APrologueMonitorRoom()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	const auto Setup = [this](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		C->SetupAttachment(Root);
		// Placed in world space; shown or hidden with the time state; unlit, they need no light and light nothing.
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(false);
		C->bHiddenInSceneCapture = true;
		C->SetRenderCustomDepth(false);
		C->bAffectDistanceFieldLighting = false;
		return C;
	};
	LightsHome = Setup(TEXT("LightsHome"));
	LightsSecond = Setup(TEXT("LightsSecond"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		LightsHome->SetStaticMesh(CubeMesh.Object);
		LightsSecond->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Wall(TEXT("/Game/TheLastShift/Art/Prologue/SM_MonitorWall.SM_MonitorWall"));
	MonitorWallMesh = Wall.Succeeded() ? Wall.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Rack(TEXT("/Game/TheLastShift/Art/Prologue/SM_ComputeRack.SM_ComputeRack"));
	RackMesh = Rack.Succeeded() ? Rack.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Hologram(TEXT("/Game/TheLastShift/Materials/M_Hologram.M_Hologram"));
	LightMaterial = Hologram.Succeeded() ? Hologram.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Bank(TEXT("/Game/TheLastShift/Art/Prologue/SM_ControlBank.SM_ControlBank"));
	ControlBankMesh = Bank.Succeeded() ? Bank.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Screen(TEXT("/Game/TheLastShift/Art/Materials/M_BatteryScreen.M_BatteryScreen"));
	ScreenLitMaterial = Screen.Succeeded() ? Screen.Object : nullptr;
}

void APrologueMonitorRoom::BeginPlay()
{
	Super::BeginPlay();
	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &APrologueMonitorRoom::HandleTimeStateChanged);
	}
	// The battery's hall: the lit screens read the aftermath of the frame (the chamber's): tick after it, so that the last
	// monitor goes black on the aftermath's last frame, not one later.
	if (bBatteryScreens)
	{
		if (AEpilogueChamber* Chamber = AEpilogueChamber::Find(GetWorld()))
		{
			AddTickPrerequisiteActor(Chamber);
		}
	}
	Rebuild();
}

void APrologueMonitorRoom::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.RemoveDynamic(this, &APrologueMonitorRoom::HandleTimeStateChanged);
		TimeState = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void APrologueMonitorRoom::Rebuild()
{
	BuildPlates();
	BuildLights();
	UpdateVisibility();
	UpdateBlink(true);
}

TArray<UStaticMeshComponent*> APrologueMonitorRoom::GetPlates() const
{
	TArray<UStaticMeshComponent*> Out;
	for (UStaticMeshComponent* Plate : Plates)
	{
		if (Plate)
		{
			Out.Add(Plate);
		}
	}
	return Out;
}

void APrologueMonitorRoom::BuildPlates()
{
	using namespace MonitorRoomLayout;
	for (UStaticMeshComponent* Plate : Plates)
	{
		if (Plate)
		{
			Plate->DestroyComponent();
		}
	}
	Plates.Reset();
	for (UStaticMeshComponent* Plate : LitPlates)
	{
		if (Plate)
		{
			Plate->DestroyComponent();
		}
	}
	LitPlates.Reset();
	LitLayouts.Reset();
	LitOnBank.Reset();
	LitOrder.Reset();
	LayoutMids.Reset();
	if (bBatteryScreens && ScreenLitMaterial)
	{
		for (int32 L = 0; L < LayoutCount; ++L)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(ScreenLitMaterial, this);
			Mid->SetScalarParameterValue(TEXT("Layout"), static_cast<float>(L));
			LayoutMids.Add(Mid);
		}
	}
	UWorld* World = GetWorld();
	if (!World || !MonitorWallMesh)
	{
		return;
	}
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, ScreenPlate::PlanePath);
	TArray<UStaticMeshComponent*> Walls;
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* C = *It;
		if (C && C->GetWorld() == World && C->IsRegistered() && C->GetStaticMesh() == MonitorWallMesh && !Cast<UInstancedStaticMeshComponent>(C))
		{
			Walls.Add(C);
		}
	}
	for (UStaticMeshComponent* Wall : Walls)
	{
		AActor* WallOwner = Wall->GetOwner();
		if (!WallOwner)
		{
			continue;
		}
		int32 Index = 0;
		int32 Row = 0;
		for (const float Z : MonitorRowsZ)
		{
			int32 Column = 0;
			for (const float Y : MonitorColumnsY)
			{
				// A component of the actor that owns the screen, attached to the wall's mesh: its frame is the mesh's.
				UStaticMeshComponent* Plate = NewObject<UStaticMeshComponent>(WallOwner, *FString::Printf(TEXT("ScreenPlate_%s_%02d"), *Wall->GetName(), Index));
				ScreenPlate::Setup(Plate, Plane);
				Plate->SetMobility(Wall->Mobility);
				Plate->SetupAttachment(Wall);
				ScreenPlate::Place(Plate, FVector(MonitorFaceX, Y, Z), FVector::XAxisVector, FVector::ZAxisVector, MonitorSize);
				Plate->RegisterComponent();
				ScreenPlate::ApplyMaterial(Plate);
				WallOwner->AddInstanceComponent(Plate);
				Plates.Add(Plate);
				if (bBatteryScreens)
				{
					// The layouts shared out by the monitor's place (its column and row): the same every run.
					const int32 Layout = (Column * 3 + Row * 2 + static_cast<int32>(Hash01(static_cast<uint32>(Index * 7 + Row)) * 2.f)) % LayoutCount;
					BuildLitPlate(Wall, Layout, FVector(MonitorFaceX + LitGapCm, Y, Z), FVector::XAxisVector, FVector::ZAxisVector, MonitorSize, false);
				}
				++Index;
				++Column;
			}
			++Row;
		}
	}
	int32 Banks = 0;
	if (bBatteryScreens && ControlBankMesh)
	{
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			UStaticMeshComponent* Bank = *It;
			if (!Bank || Bank->GetWorld() != World || !Bank->IsRegistered() || Bank->GetStaticMesh() != ControlBankMesh || Cast<UInstancedStaticMeshComponent>(Bank) || !Bank->GetOwner())
			{
				continue;
			}
			AActor* BankOwner = Bank->GetOwner();
			struct FFace { FVector Centre; FVector Normal; FVector Up; FVector2D Size; int32 Layout; };
			const FFace Faces[] = {
				{ DeskCentre, DeskNormal, DeskUp, DeskSize, 2 },
				{ UprightCentre, FVector::XAxisVector, FVector::ZAxisVector, UprightSize, 1 },
			};
			int32 Index = 0;
			for (const FFace& F : Faces)
			{
				UStaticMeshComponent* Plate = NewObject<UStaticMeshComponent>(BankOwner, *FString::Printf(TEXT("ScreenPlate_%s_%02d"), *Bank->GetName(), Index++));
				ScreenPlate::Setup(Plate, Plane);
				Plate->SetMobility(Bank->Mobility);
				Plate->SetupAttachment(Bank);
				ScreenPlate::Place(Plate, F.Centre, F.Normal, F.Up, F.Size);
				Plate->RegisterComponent();
				ScreenPlate::ApplyMaterial(Plate);
				BankOwner->AddInstanceComponent(Plate);
				Plates.Add(Plate);
				BuildLitPlate(Bank, F.Layout, F.Centre + F.Normal.GetSafeNormal() * LitGapCm, F.Normal, F.Up, F.Size, true);
			}
			++Banks;
		}
	}
	// The lit plates' order across the wall: along the line through the first and the last of them, 0 to 1.
	if (LitPlates.Num() > 1)
	{
		FVector A = LitPlates[0]->GetComponentLocation();
		FVector B = A;
		for (const UStaticMeshComponent* P : LitPlates)
		{
			const FVector L = P->GetComponentLocation();
			A = FVector(FMath::Min(A.X, L.X), FMath::Min(A.Y, L.Y), 0.f);
			B = FVector(FMath::Max(B.X, L.X), FMath::Max(B.Y, L.Y), 0.f);
		}
		const FVector Axis = (B - A).GetSafeNormal2D();
		const float Span = FMath::Max(static_cast<float>(FVector::DotProduct(B - A, Axis)), 1.f);
		for (const UStaticMeshComponent* P : LitPlates)
		{
			LitOrder.Add(FMath::Clamp(static_cast<float>(FVector::DotProduct(P->GetComponentLocation() - A, Axis)) / Span, 0.f, 1.f));
		}
	}
	else
	{
		LitOrder.Init(0.f, LitPlates.Num());
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %d plates on %d monitor walls and %d control banks, %d lit plates."), *GetName(), Plates.Num(), Walls.Num(), Banks, LitPlates.Num());
}

void APrologueMonitorRoom::BuildLitPlate(UStaticMeshComponent* Screen, int32 Layout, const FVector& Centre, const FVector& Normal, const FVector& Up, const FVector2D& Size, bool bBank)
{
	AActor* OwnerActor = Screen ? Screen->GetOwner() : nullptr;
	if (!OwnerActor || !LayoutMids.IsValidIndex(Layout))
	{
		return;
	}
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, ScreenPlate::PlanePath);
	UStaticMeshComponent* Lit = NewObject<UStaticMeshComponent>(OwnerActor, *FString::Printf(TEXT("LitScreen_%s_%03d"), *Screen->GetName(), LitPlates.Num()));
	// The black plate's flags (no collision, no shadow, never in a scan's capture), unlit and lighting nothing.
	ScreenPlate::Setup(Lit, Plane);
	Lit->bAffectDynamicIndirectLighting = false;
	Lit->SetMobility(Screen->Mobility);
	Lit->SetupAttachment(Screen);
	ScreenPlate::Place(Lit, Centre, Normal, Up, Size);
	Lit->RegisterComponent();
	Lit->SetMaterial(0, LayoutMids[Layout]);
	OwnerActor->AddInstanceComponent(Lit);
	LitPlates.Add(Lit);
	LitLayouts.Add(Layout);
	LitOnBank.Add(bBank);
}

TArray<UStaticMeshComponent*> APrologueMonitorRoom::GetLitPlates() const
{
	TArray<UStaticMeshComponent*> Out;
	for (UStaticMeshComponent* Plate : LitPlates)
	{
		if (Plate)
		{
			Out.Add(Plate);
		}
	}
	return Out;
}

int32 APrologueMonitorRoom::GetLitShownCount() const
{
	int32 Shown = 0;
	for (const UStaticMeshComponent* Plate : LitPlates)
	{
		Shown += Plate && Plate->IsVisible() ? 1 : 0;
	}
	return Shown;
}

void APrologueMonitorRoom::UpdateScreens()
{
	if (!bBatteryScreens || LitPlates.Num() == 0)
	{
		return;
	}
	if (!Battery.IsValid())
	{
		Battery = AChamberBattery::Find(GetWorld());
	}
	const AChamberBattery* B = Battery.Get();
	ScreenLevel = B ? B->GetGaugeLevel() : 0.f;
	const float Out = B ? B->GetScreenOutShare() : 0.f;
	const FLinearColor Color = B ? B->GetGlowColor() : FLinearColor::White;
	const float Brightness = UTheLastShiftSettings::Get()->GetBatteryLook().ScreenLevel;
	for (UMaterialInstanceDynamic* Mid : LayoutMids)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Level"), ScreenLevel);
			Mid->SetVectorParameterValue(TEXT("Color"), Color);
			Mid->SetScalarParameterValue(TEXT("Brightness"), Brightness);
		}
	}
	// A lit screen is light: Normal time only; while Stopped the black plate under it is the screen. In Open's aftermath
	// they go out one after another across the wall.
	const bool bStopped = TimeState && TimeState->IsStopped();
	const bool bShow = !bHiddenForTest && (bShownWhileStoppedForTest || !bStopped);
	for (int32 I = 0; I < LitPlates.Num(); ++I)
	{
		UStaticMeshComponent* Plate = LitPlates[I];
		if (!Plate)
		{
			continue;
		}
		const bool bGone = Out > 0.f && (Out >= 1.f || LitOrder[I] < Out);
		const bool bVisible = bShow && !bGone;
		if (Plate->IsVisible() != bVisible)
		{
			Plate->SetVisibility(bVisible);
		}
	}
}

void APrologueMonitorRoom::BuildLights()
{
	using namespace MonitorRoomLayout;
	LightsHome->ClearInstances();
	LightsSecond->ClearInstances();
	Lights.Reset();
	UWorld* World = GetWorld();
	if (!World || !RackMesh)
	{
		return;
	}
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	const FRackLightsTuning Tuning = Settings->GetRackLightsTuning();
	OnShare = Tuning.OnShare;

	// Home's tint has its one owner in SurfaceLook; the second colour is RackLights' own.
	const FLinearColor HomeTint = Settings->GetSurfaceVariant(true).Tint;
	const auto Material = [this](TObjectPtr<UMaterialInstanceDynamic>& Mid, const FLinearColor& Color, float Level, UInstancedStaticMeshComponent* Component)
	{
		if (!LightMaterial)
		{
			return;
		}
		if (!Mid || Mid->Parent != LightMaterial)
		{
			Mid = UMaterialInstanceDynamic::Create(LightMaterial, this);
		}
		Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.R * Level, Color.G * Level, Color.B * Level, 1.f));
		Component->SetMaterial(0, Mid);
	};
	Material(HomeMaterial, HomeTint, Tuning.Level, LightsHome);
	Material(SecondMaterial, Tuning.SecondColor, Tuning.Level, LightsSecond);

	TArray<UStaticMeshComponent*> Racks;
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* C = *It;
		if (C && C->GetWorld() == World && C->IsRegistered() && C->GetStaticMesh() == RackMesh && !Cast<UInstancedStaticMeshComponent>(C))
		{
			Racks.Add(C);
		}
	}
	// The same order every run (the lights' rhythms follow their index).
	Racks.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B)
	{
		const FVector LA = A.GetComponentLocation(), LB = B.GetComponentLocation();
		return LA.X != LB.X ? LA.X < LB.X : LA.Y < LB.Y;
	});
	const float Size = Tuning.SizeCm;
	const FVector Scale(Size / 100.f);
	int32 Index = 0;
	for (const UStaticMeshComponent* Rack : Racks)
	{
		const FTransform RackTransform = Rack->GetComponentTransform();
		for (const float ServerZ : ServerCentresZ)
		{
			const float Top = ServerZ + 0.5f * LightRowSpacing * (Tuning.PerServer - 1);
			for (int32 L = 0; L < Tuning.PerServer; ++L, ++Index)
			{
				const FVector Local(ServerFaceX + LightGap + Size * 0.5f, LightY, Top - L * LightRowSpacing);
				FLight Light;
				Light.World = FTransform(RackTransform.GetRotation(), RackTransform.TransformPosition(Local), Scale * RackTransform.GetScale3D());
				Light.Colour = Hash01(Index * 3 + 1) < Tuning.SecondShare ? 1 : 0;
				Light.Period = FMath::Lerp(Tuning.PeriodMinSeconds, Tuning.PeriodMaxSeconds, Hash01(Index * 3 + 2));
				Light.Phase = Hash01(Index * 3 + 3) * Light.Period;
				UInstancedStaticMeshComponent* Component = Light.Colour == 0 ? LightsHome : LightsSecond;
				Light.Instance = Component->AddInstance(Light.World, true);
				Lights.Add(Light);
			}
		}
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %d lights on %d racks (%d a server)."), *GetName(), Lights.Num(), Racks.Num(), Tuning.PerServer);
}

bool APrologueMonitorRoom::IsLightOn(const FLight& Light, double RunSeconds) const
{
	const double Cycle = FMath::Fmod(RunSeconds + Light.Phase, static_cast<double>(FMath::Max(Light.Period, 0.01f)));
	return Cycle < Light.Period * OnShare;
}

void APrologueMonitorRoom::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBlink(false);
	UpdateScreens();
}

void APrologueMonitorRoom::UpdateBlink(bool bForce)
{
	// The run's clock: it stands still behind the pause menu, and so do the lights.
	const double Now = TimeState ? TimeState->GetRunSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	bool bHomeDirty = false;
	bool bSecondDirty = false;
	for (FLight& Light : Lights)
	{
		const bool bOn = IsLightOn(Light, Now);
		if (bOn == Light.bOn && !bForce)
		{
			continue;
		}
		Light.bOn = bOn;
		UInstancedStaticMeshComponent* Component = Light.Colour == 0 ? LightsHome : LightsSecond;
		FTransform T = Light.World;
		if (!bOn)
		{
			// Off: the instance shrinks to nothing (no draw, no light: it never lit anything).
			T.SetScale3D(FVector::ZeroVector);
		}
		Component->UpdateInstanceTransform(Light.Instance, T, true, false, true);
		(Light.Colour == 0 ? bHomeDirty : bSecondDirty) = true;
	}
	if (bHomeDirty)
	{
		LightsHome->MarkRenderStateDirty();
	}
	if (bSecondDirty)
	{
		LightsSecond->MarkRenderStateDirty();
	}
}

int32 APrologueMonitorRoom::GetLightsOnCount() const
{
	int32 On = 0;
	for (const FLight& Light : Lights)
	{
		On += Light.bOn ? 1 : 0;
	}
	return On;
}

bool APrologueMonitorRoom::GetLight(int32 Index, FVector& OutLocation, int32& OutColour, float& OutPeriod, float& OutPhase, bool& bOutOn) const
{
	if (!Lights.IsValidIndex(Index))
	{
		return false;
	}
	const FLight& Light = Lights[Index];
	OutLocation = Light.World.GetLocation();
	OutColour = Light.Colour;
	OutPeriod = Light.Period;
	OutPhase = Light.Phase;
	bOutOn = Light.bOn;
	return true;
}

bool APrologueMonitorRoom::AreLightsShown() const
{
	return LightsHome && LightsHome->IsVisible();
}

void APrologueMonitorRoom::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	UpdateVisibility();
}

void APrologueMonitorRoom::UpdateVisibility()
{
	const bool bStopped = TimeState && TimeState->IsStopped();
	const bool bShow = !bHiddenForTest && (bShownWhileStoppedForTest || !bStopped);
	LightsHome->SetVisibility(bShow);
	LightsSecond->SetVisibility(bShow);
	UpdateScreens();
}

void APrologueMonitorRoom::SetShownWhileStoppedForTest(bool bShown)
{
	bShownWhileStoppedForTest = bShown;
	UpdateVisibility();
}

void APrologueMonitorRoom::SetHiddenForTest(bool bInHidden)
{
	bHiddenForTest = bInHidden;
	UpdateVisibility();
}

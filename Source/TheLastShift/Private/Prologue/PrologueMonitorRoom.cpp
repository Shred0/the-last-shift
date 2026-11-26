// SPDX-License-Identifier: MIT

#include "Prologue/PrologueMonitorRoom.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
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
}

void APrologueMonitorRoom::BeginPlay()
{
	Super::BeginPlay();
	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &APrologueMonitorRoom::HandleTimeStateChanged);
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
		for (const float Z : MonitorRowsZ)
		{
			for (const float Y : MonitorColumnsY)
			{
				// A component of the actor that owns the screen, attached to the wall's mesh: its frame is the mesh's.
				UStaticMeshComponent* Plate = NewObject<UStaticMeshComponent>(WallOwner, *FString::Printf(TEXT("ScreenPlate_%s_%02d"), *Wall->GetName(), Index++));
				ScreenPlate::Setup(Plate, Plane);
				Plate->SetMobility(Wall->Mobility);
				Plate->SetupAttachment(Wall);
				ScreenPlate::Place(Plate, FVector(MonitorFaceX, Y, Z), FVector::XAxisVector, FVector::ZAxisVector, MonitorSize);
				Plate->RegisterComponent();
				ScreenPlate::ApplyMaterial(Plate);
				WallOwner->AddInstanceComponent(Plate);
				Plates.Add(Plate);
			}
		}
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %d plates on %d monitor walls."), *GetName(), Plates.Num(), Walls.Num());
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

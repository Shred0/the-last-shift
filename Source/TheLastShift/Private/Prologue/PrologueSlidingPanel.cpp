// SPDX-License-Identifier: MIT

#include "Prologue/PrologueSlidingPanel.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Energy/DoorDevice.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "TheLastShift.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueSlidingPanel)

namespace ProloguePanelImpl
{
	/** Size of the engine's basic cube mesh used for the panel. */
	static constexpr float CubeMeshSize = 100.f;
	/** The panel is this share of the opening's depth, so that out of the opening it sits inside the wall or the floor. */
	static constexpr float PanelDepthShare = 0.75f;
	/** Open, the panel stops this far past the opening's edge, so its face never lies in the frame's face. */
	static constexpr float ClearGap = 1.f;
}

APrologueSlidingPanel::APrologueSlidingPanel()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(GetRootComponent());
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->SetCanEverAffectNavigation(false);
	Panel->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Panel->SetStaticMesh(CubeMesh.Object);
	}

	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(GetRootComponent());
	Blocker->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Blocker->SetCanEverAffectNavigation(false);
	Blocker->SetHiddenInGame(true);
}

FVector APrologueSlidingPanel::GetOpening() const
{
	return OpeningSize.ComponentMax(FVector(10.f)).ComponentMin(FVector(5000.f));
}

void APrologueSlidingPanel::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const FVector Opening = GetOpening();
	Blocker->SetBoxExtent(Opening * 0.5f);
	Blocker->SetRelativeLocation(FVector(0.f, 0.f, Opening.Z * 0.5f));
	ApplyPanel();
}

void APrologueSlidingPanel::BeginPlay()
{
	Super::BeginPlay();
	OpenFraction = 0.f;
	ApplyPanel();
	UE_LOG(LogTheLastShift, Log, TEXT("%s: closed, opening %s, slides %s%s."), *GetName(), *GetOpening().ToCompactString(),
		Slide == EProloguePanelSlide::Up ? TEXT("up") : TEXT("down"), bBlockWhenOpen ? TEXT(", the opening blocked even when open") : TEXT(""));
}

void APrologueSlidingPanel::SetOpenFraction(float InFraction)
{
	const float New = FMath::Clamp(InFraction, 0.f, 1.f);
	if (New == OpenFraction)
	{
		return;
	}
	const bool bWasOpen = IsFullyOpen();
	const float Old = OpenFraction;
	OpenFraction = New;
	ApplyPanel();
	// A motion starts when the fraction starts to rise (opening) or to fall (closing), a reversal included; it ends at the
	// end it moves to.
	const int32 Direction = New > Old ? 1 : -1;
	if (Direction != MotionDirection)
	{
		MotionDirection = Direction;
		OnMotionStarted.Broadcast(this, Direction > 0, true);
	}
	if ((Direction > 0 && New >= 1.f) || (Direction < 0 && New <= 0.f))
	{
		MotionDirection = 0;
		OnMotionEnded.Broadcast(this);
	}
	if (IsFullyOpen() != bWasOpen)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %s%s."), *GetName(), IsFullyOpen() ? TEXT("fully open") : TEXT("no longer fully open"),
			IsBlocking() ? TEXT(", the opening blocked") : TEXT(", the opening passable"));
	}
}

void APrologueSlidingPanel::SetOpenAtOnce(float InFraction)
{
	OpenFraction = FMath::Clamp(InFraction, 0.f, 1.f);
	MotionDirection = 0;
	ApplyPanel();
	UE_LOG(LogTheLastShift, Log, TEXT("%s: set at once to %.2f open (no motion)%s."), *GetName(), OpenFraction, IsBlocking() ? TEXT(", the opening blocked") : TEXT(", the opening passable"));
}

bool APrologueSlidingPanel::IsBlocking() const
{
	return Blocker && Blocker->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
}

FBox APrologueSlidingPanel::GetOpeningBox() const
{
	const FVector Extent = GetOpening() * 0.5f;
	return FBox(-Extent, Extent).TransformBy(FTransform(FVector(0.f, 0.f, Extent.Z)) * GetActorTransform());
}

void APrologueSlidingPanel::ApplyPanel()
{
	using namespace ProloguePanelImpl;
	if (!Panel || !Blocker)
	{
		return;
	}
	const FVector Opening = GetOpening();
	const float Overlap = FMath::Clamp(PanelOverlap, 0.f, 100.f);
	const FVector Size(Opening.X * PanelDepthShare, Opening.Y + 2.f * Overlap, Opening.Z + Overlap);
	// A modelled panel (PanelMesh) is shown at scale 1 and placed by its bottom centre; otherwise the cube is scaled.
	const bool bModelled = ADoorDevice::ApplyPanelMesh(Panel, PanelMesh);
	Panel->SetRelativeScale3D(bModelled ? FVector::OneVector : Size / CubeMeshSize);
	// Closed: the overlap is on the side the panel slides to (into the lintel, or into the floor). Open: moved by its
	// whole height and a gap, out of the opening.
	const float Travel = OpenFraction * (Size.Z + ClearGap);
	const float ClosedCentre = Slide == EProloguePanelSlide::Up ? Size.Z * 0.5f - Overlap : Size.Z * 0.5f;
	const float Centre = Slide == EProloguePanelSlide::Up ? ClosedCentre + Travel : ClosedCentre - Travel;
	Panel->SetRelativeLocation(FVector(0.f, 0.f, Centre - (bModelled ? Size.Z * 0.5f : 0.f)));

	const bool bBlock = bBlockWhenOpen || !IsFullyOpen();
	Blocker->SetCollisionEnabled(bBlock ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

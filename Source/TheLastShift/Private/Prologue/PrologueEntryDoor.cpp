// SPDX-License-Identifier: MIT

#include "Prologue/PrologueEntryDoor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "TheLastShift.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueEntryDoor)

namespace PrologueEntryDoorImpl
{
	/** Size of the engine's basic cube mesh used for the panel. */
	static constexpr float CubeMeshSize = 100.f;
	/** The panel is this share of the door's thickness, so that raised it sits inside the wall above the doorway. */
	static constexpr float PanelDepthShare = 0.75f;
	/** Raised, the panel's bottom stops this far above the doorway's top, so its face never lies in the lintel's face. */
	static constexpr float ClearGap = 1.f;
}

APrologueEntryDoor::APrologueEntryDoor()
{
	// The home's free doors: 2 m wide, 3 m high, untimed, never reset (there is no time jump outside the run).
	Blocker->SetBoxExtent(FVector(10.f, 100.f, 150.f));
	Doorway->SetBoxExtent(FVector(80.f, 100.f, 150.f));
	PoweredSeconds = 0.f;
	bResetOnJump = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Greybox(TEXT("/Game/TheLastShift/Art/M_Greybox.M_Greybox"));
	if (Greybox.Succeeded())
	{
		Panel->SetMaterial(0, Greybox.Object);
	}
}

void APrologueEntryDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPanelFraction();
}

bool APrologueEntryDoor::IsBlocking() const
{
	return Blocker && Blocker->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
}

void APrologueEntryDoor::ApplyPanelFraction()
{
	using namespace PrologueEntryDoorImpl;
	if (!Panel || !Blocker)
	{
		return;
	}
	const FVector Size = Blocker->GetUnscaledBoxExtent() * 2.f;
	// A modelled panel (PanelMesh) is shown at scale 1 and placed by its bottom centre; otherwise the cube is scaled.
	const bool bModelled = ApplyPanelMesh(Panel, PanelMesh);
	Panel->SetRelativeScale3D(bModelled ? FVector::OneVector : FVector(Size.X * PanelDepthShare, Size.Y, Size.Z) / CubeMeshSize);
	// Panel is attached to Blocker, whose centre is the doorway's centre: closed at 0, raised by its height and a gap.
	const float Raise = bClosingBehind ? (1.f - ClosedFraction) * (Size.Z + ClearGap) : 0.f;
	Panel->SetRelativeLocation(FVector(0.f, 0.f, Raise - (bModelled ? Size.Z * 0.5f : 0.f)));
}

bool APrologueEntryDoor::BeginCloseBehind()
{
	if (bClosingBehind || bClosedBehind || !IsOpen() || IsDoorwayOccupied())
	{
		return false;
	}
	bClosingBehind = true;
	ClosedFraction = 0.f;
	Blocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// Nobody can be in the doorway any more (the Blocker shuts it, and it was clear): the volume that keeps an unpowered
	// door open for a pawn in it has nothing left to guard, and a player against the closed panel must not hold it open.
	Doorway->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ApplyPanelFraction();
	Panel->SetVisibility(true);
	UE_LOG(LogTheLastShift, Log, TEXT("%s: closing behind the player: the way shut, the panel coming down."), *GetName());
	return true;
}

void APrologueEntryDoor::SetClosedFraction(float InFraction)
{
	if (!bClosingBehind)
	{
		return;
	}
	ClosedFraction = FMath::Clamp(InFraction, 0.f, 1.f);
	ApplyPanelFraction();
}

void APrologueEntryDoor::FinishCloseBehind()
{
	if (!bClosingBehind)
	{
		return;
	}
	ClosedFraction = 1.f;
	ApplyPanelFraction();
	bClosingBehind = false;
	bClosedBehind = true;
	ApplyPanelFraction();
	UE_LOG(LogTheLastShift, Log, TEXT("%s: closed behind the player; switched off."), *GetName());
	PowerOff();
}

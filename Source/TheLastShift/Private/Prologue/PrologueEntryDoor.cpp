// SPDX-License-Identifier: MIT

#include "Prologue/PrologueEntryDoor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TheLastShift.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueEntryDoor)

namespace PrologueEntryDoorImpl
{
	/** The panel is this share of the door's thickness, so that risen it sits inside the wall above the doorway. */
	static constexpr float PanelDepthShare = 0.75f;
}

APrologueEntryDoor::APrologueEntryDoor()
{
	// The home's free doors: 2 m wide, 3 m high, untimed, never reset (there is no time jump outside the run).
	Blocker->SetBoxExtent(FVector(10.f, 100.f, 150.f));
	Doorway->SetBoxExtent(FVector(80.f, 100.f, 150.f));
	PoweredSeconds = 0.f;
	bResetOnJump = false;
	ApplyPanel();
}

float APrologueEntryDoor::GetPanelDepthShare() const
{
	return PrologueEntryDoorImpl::PanelDepthShare;
}

bool APrologueEntryDoor::IsPanelShown() const
{
	// Closing behind, the panel shows from its risen place down.
	return bClosingBehind || Super::IsPanelShown();
}

bool APrologueEntryDoor::IsBlocking() const
{
	return (Blocker && Blocker->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		|| (PanelCollision && PanelCollision->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
}

bool APrologueEntryDoor::BeginCloseBehind()
{
	if (bClosingBehind || bClosedBehind || !IsFullyOpen() || IsMoving() || IsDoorwayOccupied())
	{
		return false;
	}
	bClosingBehind = true;
	ClosedFraction = 0.f;
	Blocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// Nobody can be in the doorway any more (the Blocker shuts it, and it was clear): the volume that keeps a door due to
	// close open for a pawn in it has nothing left to guard, and a player against the closed panel must not hold it open.
	Doorway->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetPanelShareAtOnce(1.f);
	UE_LOG(LogTheLastShift, Log, TEXT("%s: closing behind the player: the way shut, the panel coming down."), *GetName());
	// The close starts here; FinishCloseBehind ends it.
	OnMotionStarted.Broadcast(this, false, true);
	return true;
}

void APrologueEntryDoor::SetClosedFraction(float InFraction)
{
	if (!bClosingBehind)
	{
		return;
	}
	ClosedFraction = FMath::Clamp(InFraction, 0.f, 1.f);
	// Never quite shut while closing behind: the door is shut by FinishCloseBehind.
	SetPanelShareAtOnce(FMath::Max(1.f - ClosedFraction, KINDA_SMALL_NUMBER));
}

void APrologueEntryDoor::FinishCloseBehind()
{
	if (!bClosingBehind)
	{
		return;
	}
	ClosedFraction = 1.f;
	bClosingBehind = false;
	bClosedBehind = true;
	SetPanelShareAtOnce(0.f);
	// The panel is down: the close that began at BeginCloseBehind ends here.
	OnMotionEnded.Broadcast(this);
	UE_LOG(LogTheLastShift, Log, TEXT("%s: closed behind the player; switched off."), *GetName());
	PowerOff();
}

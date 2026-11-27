// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "EntityForm.generated.h"

/**
 * The look of the entity's figure (Brief 29): the surface its points are sampled from. Procedural is the capsule humanoid
 * refracted around its head (FEntityFigureShape::Build); the others are modelled meshes, one for each area. Only the look
 * changes: the entity moves, spawns, touches and is found the same way with every form.
 */
UENUM(BlueprintType)
enum class EEntityForm : uint8
{
	/** The procedural humanoid cloud, in RefractionCount copies around the head: the fallback. */
	Procedural,
	/** Area 1: four human figures joined at one head (SM_EntityFour). */
	Four,
	/** Area 2: a vast shapeless mass on long limbs (SM_EntityVast). */
	Vast,
	/** Area 3: an eye within wheels and wings (SM_EntityWheels). */
	Wheels,
	/** Area 4: the engineer's own echo (SM_EntityEcho). */
	Echo
};

inline const TCHAR* LexToString(EEntityForm Form)
{
	switch (Form)
	{
	case EEntityForm::Procedural: return TEXT("Procedural");
	case EEntityForm::Four: return TEXT("Four");
	case EEntityForm::Vast: return TEXT("Vast");
	case EEntityForm::Wheels: return TEXT("Wheels");
	case EEntityForm::Echo: return TEXT("Echo");
	}
	return TEXT("Unknown");
}

/** Parses a form's name (case-insensitive); false when Name names none. */
inline bool LexTryParseEntityForm(const FString& Name, EEntityForm& OutForm)
{
	for (const EEntityForm Form : { EEntityForm::Procedural, EEntityForm::Four, EEntityForm::Vast, EEntityForm::Wheels, EEntityForm::Echo })
	{
		if (Name.Equals(LexToString(Form), ESearchCase::IgnoreCase))
		{
			OutForm = Form;
			return true;
		}
	}
	return false;
}

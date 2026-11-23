// SPDX-License-Identifier: MIT

#include "Look/SurfaceLookSubsystem.h"

#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SurfaceLookSubsystem)

void USurfaceLookSubsystem::PostInitialize()
{
	Super::PostInitialize();
	// The editor's own world shows the look too while a map is edited (a game world pushes it again at BeginPlay).
	if (const UWorld* World = GetWorld(); World && World->WorldType == EWorldType::Editor)
	{
		ApplyLook();
	}
}

void USurfaceLookSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (ApplyLook())
	{
		UE_LOG(LogTheLastShift, Log, TEXT("Surface look: %s shows the %s variant."), *UWorld::RemovePIEPrefix(InWorld.GetOutermost()->GetName()),
			PushedVariant == 0 ? TEXT("home") : TEXT("twin"));
	}
}

UMaterialParameterCollection* USurfaceLookSubsystem::GetCollection()
{
	return UTheLastShiftSettings::Get()->SurfaceLook.Parameters.LoadSynchronous();
}

FName USurfaceLookSubsystem::ParamName(bool bHome, const TCHAR* Field)
{
	return FName(FString::Printf(TEXT("%s%s"), bHome ? TEXT("Home") : TEXT("Twin"), Field));
}

bool USurfaceLookSubsystem::IsHomeLook() const
{
	const UWorld* World = GetWorld();
	return World && UTheLastShiftSettings::Get()->IsHomeLookMap(World->GetOutermost()->GetName());
}

bool USurfaceLookSubsystem::ApplyLook()
{
	UWorld* World = GetWorld();
	UMaterialParameterCollection* Collection = GetCollection();
	UMaterialParameterCollectionInstance* Instance = World && Collection ? World->GetParameterCollectionInstance(Collection) : nullptr;
	if (!Instance)
	{
		return false;
	}
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	PushVariant(Instance, true, Settings->GetSurfaceVariant(true));
	PushVariant(Instance, false, Settings->GetSurfaceVariant(false));
	PushedVariant = IsHomeLook() ? 0 : 1;
	Instance->SetScalarParameterValue(TEXT("MapVariant"), static_cast<float>(PushedVariant));
	return true;
}

void USurfaceLookSubsystem::PushVariant(UMaterialParameterCollectionInstance* Instance, bool bHome, const FSurfaceVariant& V) const
{
	// The base tint is a hue only: its luminance is taken out, so that it never makes a family lighter or darker.
	const float Lum = FMath::Max(V.BaseTint.GetLuminance(), 1.e-3f);
	const FLinearColor Base(V.BaseTint.R / Lum, V.BaseTint.G / Lum, V.BaseTint.B / Lum, 1.f);
	Instance->SetVectorParameterValue(ParamName(bHome, TEXT("Tint")), FLinearColor(V.Tint.R, V.Tint.G, V.Tint.B, 1.f));
	Instance->SetVectorParameterValue(ParamName(bHome, TEXT("BaseTint")), Base);
	Instance->SetScalarParameterValue(ParamName(bHome, TEXT("AccentStrength")), V.AccentStrength);
	Instance->SetScalarParameterValue(ParamName(bHome, TEXT("AccentLevel")), V.AccentLevel);
	Instance->SetScalarParameterValue(ParamName(bHome, TEXT("Tone")), V.Tone);
	Instance->SetScalarParameterValue(ParamName(bHome, TEXT("Wear")), V.Wear);
	Instance->SetScalarParameterValue(ParamName(bHome, TEXT("Dirt")), V.Dirt);
	Instance->SetScalarParameterValue(ParamName(bHome, TEXT("PanelVariation")), V.PanelVariation);
}

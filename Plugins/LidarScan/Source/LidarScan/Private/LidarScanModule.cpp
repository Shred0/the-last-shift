// Copyright 2026 <OWNER>. All rights reserved. See Plugins/LidarScan/LICENSE.

#include "LidarScanModule.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "LidarScanComponent.h"
#include "Misc/PackageName.h"
#include "UObject/ICookInfo.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#endif

DEFINE_LOG_CATEGORY(LogLidarScan);

#if WITH_EDITOR
namespace LidarScanCook
{
	/**
	 * The scan component finds its assets through soft references set in its constructor. A default that nothing
	 * overrides is never saved in any package, so the cooker cannot follow it and the assets would be left out of
	 * the build. The module adds them to every cook itself: the package of each soft object property of the
	 * component's class defaults (and of any loaded subclass). Their hard dependencies (the point material) follow
	 * through the normal cook. The asset paths stay in one place, the component.
	 */
	static void ModifyCook(UE::Cook::ICookInfo& CookInfo, TArray<UE::Cook::FPackageCookRule>& InOutRules)
	{
		TSet<FName> Packages;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const UClass* Class = *It;
			if (!Class->IsChildOf(ULidarScanComponent::StaticClass())
				|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				|| Class->GetName().StartsWith(TEXT("SKEL_")))
			{
				continue;
			}

			const UObject* Defaults = Class->GetDefaultObject();
			for (TFieldIterator<FSoftObjectProperty> PropertyIt(Class); PropertyIt; ++PropertyIt)
			{
				for (int32 Index = 0; Index < PropertyIt->ArrayDim; ++Index)
				{
					const FSoftObjectPath Path = PropertyIt->GetPropertyValue_InContainer(Defaults, Index).ToSoftObjectPath();
					if (Path.IsNull())
					{
						continue;
					}
					if (!FPackageName::DoesPackageExist(Path.GetLongPackageName()))
					{
						UE_LOG(LogLidarScan, Error, TEXT("Lidar Scan: %s.%s points to '%s', which does not exist; it cannot be cooked."),
							*Class->GetName(), *PropertyIt->GetName(), *Path.ToString());
						continue;
					}
					Packages.Add(Path.GetLongPackageFName());
				}
			}
		}

		for (const FName& Package : Packages)
		{
			UE::Cook::FPackageCookRule& Rule = InOutRules.AddDefaulted_GetRef();
			Rule.PackageName = Package;
			Rule.InstigatorName = TEXT("LidarScan");
			Rule.CookRule = UE::Cook::EPackageCookRule::AddToCook;
		}
		UE_LOG(LogLidarScan, Display, TEXT("Lidar Scan: %d packages added to the cook."), Packages.Num());
	}
}
#endif // WITH_EDITOR

void FLidarScanModule::StartupModule()
{
#if WITH_EDITOR
	ModifyCookHandle = UE::Cook::FDelegates::ModifyCook.AddStatic(&LidarScanCook::ModifyCook);
#endif
}

void FLidarScanModule::ShutdownModule()
{
#if WITH_EDITOR
	UE::Cook::FDelegates::ModifyCook.Remove(ModifyCookHandle);
#endif
}

IMPLEMENT_MODULE(FLidarScanModule, LidarScan)

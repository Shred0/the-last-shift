// SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class TheLastShift : ModuleRules
{
	public TheLastShift(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"LidarScan",
			"NavigationSystem",
			"PCG",
			"StdCore",
			"SlateCore"
		});

	}
}

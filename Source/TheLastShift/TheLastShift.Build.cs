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
			"LidarScan",
			"NavigationSystem",
			"StdCore",
			"SlateCore"
		});

		// Area generator and validator (editor-time): volume brushes, editor world access.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}

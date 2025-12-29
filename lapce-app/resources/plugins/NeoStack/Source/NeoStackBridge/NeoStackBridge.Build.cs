// Copyright NeoStack. All Rights Reserved.

using UnrealBuildTool;

public class NeoStackBridge : ModuleRules
{
	public NeoStackBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Sockets",
				"Networking",
				"Json",
				"JsonUtilities",
				"WebSockets",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"Slate",
				"SlateCore",
				"NeoStack",  // For tool registry
			}
		);

		// Editor-only module
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"AssetRegistry",
					"Kismet",
				}
			);

			// PixelStreaming2 integration (optional - only if plugin is enabled)
			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
					"PixelStreaming2Editor",
					"PixelStreaming2Settings",
					"PixelStreaming2Servers",
				}
			);
		}
	}
}

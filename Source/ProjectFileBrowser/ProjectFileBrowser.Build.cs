// Copyright (c) 2026 Vahab Ahmadvand. All Rights Reserved.

using UnrealBuildTool;

public class ProjectFileBrowser : ModuleRules
{
	public ProjectFileBrowser(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ContentBrowserData"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UnrealEd",
				"AssetTools",
				"ContentBrowser",
				"DirectoryWatcher",
				"DeveloperSettings",
				"ToolMenus",
				"ContentBrowserFileDataSource",
				"ImageCore",
				"ImageWrapper",
				"RenderCore",
				"RHI"
			}
		);
	}
}

using UnrealBuildTool;

public class CookContent : ModuleRules
{
    public CookContent(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Slate", 
            "SlateCore",
            "EditorStyle",
            "UnrealEd",
            "LevelEditor",
            "ToolMenus",
            "EditorFramework",
            "Projects",
            "DesktopPlatform"
        });
    }
}
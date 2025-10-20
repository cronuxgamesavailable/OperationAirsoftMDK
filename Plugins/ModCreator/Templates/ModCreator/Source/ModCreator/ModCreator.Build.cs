using UnrealBuildTool;

public class ModCreator : ModuleRules
{
    public ModCreator(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
        "Core","CoreUObject","Engine","InputCore",
        "Slate","SlateCore","EditorStyle","UnrealEd",
        "LevelEditor","Projects","Json","JsonUtilities"
    });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ToolMenus",
            "UnrealEd",
            "EditorSubsystem",
            "LevelEditor",
            "Projects",
            "PakFile",
            "Json",
            "JsonUtilities",
            "InputCore", 
            "Settings"
        });
    }
}
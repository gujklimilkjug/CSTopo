using UnrealBuildTool;

public class CSTopo : ModuleRules
{
    public CSTopo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "EnhancedInput",
            "Json",
            "JsonUtilities",
            "InputCore",
            "ProceduralMeshComponent",
            "UMG",
            "LidarPointCloudRuntime"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "DesktopPlatform",
            "Projects",
            "Slate",
            "SlateCore"
        });
    }
}

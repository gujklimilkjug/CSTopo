using System.IO;
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
            "GeometryCore",
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

        string PdalRuntimeDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "PDAL", "Windows"));
        if (Directory.Exists(PdalRuntimeDir))
        {
            foreach (string RuntimeFile in Directory.GetFiles(PdalRuntimeDir, "*", SearchOption.AllDirectories))
            {
                RuntimeDependencies.Add(RuntimeFile, StagedFileType.NonUFS);
            }
        }
    }
}

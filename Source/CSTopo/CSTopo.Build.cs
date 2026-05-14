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

        string ProjectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
        string PdalRuntimeManifest = Path.Combine(ProjectRoot, "Config", "CSTopoPdalRuntime.json");
        if (File.Exists(PdalRuntimeManifest))
        {
            RuntimeDependencies.Add(PdalRuntimeManifest, StagedFileType.NonUFS);
        }

        string PdalBootstrapScript = Path.Combine(ProjectRoot, "scripts", "bootstrap_pdal_runtime.py");
        if (File.Exists(PdalBootstrapScript))
        {
            RuntimeDependencies.Add(PdalBootstrapScript, StagedFileType.NonUFS);
        }
    }
}

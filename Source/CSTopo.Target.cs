using UnrealBuildTool;
using System.Collections.Generic;

public class CSTopoTarget : TargetRules
{
    public CSTopoTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("CSTopo");
    }
}

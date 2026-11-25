using UnrealBuildTool;

public class MissionRecall : ModuleRules
{
    public MissionRecall(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore" });
        PrivateDependencyModuleNames.Add("ProceduralMeshComponent");
    }
}


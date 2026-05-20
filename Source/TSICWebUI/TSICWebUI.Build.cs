using UnrealBuildTool;

public class TSICWebUI : ModuleRules
{
	public TSICWebUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayAbilities",
			"GameplayMessageRuntime",
			"GameplayTags",
			"Projects",
			"RenderCore",
			"Renderer",
			"RHI",
			"Slate",
			"SlateCore",
			"UMG",
			"UltralightSDK",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"ApplicationCore",
			"Json",
			"JsonUtilities",
			"AssetRegistry",
			"ImageWrapper",
		});
	}
}

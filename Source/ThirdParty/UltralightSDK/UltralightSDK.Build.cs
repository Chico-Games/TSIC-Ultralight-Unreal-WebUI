using System.IO;
using UnrealBuildTool;

public class UltralightSDK : ModuleRules
{
	private string ProjectBinariesWin64
	{
		get { return Path.Combine(Directory.GetParent(ModuleDirectory).Parent.Parent.FullName, "Binaries", "Win64"); }
	}

	private void StageRuntimeFile(string SourcePath)
	{
		string FileName = Path.GetFileName(SourcePath);
		string DestPath = Path.Combine(ProjectBinariesWin64, FileName);

		if (!Directory.Exists(ProjectBinariesWin64))
		{
			Directory.CreateDirectory(ProjectBinariesWin64);
		}

		if (!File.Exists(DestPath) || File.GetLastWriteTimeUtc(SourcePath) > File.GetLastWriteTimeUtc(DestPath))
		{
			File.Copy(SourcePath, DestPath, true);
		}

		RuntimeDependencies.Add(DestPath, SourcePath);
	}

	private void StageResource(string RelativePath)
	{
		string SrcPath = Path.Combine(ModuleDirectory, "Binaries", "Win64", RelativePath);
		string DestPath = Path.Combine(ProjectBinariesWin64, RelativePath);

		string DestDir = Path.GetDirectoryName(DestPath);
		if (!Directory.Exists(DestDir))
		{
			Directory.CreateDirectory(DestDir);
		}

		if (!File.Exists(DestPath) || File.GetLastWriteTimeUtc(SrcPath) > File.GetLastWriteTimeUtc(DestPath))
		{
			File.Copy(SrcPath, DestPath, true);
		}

		RuntimeDependencies.Add(DestPath, SrcPath);
	}

	public UltralightSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));

		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			return;
		}

		string LibDir = Path.Combine(ModuleDirectory, "Win64");
		string SrcBinariesDir = Path.Combine(ModuleDirectory, "Binaries", "Win64");

		PublicAdditionalLibraries.AddRange(new string[]
		{
			Path.Combine(LibDir, "Ultralight.lib"),
			Path.Combine(LibDir, "UltralightCore.lib"),
			Path.Combine(LibDir, "WebCore.lib"),
		});

		PublicDelayLoadDLLs.AddRange(new string[]
		{
			"Ultralight.dll",
			"UltralightCore.dll",
			"WebCore.dll",
		});

		StageRuntimeFile(Path.Combine(SrcBinariesDir, "Ultralight.dll"));
		StageRuntimeFile(Path.Combine(SrcBinariesDir, "UltralightCore.dll"));
		StageRuntimeFile(Path.Combine(SrcBinariesDir, "WebCore.dll"));

		StageResource(Path.Combine("resources", "icudt67l.dat"));
		StageResource(Path.Combine("resources", "cacert.pem"));
	}
}

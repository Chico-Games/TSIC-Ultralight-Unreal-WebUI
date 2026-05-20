#include "TSICWebUI.h"
#include "TSICWebFileSystem.h"
#include "TSICWebFontLoader.h"
#include "TSICWebGPUDriver.h"
#include "TSICWebLogger.h"
#include "TSICWebStringHelpers.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCore.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/Ultralight.h>
#include <Ultralight/Renderer.h>
#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY(LogTSICWebUI);

#define LOCTEXT_NAMESPACE "FTSICWebUIModule"

void FTSICWebUIModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TSICWebUI"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("TSICWebUI: plugin manifest not found, skipping startup."));
		return;
	}

	const FString BaseDir = Plugin->GetBaseDir();
	SdkBinariesDir = FPaths::Combine(BaseDir, TEXT("Source/ThirdParty/UltralightSDK/Binaries/Win64"));

	const FString WebCorePath = FPaths::Combine(SdkBinariesDir, TEXT("WebCore.dll"));
	const FString UltralightCorePath = FPaths::Combine(SdkBinariesDir, TEXT("UltralightCore.dll"));
	const FString UltralightPath = FPaths::Combine(SdkBinariesDir, TEXT("Ultralight.dll"));

	FPlatformProcess::PushDllDirectory(*SdkBinariesDir);
	WebCoreHandle = FPlatformProcess::GetDllHandle(*WebCorePath);
	UltralightCoreHandle = FPlatformProcess::GetDllHandle(*UltralightCorePath);
	UltralightHandle = FPlatformProcess::GetDllHandle(*UltralightPath);
	FPlatformProcess::PopDllDirectory(*SdkBinariesDir);

	bLibrariesLoaded = WebCoreHandle && UltralightCoreHandle && UltralightHandle;
	if (!bLibrariesLoaded)
	{
		UE_LOG(LogTSICWebUI, Error,
			TEXT("TSICWebUI: failed to load Ultralight DLLs from %s (WebCore=%p, UltralightCore=%p, Ultralight=%p)"),
			*SdkBinariesDir, WebCoreHandle, UltralightCoreHandle, UltralightHandle);
		return;
	}

	UE_LOG(LogTSICWebUI, Log, TEXT("TSICWebUI: Ultralight DLLs loaded from %s"), *SdkBinariesDir);

	const FString ShaderDir = FPaths::Combine(BaseDir, TEXT("Shaders"));
	if (FPaths::DirectoryExists(ShaderDir))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/TSICWebUI"), ShaderDir);
		UE_LOG(LogTSICWebUI, Log, TEXT("TSICWebUI: registered shader dir %s -> /Plugin/TSICWebUI"), *ShaderDir);
	}

	// Disable Slate paint caching for the layers that wrap the WebUI viewport.
	// CommonUI's primary layout puts the viewport behind an invalidation panel,
	// and UE 5.6's global-invalidation root caches widget paint output across
	// frames at a separate layer. Either layer leaves the slate stuck on the
	// pre-transition page until a window resize busts the cache. The WebUI is
	// a single full-screen surface whose contents change every Ultralight tick,
	// so caching buys us nothing.
	// Deferred to OnPostEngineInit because applying these CVars from
	// DefaultEngine.ini's [ConsoleVariables] section crashes — their OnChanged
	// callbacks broadcast a downstream delegate that isn't constructed yet
	// during PreInitPreStartupScreen.
	FCoreDelegates::OnPostEngineInit.AddLambda([]()
	{
		auto ForceCVarOff = [](const TCHAR* Name)
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
			if (!CVar)
			{
				UE_LOG(LogTSICWebUI, Warning, TEXT("TSICWebUI: CVar %s not found; cannot disable Slate cache layer."), Name);
				return;
			}
			CVar->Set(0, ECVF_SetByProjectSetting);
			UE_LOG(LogTSICWebUI, Log, TEXT("TSICWebUI: forced %s=0 (was reflected as %d)."), Name, CVar->GetInt());
		};
		ForceCVarOff(TEXT("Slate.EnableInvalidationPanels"));
		ForceCVarOff(TEXT("Slate.EnableGlobalInvalidation"));
	});
}

void FTSICWebUIModule::EnsurePlatformInitialised()
{
	if (bPlatformInitialised || !bLibrariesLoaded)
	{
		return;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("TSICWebUI"));
	if (!Plugin.IsValid())
	{
		return;
	}

	const FString BaseDir = Plugin->GetBaseDir();
	const FString ResourceRoot = FPaths::Combine(BaseDir, TEXT("Source/ThirdParty/UltralightSDK/Binaries/Win64"));
	const FString ContentRoot  = FPaths::Combine(BaseDir, TEXT("Content/UI/Web"));
	const FString CacheDir     = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UltralightCache"));
	IFileManager::Get().MakeDirectory(*CacheDir, /*Tree*/ true);

	ultralight::Config Cfg;
	Cfg.cache_path = TSICWebUI::FStringToUL(CacheDir);
	Cfg.resource_path_prefix = "resources/";
	Cfg.face_winding = ultralight::FaceWinding::Clockwise;

	auto* Logger     = new FTSICWebLogger();
	auto* FileSystem = new FTSICWebFileSystem(ContentRoot, ResourceRoot);
	auto* FontLoader = new FTSICWebFontLoader();
	LoggerPtr = Logger;
	FileSystemPtr = FileSystem;
	FontLoaderPtr = FontLoader;

	ultralight::Platform& Platform = ultralight::Platform::instance();
	Platform.set_config(Cfg);
	Platform.set_logger(Logger);
	Platform.set_file_system(FileSystem);
	Platform.set_font_loader(FontLoader);

	// GPU driver registration (read-only CVar; module-level so it doesn't flip mid-run).
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TSICWebUI.GPU"));
	bGPUAccelerated = (CVar && CVar->GetInt() != 0);
	if (bGPUAccelerated)
	{
		auto* GPUDriver = new TSICWebUI::FWebGPUDriver();
		GPUDriverPtr = GPUDriver;
		Platform.set_gpu_driver(GPUDriver);
		UE_LOG(LogTSICWebUI, Log, TEXT("TSICWebUI: GPU driver registered"));
	}

	ultralight::RefPtr<ultralight::Renderer> Renderer = ultralight::Renderer::Create();
	if (!Renderer.get())
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("TSICWebUI: Renderer::Create() returned null."));
		TeardownPlatform();
		return;
	}
	RendererPtr = new ultralight::RefPtr<ultralight::Renderer>(Renderer);

	bPlatformInitialised = true;
	UE_LOG(LogTSICWebUI, Log, TEXT("TSICWebUI: platform + renderer ready (module-owned, gpu=%d)"),
		bGPUAccelerated ? 1 : 0);
}

void FTSICWebUIModule::TeardownPlatform()
{
	FlushRenderingCommands();

	ultralight::Platform& Platform = ultralight::Platform::instance();
	Platform.set_gpu_driver(nullptr);
	Platform.set_font_loader(nullptr);
	Platform.set_file_system(nullptr);
	Platform.set_logger(nullptr);

	if (RendererPtr)
	{
		delete static_cast<ultralight::RefPtr<ultralight::Renderer>*>(RendererPtr);
		RendererPtr = nullptr;
	}
	FlushRenderingCommands();

	if (GPUDriverPtr) { delete static_cast<TSICWebUI::FWebGPUDriver*>(GPUDriverPtr); GPUDriverPtr = nullptr; }
	if (FontLoaderPtr){ delete static_cast<FTSICWebFontLoader*>(FontLoaderPtr); FontLoaderPtr = nullptr; }
	if (FileSystemPtr){ delete static_cast<FTSICWebFileSystem*>(FileSystemPtr); FileSystemPtr = nullptr; }
	if (LoggerPtr)    { delete static_cast<FTSICWebLogger*>(LoggerPtr); LoggerPtr = nullptr; }
	bGPUAccelerated = false;
	bPlatformInitialised = false;
}

void FTSICWebUIModule::ShutdownModule()
{
	TeardownPlatform();

	if (UltralightHandle) { FPlatformProcess::FreeDllHandle(UltralightHandle); UltralightHandle = nullptr; }
	if (UltralightCoreHandle) { FPlatformProcess::FreeDllHandle(UltralightCoreHandle); UltralightCoreHandle = nullptr; }
	if (WebCoreHandle) { FPlatformProcess::FreeDllHandle(WebCoreHandle); WebCoreHandle = nullptr; }
	bLibrariesLoaded = false;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTSICWebUIModule, TSICWebUI)

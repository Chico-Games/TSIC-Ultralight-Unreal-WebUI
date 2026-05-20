#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

TSICWEBUI_API DECLARE_LOG_CATEGORY_EXTERN(LogTSICWebUI, Log, All);

class FTSICWebUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FTSICWebUIModule& Get() { return FModuleManager::LoadModuleChecked<FTSICWebUIModule>(TEXT("TSICWebUI")); }
	static FTSICWebUIModule* GetPtr() { return FModuleManager::GetModulePtr<FTSICWebUIModule>(TEXT("TSICWebUI")); }

	bool AreLibrariesLoaded() const { return bLibrariesLoaded; }
	FString GetSdkBinariesDir() const { return SdkBinariesDir; }

	// Module-owned Ultralight runtime so it survives across PIE start/stop cycles.
	// The GameInstanceSubsystem uses these via accessors instead of owning its own.
	bool IsRendererReady() const { return RendererPtr != nullptr; }
	void* GetRenderer() const { return RendererPtr; }
	void* GetGPUDriver() const { return GPUDriverPtr; }
	bool  IsGPUAccelerated() const { return bGPUAccelerated; }

	// Called from the first subsystem Initialize. Idempotent.
	void EnsurePlatformInitialised();

private:
	void TeardownPlatform();

	void* WebCoreHandle = nullptr;
	void* UltralightCoreHandle = nullptr;
	void* UltralightHandle = nullptr;
	FString SdkBinariesDir;
	bool bLibrariesLoaded = false;

	// Ultralight Platform runtime (module-owned).
	void* RendererPtr = nullptr;
	void* FileSystemPtr = nullptr;
	void* FontLoaderPtr = nullptr;
	void* LoggerPtr = nullptr;
	void* GPUDriverPtr = nullptr;
	bool  bGPUAccelerated = false;
	bool  bPlatformInitialised = false;
};

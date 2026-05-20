#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/platform/FileSystem.h>
THIRD_PARTY_INCLUDES_END

class UGameInstance;

class FTSICWebFileSystem : public ultralight::FileSystem
{
public:
	FTSICWebFileSystem(const FString& InContentRoot, const FString& InResourceRoot);
	virtual ~FTSICWebFileSystem() override = default;

	/** Set the active game instance — used to resolve tex:// URLs against the registry. */
	void SetGameInstance(UGameInstance* InGameInstance);

	virtual bool FileExists(const ultralight::String& FilePath) override;
	virtual ultralight::String GetFileMimeType(const ultralight::String& FilePath) override;
	virtual ultralight::String GetFileCharset(const ultralight::String& FilePath) override;
	virtual ultralight::RefPtr<ultralight::Buffer> OpenFile(const ultralight::String& FilePath) override;

private:
	bool Resolve(const ultralight::String& InPath, FString& OutAbsolute) const;

	FString ContentRoot;
	FString ResourceRoot;
	TWeakObjectPtr<UGameInstance> GameInstanceWeak;
};

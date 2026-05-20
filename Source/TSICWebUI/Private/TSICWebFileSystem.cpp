#include "TSICWebFileSystem.h"
#include "TSICWebStringHelpers.h"
#include "TSICWebTexProvider.h"
#include "TSICWebUI.h"

#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/Buffer.h>
THIRD_PARTY_INCLUDES_END

namespace
{
	void DestroyTArrayBuffer(void* UserData, void* /*Data*/)
	{
		delete static_cast<TArray<uint8>*>(UserData);
	}

	FString NormaliseRelative(const ultralight::String& InPath)
	{
		FString Rel = TSICWebUI::ULToFString(InPath);
		Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Rel.StartsWith(TEXT("/")))
		{
			Rel.RightChopInline(1, EAllowShrinking::No);
		}
		return Rel;
	}
}

FTSICWebFileSystem::FTSICWebFileSystem(const FString& InContentRoot, const FString& InResourceRoot)
	: ContentRoot(FPaths::ConvertRelativePathToFull(InContentRoot))
	, ResourceRoot(FPaths::ConvertRelativePathToFull(InResourceRoot))
{
	IFileManager& FileManager = IFileManager::Get();
	checkf(FileManager.DirectoryExists(*ResourceRoot),
		TEXT("TSICWebFileSystem: resource root does not exist: %s"), *ResourceRoot);

	if (!FileManager.DirectoryExists(*ContentRoot))
	{
		UE_LOG(LogTSICWebUI, Warning,
			TEXT("TSICWebFileSystem: content root does not exist (will fall back to resource root): %s"),
			*ContentRoot);
	}

	UE_LOG(LogTSICWebUI, Log, TEXT("TSICWebFileSystem: content=%s, resources=%s"), *ContentRoot, *ResourceRoot);
}

void FTSICWebFileSystem::SetGameInstance(UGameInstance* InGameInstance)
{
	GameInstanceWeak = InGameInstance;
	TSICWebTex::ResetCache(); // game-instance swap invalidates tex registry
}

bool FTSICWebFileSystem::Resolve(const ultralight::String& InPath, FString& OutAbsolute) const
{
	const FString Rel = NormaliseRelative(InPath);
	if (Rel.IsEmpty() || Rel.Contains(TEXT("..")))
	{
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();

	const FString ContentCandidate = FPaths::Combine(ContentRoot, Rel);
	if (FileManager.FileExists(*ContentCandidate))
	{
		OutAbsolute = ContentCandidate;
		return true;
	}

	const FString ResourceCandidate = FPaths::Combine(ResourceRoot, Rel);
	if (FileManager.FileExists(*ResourceCandidate))
	{
		OutAbsolute = ResourceCandidate;
		return true;
	}

	return false;
}

bool FTSICWebFileSystem::FileExists(const ultralight::String& FilePath)
{
	const FString PathStr = TSICWebUI::ULToFString(FilePath);
	if (TSICWebTex::IsTexUrl(PathStr))
	{
		TArray<uint8> Bytes;
		return TSICWebTex::ResolveAndEncode(PathStr, GameInstanceWeak.Get(), Bytes);
	}

	FString Resolved;
	const bool bFound = Resolve(FilePath, Resolved);
	UE_LOG(LogTSICWebUI, Verbose, TEXT("FileSystem::FileExists '%s' -> %s"),
		*PathStr, bFound ? *Resolved : TEXT("(not found)"));
	return bFound;
}

ultralight::String FTSICWebFileSystem::GetFileMimeType(const ultralight::String& FilePath)
{
	const FString Path = TSICWebUI::ULToFString(FilePath);
	if (TSICWebTex::IsTexUrl(Path))
	{
		return ultralight::String("image/png");
	}
	const FString Ext = FPaths::GetExtension(Path).ToLower();

	if (Ext == TEXT("html") || Ext == TEXT("htm")) return ultralight::String("text/html");
	if (Ext == TEXT("css")) return ultralight::String("text/css");
	if (Ext == TEXT("js")) return ultralight::String("application/javascript");
	if (Ext == TEXT("json")) return ultralight::String("application/json");
	if (Ext == TEXT("png")) return ultralight::String("image/png");
	if (Ext == TEXT("jpg") || Ext == TEXT("jpeg")) return ultralight::String("image/jpeg");
	if (Ext == TEXT("gif")) return ultralight::String("image/gif");
	if (Ext == TEXT("svg")) return ultralight::String("image/svg+xml");
	if (Ext == TEXT("woff")) return ultralight::String("font/woff");
	if (Ext == TEXT("woff2")) return ultralight::String("font/woff2");
	if (Ext == TEXT("ttf")) return ultralight::String("font/ttf");
	if (Ext == TEXT("otf")) return ultralight::String("font/otf");
	if (Ext == TEXT("dat")) return ultralight::String("application/octet-stream");
	if (Ext == TEXT("pem")) return ultralight::String("application/x-pem-file");
	if (Ext == TEXT("txt")) return ultralight::String("text/plain");
	return ultralight::String("application/octet-stream");
}

ultralight::String FTSICWebFileSystem::GetFileCharset(const ultralight::String& /*FilePath*/)
{
	return ultralight::String("utf-8");
}

ultralight::RefPtr<ultralight::Buffer> FTSICWebFileSystem::OpenFile(const ultralight::String& FilePath)
{
	const FString PathStr = TSICWebUI::ULToFString(FilePath);
	if (TSICWebTex::IsTexUrl(PathStr))
	{
		TUniquePtr<TArray<uint8>> Bytes = MakeUnique<TArray<uint8>>();
		if (!TSICWebTex::ResolveAndEncode(PathStr, GameInstanceWeak.Get(), *Bytes))
		{
			UE_LOG(LogTSICWebUI, Warning, TEXT("FileSystem::OpenFile: tex miss '%s'"), *PathStr);
			return nullptr;
		}
		const size_t Size = static_cast<size_t>(Bytes->Num());
		void* Data = Bytes->GetData();
		TArray<uint8>* Owned = Bytes.Release();
		return ultralight::Buffer::Create(Data, Size, Owned, &DestroyTArrayBuffer);
	}

	FString Absolute;
	if (!Resolve(FilePath, Absolute))
	{
		UE_LOG(LogTSICWebUI, Warning, TEXT("FileSystem::OpenFile: not found '%s'"), *PathStr);
		return nullptr;
	}

	TUniquePtr<TArray<uint8>> Bytes = MakeUnique<TArray<uint8>>();
	if (!FFileHelper::LoadFileToArray(*Bytes, *Absolute))
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("FileSystem::OpenFile: read failed '%s'"), *Absolute);
		return nullptr;
	}

	const size_t Size = static_cast<size_t>(Bytes->Num());
	void* Data = Bytes->GetData();
	TArray<uint8>* Owned = Bytes.Release();

	return ultralight::Buffer::Create(Data, Size, Owned, &DestroyTArrayBuffer);
}

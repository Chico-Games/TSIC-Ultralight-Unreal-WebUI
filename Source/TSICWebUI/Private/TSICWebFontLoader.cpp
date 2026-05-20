#include "TSICWebFontLoader.h"
#include "TSICWebStringHelpers.h"
#include "TSICWebUI.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

namespace
{
	FString PickFallbackFontPath()
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		const FString WindowsDir = FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR"));
		const TArray<FString> Candidates = {
			FPaths::Combine(WindowsDir, TEXT("Fonts/segoeui.ttf")),
			FPaths::Combine(WindowsDir, TEXT("Fonts/arial.ttf")),
			FPaths::Combine(WindowsDir, TEXT("Fonts/tahoma.ttf")),
			TEXT("C:/Windows/Fonts/segoeui.ttf"),
			TEXT("C:/Windows/Fonts/arial.ttf"),
		};

		for (const FString& Candidate : Candidates)
		{
			if (PlatformFile.FileExists(*Candidate))
			{
				return Candidate;
			}
		}

		return FString();
	}
}

FTSICWebFontLoader::FTSICWebFontLoader()
	: FallbackFontPath(PickFallbackFontPath())
{
	if (FallbackFontPath.IsEmpty())
	{
		UE_LOG(LogTSICWebUI, Error, TEXT("FontLoader: no fallback font found in Windows Fonts directory."));
		return;
	}
	UE_LOG(LogTSICWebUI, Log, TEXT("FontLoader: using fallback font %s"), *FallbackFontPath);
}

ultralight::String FTSICWebFontLoader::fallback_font() const
{
	return ultralight::String("sans-serif");
}

ultralight::String FTSICWebFontLoader::fallback_font_for_characters(
	const ultralight::String& /*Characters*/, int /*Weight*/, bool /*bItalic*/) const
{
	return fallback_font();
}

ultralight::RefPtr<ultralight::FontFile> FTSICWebFontLoader::Load(
	const ultralight::String& /*Family*/, int /*Weight*/, bool /*bItalic*/)
{
	if (FallbackFontPath.IsEmpty())
	{
		return nullptr;
	}
	return ultralight::FontFile::Create(TSICWebUI::FStringToUL(FallbackFontPath));
}

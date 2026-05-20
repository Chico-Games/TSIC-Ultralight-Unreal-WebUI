#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/platform/FontLoader.h>
THIRD_PARTY_INCLUDES_END

class FTSICWebFontLoader : public ultralight::FontLoader
{
public:
	FTSICWebFontLoader();
	virtual ~FTSICWebFontLoader() override = default;

	virtual ultralight::String fallback_font() const override;
	virtual ultralight::String fallback_font_for_characters(
		const ultralight::String& Characters, int Weight, bool bItalic) const override;
	virtual ultralight::RefPtr<ultralight::FontFile> Load(
		const ultralight::String& Family, int Weight, bool bItalic) override;

private:
	FString FallbackFontPath;
};

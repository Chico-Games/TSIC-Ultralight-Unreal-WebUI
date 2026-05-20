#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/String.h>
THIRD_PARTY_INCLUDES_END

namespace TSICWebUI
{
	inline ultralight::String FStringToUL(const FString& In)
	{
		FTCHARToUTF8 Conv(*In);
		return ultralight::String(Conv.Get(), Conv.Length());
	}

	inline FString ULToFString(const ultralight::String& In)
	{
		return FString(UTF8_TO_TCHAR(In.utf8().data()));
	}
}

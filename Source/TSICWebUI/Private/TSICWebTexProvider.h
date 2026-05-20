// Copyright Chico Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

namespace TSICWebTex
{
    /** Returns true if Url starts with "tex://". */
    bool IsTexUrl(const FString& Url);

    /** PNG-encode the resource named by Url. Returns false on miss / failure. */
    bool ResolveAndEncode(const FString& Url, UGameInstance* GameInstance, TArray<uint8>& OutBytes);

    /** Returns "image/png" — separate function so the FileSystem can decide
     *  whether to short-circuit MIME-type lookup. */
    FString GetTexMimeType();

    /** Drop the LRU cache. Called from the FileSystem when the game instance changes. */
    void ResetCache();
}

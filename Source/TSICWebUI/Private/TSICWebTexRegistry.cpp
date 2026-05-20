// Copyright Chico Games, Inc. All Rights Reserved.
#include "TSICWebTexRegistry.h"
#include "Engine/TextureRenderTarget2D.h"

void UTSICWebTexRegistry::RegisterRenderTarget(FName Key, UTextureRenderTarget2D* RenderTarget)
{
    if (Key.IsNone() || !IsValid(RenderTarget))
    {
        return;
    }
    Map.Add(Key, RenderTarget);
}

void UTSICWebTexRegistry::UnregisterRenderTarget(FName Key)
{
    Map.Remove(Key);
}

UTextureRenderTarget2D* UTSICWebTexRegistry::GetRenderTarget(FName Key) const
{
    if (const TObjectPtr<UTextureRenderTarget2D>* Found = Map.Find(Key))
    {
        return *Found;
    }
    return nullptr;
}

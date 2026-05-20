// Copyright Chico Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TSICWebTexRegistry.generated.h"

class UTextureRenderTarget2D;

/**
 * Lets gameplay-side code register live UTextureRenderTarget2D objects under a
 * stable name (e.g. "character-preview"); the WebFileSystem resolves
 * tex://<name> URLs by looking the texture up here and PNG-encoding the pixels.
 */
UCLASS()
class TSICWEBUI_API UTSICWebTexRegistry : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    void RegisterRenderTarget(FName Key, UTextureRenderTarget2D* RenderTarget);
    void UnregisterRenderTarget(FName Key);
    UTextureRenderTarget2D* GetRenderTarget(FName Key) const;

private:
    UPROPERTY()
    TMap<FName, TObjectPtr<UTextureRenderTarget2D>> Map;
};

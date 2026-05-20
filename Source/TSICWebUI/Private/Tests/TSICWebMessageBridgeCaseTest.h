#pragma once

#include "CoreMinimal.h"
#include "TSICWebMessageBridgeCaseTest.generated.h"

// Test-only payload that exercises the JSON-case contract enforced by
// TSICWebMessageBridge.h. Field names are picked to cover every flavour of
// first-letter risk: PascalCase (StationId), lower-then-Pascal (recipeId),
// b-prefix bool (bIsActive), and a trivial scalar (Count).
USTRUCT()
struct FTSICWebBridgeCaseTestPayload
{
	GENERATED_BODY()

	UPROPERTY() FString StationId;
	UPROPERTY() FString RecipeId;
	UPROPERTY() bool bIsActive = false;
	UPROPERTY() int32 Count = 0;
};

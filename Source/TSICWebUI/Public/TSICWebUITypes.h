#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "TSICWebUITypes.generated.h"

// Slate keyboard-capture policy for a single web view. Orthogonal to the JS
// `UI.Input.Mode.Changed` device mode (MouseAndKeyboard/Gamepad/Touch); a view
// can be in any combination of the two.
UENUM(BlueprintType)
enum class EWebViewFocusMode : uint8
{
	CaptureOnClick UMETA(DisplayName = "Capture On Click", ToolTip = "Clicking the view focuses it; pressing the release key (default Escape) or clicking outside returns focus to gameplay."),
	AlwaysCaptured UMETA(DisplayName = "Always Captured", ToolTip = "View permanently has keyboard focus while visible. No release key honored."),
	ManualOnly UMETA(DisplayName = "Manual Only", ToolTip = "Focus only via SetFocusCapture() Blueprint call; clicks do not focus."),
	MouseOnly UMETA(DisplayName = "Mouse Only (HUD)", ToolTip = "Mouse events forwarded but keyboard never captured. Use for non-interactive HUD overlays."),
};

USTRUCT(BlueprintType)
struct TSICWEBUI_API FTSICWebViewConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI", meta = (ClampMin = "1"))
	int32 Width = 1280;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI", meta = (ClampMin = "1"))
	int32 Height = 720;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	bool bIsTransparent = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	bool bEnableJavaScript = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI", meta = (ClampMin = "0.1"))
	float DeviceScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI|Input")
	EWebViewFocusMode FocusMode = EWebViewFocusMode::CaptureOnClick;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI|Input")
	FKey FocusReleaseKey = EKeys::Escape;
};

USTRUCT(BlueprintType)
struct TSICWEBUI_API FTSICWebInteractiveRect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	int32 Y = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	int32 Width = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TSIC Web UI")
	int32 Height = 0;
};

UENUM(BlueprintType)
enum class EWebChannelKind : uint8
{
	Event UMETA(DisplayName = "Event", ToolTip = "Fire-and-forget. Subscribers only receive events fired after they subscribe."),
	Sticky UMETA(DisplayName = "Sticky", ToolTip = "Last-broadcast payload is cached; new subscribers receive it immediately."),
};

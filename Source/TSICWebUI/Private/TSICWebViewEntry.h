#pragma once

#include "CoreMinimal.h"
#include "TSICWebUITypes.h"
#include "UObject/WeakObjectPtr.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/RefPtr.h>
#include <Ultralight/View.h>
THIRD_PARTY_INCLUDES_END

class FTSICWebLoadListener;
class FTSICWebViewListener;
class SWidget;
class UTexture2D;

struct FTSICWebViewEntry
{
	FName ViewName;
	ultralight::RefPtr<ultralight::View> View;
	TWeakObjectPtr<UTexture2D> Texture;
	uint32 Width = 0;
	uint32 Height = 0;

	// Listeners owned by this entry (raw because Ultralight does not take ownership).
	TUniquePtr<FTSICWebViewListener> ViewListener;
	TUniquePtr<FTSICWebLoadListener> LoadListener;

	// Per-view focus state managed by the subsystem; clients set this via SetFocusMode().
	EWebViewFocusMode FocusMode = EWebViewFocusMode::CaptureOnClick;
	FKey FocusReleaseKey = EKeys::Escape;
	bool bHasFocus = false;
	bool bJSContextReady = false;

	// The Slate widget hosting this view, if any. Set by STSICWebViewSlate::Construct;
	// SetFocusCapture() uses this to also set Slate user focus so OS key events route here.
	TWeakPtr<SWidget> SlateWidget;

	// Interactive rects set by the page via tsic.setInteractiveRects(). Empty = entire view is interactive.
	TArray<FTSICWebInteractiveRect> InteractiveRects;

	bool IsPointInteractive(int32 LocalX, int32 LocalY) const
	{
		if (InteractiveRects.Num() == 0)
		{
			return true;
		}
		for (const FTSICWebInteractiveRect& R : InteractiveRects)
		{
			if (LocalX >= R.X && LocalY >= R.Y && LocalX < R.X + R.Width && LocalY < R.Y + R.Height)
			{
				return true;
			}
		}
		return false;
	}
};

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/ICursor.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/Listener.h>
THIRD_PARTY_INCLUDES_END

class UTSICWebUISubsystem;

// Per-view ViewListener that:
//   * Forwards console messages to UE_LOG (renamed per view).
//   * Tracks the current OS cursor requested by the page so Slate::GetCursor() can return it.
class FTSICWebViewListener : public ultralight::ViewListener
{
public:
	FTSICWebViewListener(UTSICWebUISubsystem* InSubsystem, FName InViewName);
	virtual ~FTSICWebViewListener() override = default;

	virtual void OnChangeCursor(ultralight::View* Caller, ultralight::Cursor Cursor) override;
	virtual void OnAddConsoleMessage(ultralight::View* Caller, const ultralight::ConsoleMessage& Message) override;
	virtual void OnChangeTitle(ultralight::View* Caller, const ultralight::String& Title) override;

	EMouseCursor::Type GetCurrentCursor() const { return CurrentCursor; }

private:
	TWeakObjectPtr<UTSICWebUISubsystem> Subsystem;
	FName ViewName;
	EMouseCursor::Type CurrentCursor = EMouseCursor::Default;
};

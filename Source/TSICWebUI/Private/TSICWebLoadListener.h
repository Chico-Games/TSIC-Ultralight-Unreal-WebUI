#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/Listener.h>
THIRD_PARTY_INCLUDES_END

class UTSICWebUISubsystem;

class FTSICWebLoadListener : public ultralight::LoadListener
{
public:
	FTSICWebLoadListener(UTSICWebUISubsystem* InSubsystem, FName InViewName);
	virtual ~FTSICWebLoadListener() override = default;

	virtual void OnWindowObjectReady(ultralight::View* Caller, uint64_t FrameId, bool bIsMainFrame, const ultralight::String& Url) override;
	virtual void OnDOMReady(ultralight::View* Caller, uint64_t FrameId, bool bIsMainFrame, const ultralight::String& Url) override;
	virtual void OnFinishLoading(ultralight::View* Caller, uint64_t FrameId, bool bIsMainFrame, const ultralight::String& Url) override;
	virtual void OnFailLoading(ultralight::View* Caller, uint64_t FrameId, bool bIsMainFrame, const ultralight::String& Url, const ultralight::String& Description, const ultralight::String& ErrorDomain, int32 ErrorCode) override;

private:
	TWeakObjectPtr<UTSICWebUISubsystem> Subsystem;
	FName ViewName;
};

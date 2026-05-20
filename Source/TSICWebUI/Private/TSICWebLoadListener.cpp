#include "TSICWebLoadListener.h"
#include "TSICWebJSBindings.h"
#include "TSICWebStringHelpers.h"
#include "TSICWebUI.h"
#include "TSICWebUISubsystem.h"

FTSICWebLoadListener::FTSICWebLoadListener(UTSICWebUISubsystem* InSubsystem, FName InViewName)
	: Subsystem(InSubsystem)
	, ViewName(InViewName)
{
}

void FTSICWebLoadListener::OnWindowObjectReady(ultralight::View* Caller, uint64_t /*FrameId*/, bool bIsMainFrame, const ultralight::String& Url)
{
	if (!bIsMainFrame || !Subsystem.IsValid())
	{
		return;
	}

	TSICWebUI::JSBindings::Install(Caller, Subsystem.Get(), ViewName);
	Subsystem->NotifyJSContextReady(ViewName);

	UE_LOG(LogTSICWebUI, Verbose, TEXT("[%s] OnWindowObjectReady => %s"),
		*ViewName.ToString(), *TSICWebUI::ULToFString(Url));
}

void FTSICWebLoadListener::OnDOMReady(ultralight::View* /*Caller*/, uint64_t /*FrameId*/, bool bIsMainFrame, const ultralight::String& Url)
{
	if (!bIsMainFrame)
	{
		return;
	}
	UE_LOG(LogTSICWebUI, Verbose, TEXT("[%s] OnDOMReady => %s"),
		*ViewName.ToString(), *TSICWebUI::ULToFString(Url));
}

void FTSICWebLoadListener::OnFinishLoading(ultralight::View* /*Caller*/, uint64_t /*FrameId*/, bool bIsMainFrame, const ultralight::String& Url)
{
	if (!bIsMainFrame)
	{
		return;
	}
	UE_LOG(LogTSICWebUI, Log, TEXT("[%s] finish-load => %s"),
		*ViewName.ToString(), *TSICWebUI::ULToFString(Url));

	// Nudge the Slate brush to re-bind: when LoadURL fires we create a fresh
	// UTexture2D, but Ultralight may keep painting the *previous* page into
	// that texture for several frames until the new DOM is up. By the time the
	// new page finishes loading, the slate widget is locked onto a brush that
	// contains the pre-transition content and only a window resize unsticks it.
	// Recreating the texture again here gives Slate a clean target whose first
	// painted frame is guaranteed to be the new page.
	if (Subsystem.IsValid())
	{
		Subsystem->RecreateViewTextureForLoad(ViewName);
	}
}

void FTSICWebLoadListener::OnFailLoading(ultralight::View* /*Caller*/, uint64_t /*FrameId*/, bool bIsMainFrame, const ultralight::String& Url, const ultralight::String& Description, const ultralight::String& /*ErrorDomain*/, int32 ErrorCode)
{
	if (!bIsMainFrame)
	{
		return;
	}
	UE_LOG(LogTSICWebUI, Error, TEXT("[%s] fail-load (%d) %s -- %s"),
		*ViewName.ToString(), ErrorCode,
		*TSICWebUI::ULToFString(Url),
		*TSICWebUI::ULToFString(Description));
}

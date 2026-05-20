#include "TSICWebViewListener.h"
#include "TSICWebStringHelpers.h"
#include "TSICWebUI.h"
#include "TSICWebUISubsystem.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/ConsoleMessage.h>
THIRD_PARTY_INCLUDES_END

namespace
{
	EMouseCursor::Type MapCursor(ultralight::Cursor InCursor)
	{
		using namespace ultralight;
		switch (InCursor)
		{
			case kCursor_Pointer:               return EMouseCursor::Default;
			case kCursor_Cross:                 return EMouseCursor::Crosshairs;
			case kCursor_Hand:                  return EMouseCursor::Hand;
			case kCursor_IBeam:                 return EMouseCursor::TextEditBeam;
			case kCursor_Wait:                  return EMouseCursor::SlashedCircle;
			case kCursor_Help:                  return EMouseCursor::Default;
			case kCursor_EastResize:            return EMouseCursor::ResizeLeftRight;
			case kCursor_WestResize:            return EMouseCursor::ResizeLeftRight;
			case kCursor_EastWestResize:        return EMouseCursor::ResizeLeftRight;
			case kCursor_ColumnResize:          return EMouseCursor::ResizeLeftRight;
			case kCursor_NorthResize:           return EMouseCursor::ResizeUpDown;
			case kCursor_SouthResize:           return EMouseCursor::ResizeUpDown;
			case kCursor_NorthSouthResize:      return EMouseCursor::ResizeUpDown;
			case kCursor_RowResize:             return EMouseCursor::ResizeUpDown;
			case kCursor_NorthEastResize:       return EMouseCursor::ResizeSouthWest;
			case kCursor_SouthWestResize:       return EMouseCursor::ResizeSouthWest;
			case kCursor_NorthEastSouthWestResize: return EMouseCursor::ResizeSouthWest;
			case kCursor_NorthWestResize:       return EMouseCursor::ResizeSouthEast;
			case kCursor_SouthEastResize:       return EMouseCursor::ResizeSouthEast;
			case kCursor_NorthWestSouthEastResize: return EMouseCursor::ResizeSouthEast;
			case kCursor_Move:                  return EMouseCursor::CardinalCross;
			case kCursor_VerticalText:          return EMouseCursor::TextEditBeam;
			case kCursor_Cell:                  return EMouseCursor::Crosshairs;
			case kCursor_ContextMenu:           return EMouseCursor::Default;
			case kCursor_Alias:                 return EMouseCursor::Hand;
			case kCursor_Progress:              return EMouseCursor::SlashedCircle;
			case kCursor_NoDrop:                return EMouseCursor::SlashedCircle;
			case kCursor_NotAllowed:            return EMouseCursor::SlashedCircle;
			case kCursor_Copy:                  return EMouseCursor::Hand;
			case kCursor_None:                  return EMouseCursor::None;
			case kCursor_ZoomIn:                return EMouseCursor::Crosshairs;
			case kCursor_ZoomOut:               return EMouseCursor::Crosshairs;
			case kCursor_Grab:                  return EMouseCursor::GrabHand;
			case kCursor_Grabbing:              return EMouseCursor::GrabHandClosed;
			default:                            return EMouseCursor::Default;
		}
	}
}

FTSICWebViewListener::FTSICWebViewListener(UTSICWebUISubsystem* InSubsystem, FName InViewName)
	: Subsystem(InSubsystem)
	, ViewName(InViewName)
{
}

void FTSICWebViewListener::OnChangeCursor(ultralight::View* /*Caller*/, ultralight::Cursor InCursor)
{
	CurrentCursor = MapCursor(InCursor);
}

void FTSICWebViewListener::OnAddConsoleMessage(ultralight::View* /*Caller*/, const ultralight::ConsoleMessage& Message)
{
	const FString Text = TSICWebUI::ULToFString(Message.message());
	const FString Source = TSICWebUI::ULToFString(Message.source_id());
	const uint32 Line = Message.line_number();

	switch (Message.level())
	{
		case ultralight::kMessageLevel_Error:
			UE_LOG(LogTSICWebUI, Error, TEXT("[%s:%d] [%s] %s"), *Source, Line, *ViewName.ToString(), *Text);
			break;
		case ultralight::kMessageLevel_Warning:
			UE_LOG(LogTSICWebUI, Warning, TEXT("[%s:%d] [%s] %s"), *Source, Line, *ViewName.ToString(), *Text);
			break;
		default:
			UE_LOG(LogTSICWebUI, Log, TEXT("[%s:%d] [%s] %s"), *Source, Line, *ViewName.ToString(), *Text);
			break;
	}
}

void FTSICWebViewListener::OnChangeTitle(ultralight::View* /*Caller*/, const ultralight::String& Title)
{
	UE_LOG(LogTSICWebUI, Verbose, TEXT("[%s] title => %s"), *ViewName.ToString(), *TSICWebUI::ULToFString(Title));
}

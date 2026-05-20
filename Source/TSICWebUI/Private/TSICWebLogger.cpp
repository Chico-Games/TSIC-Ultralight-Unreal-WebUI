#include "TSICWebLogger.h"
#include "TSICWebStringHelpers.h"
#include "TSICWebUI.h"

void FTSICWebLogger::LogMessage(ultralight::LogLevel Level, const ultralight::String& Message)
{
	const FString Text = TSICWebUI::ULToFString(Message);
	switch (Level)
	{
	case ultralight::LogLevel::Error:
		UE_LOG(LogTSICWebUI, Error, TEXT("[Ultralight] %s"), *Text);
		break;
	case ultralight::LogLevel::Warning:
		UE_LOG(LogTSICWebUI, Warning, TEXT("[Ultralight] %s"), *Text);
		break;
	case ultralight::LogLevel::Info:
	default:
		UE_LOG(LogTSICWebUI, Log, TEXT("[Ultralight] %s"), *Text);
		break;
	}
}

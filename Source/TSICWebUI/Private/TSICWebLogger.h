#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/platform/Logger.h>
THIRD_PARTY_INCLUDES_END

class FTSICWebLogger : public ultralight::Logger
{
public:
	virtual ~FTSICWebLogger() override = default;
	virtual void LogMessage(ultralight::LogLevel Level, const ultralight::String& Message) override;
};

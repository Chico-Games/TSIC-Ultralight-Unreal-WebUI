#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

struct FInputEvent;

namespace TSICWebUI
{
	// Maps a Slate FKey to the Ultralight Windows-equivalent virtual key code
	// (KeyCodes::GK_*). Returns 0 (GK_UNKNOWN) for unknown keys.
	int32 SlateKeyToVirtualKeyCode(const FKey& Key);

	// Builds Ultralight KeyEvent::Modifiers bitmask from a Slate input event.
	uint32 ModifiersFromInputEvent(const FInputEvent& InputEvent);

	// True for keys that live on the numeric keypad.
	bool IsKeypadKey(const FKey& Key);
}

#include "TSICWebKeyMappings.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"

THIRD_PARTY_INCLUDES_START
#include <Ultralight/KeyCodes.h>
#include <Ultralight/KeyEvent.h>
THIRD_PARTY_INCLUDES_END

namespace TSICWebUI
{
	int32 SlateKeyToVirtualKeyCode(const FKey& Key)
	{
		using namespace ultralight::KeyCodes;

		// Letters
		if (Key == EKeys::A) return GK_A;
		if (Key == EKeys::B) return GK_B;
		if (Key == EKeys::C) return GK_C;
		if (Key == EKeys::D) return GK_D;
		if (Key == EKeys::E) return GK_E;
		if (Key == EKeys::F) return GK_F;
		if (Key == EKeys::G) return GK_G;
		if (Key == EKeys::H) return GK_H;
		if (Key == EKeys::I) return GK_I;
		if (Key == EKeys::J) return GK_J;
		if (Key == EKeys::K) return GK_K;
		if (Key == EKeys::L) return GK_L;
		if (Key == EKeys::M) return GK_M;
		if (Key == EKeys::N) return GK_N;
		if (Key == EKeys::O) return GK_O;
		if (Key == EKeys::P) return GK_P;
		if (Key == EKeys::Q) return GK_Q;
		if (Key == EKeys::R) return GK_R;
		if (Key == EKeys::S) return GK_S;
		if (Key == EKeys::T) return GK_T;
		if (Key == EKeys::U) return GK_U;
		if (Key == EKeys::V) return GK_V;
		if (Key == EKeys::W) return GK_W;
		if (Key == EKeys::X) return GK_X;
		if (Key == EKeys::Y) return GK_Y;
		if (Key == EKeys::Z) return GK_Z;

		// Top-row digits
		if (Key == EKeys::Zero) return GK_0;
		if (Key == EKeys::One) return GK_1;
		if (Key == EKeys::Two) return GK_2;
		if (Key == EKeys::Three) return GK_3;
		if (Key == EKeys::Four) return GK_4;
		if (Key == EKeys::Five) return GK_5;
		if (Key == EKeys::Six) return GK_6;
		if (Key == EKeys::Seven) return GK_7;
		if (Key == EKeys::Eight) return GK_8;
		if (Key == EKeys::Nine) return GK_9;

		// Function keys
		if (Key == EKeys::F1) return GK_F1;
		if (Key == EKeys::F2) return GK_F2;
		if (Key == EKeys::F3) return GK_F3;
		if (Key == EKeys::F4) return GK_F4;
		if (Key == EKeys::F5) return GK_F5;
		if (Key == EKeys::F6) return GK_F6;
		if (Key == EKeys::F7) return GK_F7;
		if (Key == EKeys::F8) return GK_F8;
		if (Key == EKeys::F9) return GK_F9;
		if (Key == EKeys::F10) return GK_F10;
		if (Key == EKeys::F11) return GK_F11;
		if (Key == EKeys::F12) return GK_F12;

		// Arrows / navigation
		if (Key == EKeys::Up) return GK_UP;
		if (Key == EKeys::Down) return GK_DOWN;
		if (Key == EKeys::Left) return GK_LEFT;
		if (Key == EKeys::Right) return GK_RIGHT;
		if (Key == EKeys::Home) return GK_HOME;
		if (Key == EKeys::End) return GK_END;
		if (Key == EKeys::PageUp) return GK_PRIOR;
		if (Key == EKeys::PageDown) return GK_NEXT;
		if (Key == EKeys::Insert) return GK_INSERT;
		if (Key == EKeys::Delete) return GK_DELETE;

		// Editing / whitespace
		if (Key == EKeys::BackSpace) return GK_BACK;
		if (Key == EKeys::Tab) return GK_TAB;
		if (Key == EKeys::Enter) return GK_RETURN;
		if (Key == EKeys::SpaceBar) return GK_SPACE;
		if (Key == EKeys::Escape) return GK_ESCAPE;
		if (Key == EKeys::CapsLock) return GK_CAPITAL;
		if (Key == EKeys::Pause) return GK_PAUSE;

		// Modifiers
		if (Key == EKeys::LeftShift) return GK_LSHIFT;
		if (Key == EKeys::RightShift) return GK_RSHIFT;
		if (Key == EKeys::LeftControl) return GK_LCONTROL;
		if (Key == EKeys::RightControl) return GK_RCONTROL;
		if (Key == EKeys::LeftAlt) return GK_LMENU;
		if (Key == EKeys::RightAlt) return GK_RMENU;
		if (Key == EKeys::LeftCommand) return GK_LWIN;
		if (Key == EKeys::RightCommand) return GK_RWIN;

		// Numpad
		if (Key == EKeys::NumPadZero) return GK_NUMPAD0;
		if (Key == EKeys::NumPadOne) return GK_NUMPAD1;
		if (Key == EKeys::NumPadTwo) return GK_NUMPAD2;
		if (Key == EKeys::NumPadThree) return GK_NUMPAD3;
		if (Key == EKeys::NumPadFour) return GK_NUMPAD4;
		if (Key == EKeys::NumPadFive) return GK_NUMPAD5;
		if (Key == EKeys::NumPadSix) return GK_NUMPAD6;
		if (Key == EKeys::NumPadSeven) return GK_NUMPAD7;
		if (Key == EKeys::NumPadEight) return GK_NUMPAD8;
		if (Key == EKeys::NumPadNine) return GK_NUMPAD9;
		if (Key == EKeys::Multiply) return GK_MULTIPLY;
		if (Key == EKeys::Add) return GK_ADD;
		if (Key == EKeys::Subtract) return GK_SUBTRACT;
		if (Key == EKeys::Decimal) return GK_DECIMAL;
		if (Key == EKeys::Divide) return GK_DIVIDE;

		// OEM punctuation (US layout)
		if (Key == EKeys::Semicolon) return GK_OEM_1;
		if (Key == EKeys::Equals) return GK_OEM_PLUS;
		if (Key == EKeys::Comma) return GK_OEM_COMMA;
		if (Key == EKeys::Hyphen || Key == EKeys::Underscore) return GK_OEM_MINUS;
		if (Key == EKeys::Period) return GK_OEM_PERIOD;
		if (Key == EKeys::Slash) return GK_OEM_2;
		if (Key == EKeys::Tilde) return GK_OEM_3;
		if (Key == EKeys::LeftBracket) return GK_OEM_4;
		if (Key == EKeys::Backslash) return GK_OEM_5;
		if (Key == EKeys::RightBracket) return GK_OEM_6;
		if (Key == EKeys::Apostrophe || Key == EKeys::Quote) return GK_OEM_7;

		// Gamepad — synthesized to navigation / activation keys so default browser
		// behaviour (button focus, click) works without a custom Gamepad API page.
		if (Key == EKeys::Gamepad_DPad_Up) return GK_UP;
		if (Key == EKeys::Gamepad_DPad_Down) return GK_DOWN;
		if (Key == EKeys::Gamepad_DPad_Left) return GK_LEFT;
		if (Key == EKeys::Gamepad_DPad_Right) return GK_RIGHT;
		if (Key == EKeys::Gamepad_FaceButton_Bottom) return GK_RETURN; // A / cross
		if (Key == EKeys::Gamepad_FaceButton_Right) return GK_ESCAPE;  // B / circle
		if (Key == EKeys::Gamepad_FaceButton_Top) return GK_F1;        // Y / triangle
		if (Key == EKeys::Gamepad_FaceButton_Left) return GK_TAB;      // X / square

		return GK_UNKNOWN;
	}

	uint32 ModifiersFromInputEvent(const FInputEvent& InputEvent)
	{
		uint32 Mods = 0;
		if (InputEvent.IsAltDown())     { Mods |= ultralight::KeyEvent::kMod_AltKey; }
		if (InputEvent.IsControlDown()) { Mods |= ultralight::KeyEvent::kMod_CtrlKey; }
		if (InputEvent.IsShiftDown())   { Mods |= ultralight::KeyEvent::kMod_ShiftKey; }
		if (InputEvent.IsCommandDown()) { Mods |= ultralight::KeyEvent::kMod_MetaKey; }
		return Mods;
	}

	bool IsKeypadKey(const FKey& Key)
	{
		return Key == EKeys::NumPadZero || Key == EKeys::NumPadOne || Key == EKeys::NumPadTwo
			|| Key == EKeys::NumPadThree || Key == EKeys::NumPadFour || Key == EKeys::NumPadFive
			|| Key == EKeys::NumPadSix || Key == EKeys::NumPadSeven || Key == EKeys::NumPadEight
			|| Key == EKeys::NumPadNine || Key == EKeys::Multiply || Key == EKeys::Add
			|| Key == EKeys::Subtract || Key == EKeys::Decimal || Key == EKeys::Divide;
	}
}

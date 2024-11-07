// A common interface for keyboard, mouse and gamepad input.

namespace Input {

enum class Key {
	Invalid = 0,

	Space = 32,
	Apostrophe = 39,   /* ' */
	Comma = 44,        /* , */
	Minus = 45,        /* - */
	Period = 46,       /* . */
	Slash = 47,        /* / */

	_0 = 48,
	_1 = 49,
	_2 = 50,
	_3 = 51,
	_4 = 52,
	_5 = 53,
	_6 = 54,
	_7 = 55,
	_8 = 56,
	_9 = 57,

	Semicolon = 59,    /* ; */
	Equal = 61,        /* = */
	LeftBracket = 91,  /* [ */
	Backslash = 92,    /* \ */
	RightBracket = 93, /* ] */
	GraveAccent = 96,  /* ` */

	A = 65,
	B = 66,
	C = 67,
	D = 68,
	E = 69,
	F = 70,
	G = 71,
	H = 72,
	I = 73,
	J = 74,
	K = 75,
	L = 76,
	M = 77,
	N = 78,
	O = 79,
	P = 80,
	Q = 81,
	R = 82,
	S = 83,
	T = 84,
	U = 85,
	V = 86,
	W = 87,
	X = 88,
	Y = 89,
	Z = 90,

	Escape = 256,
	Enter = 257,
	Tab = 258,
	Backspace = 259,
	Insert = 260,
	Delete = 261,
	Right = 262,
	Left = 263,
	Down = 264,
	Up = 265,
	PageUp = 266,
	PageDown = 267,
	Home = 268,
	End = 269,
	CapsLock = 280,
	ScrollLock = 281,
	NumLock = 282,
	PrintScreen = 283,
	Pause = 284,

	F1 = 290,
	F2 = 291,
	F3 = 292,
	F4 = 293,
	F5 = 294,
	F6 = 295,
	F7 = 296,
	F8 = 297,
	F9 = 298,
	F10 = 299,
	F11 = 300,
	F12 = 301,
	F13 = 302,
	F14 = 303,
	F15 = 304,
	F16 = 305,
	F17 = 306,
	F18 = 307,
	F19 = 308,
	F20 = 309,
	F21 = 310,
	F22 = 311,
	F23 = 312,
	F24 = 313,
	F25 = 314,

	KP_0 = 320,
	KP_1 = 321,
	KP_2 = 322,
	KP_3 = 323,
	KP_4 = 324,
	KP_5 = 325,
	KP_6 = 326,
	KP_7 = 327,
	KP_8 = 328,
	KP_9 = 329,

	KP_Decimal = 330,
	KP_Divide = 331,
	KP_Multiply = 332,
	KP_Subtract = 333,
	KP_Add = 334,
	KP_Enter = 335,
	KP_Equal = 336,

	LeftShift = 340,
	LeftControl = 341,
	LeftAlt = 342,
	LeftSuper = 343,
	RightShift = 344,
	RightControl = 345,
	RightAlt = 346,
	RightSuper = 347,
	Menu = 348,

	// Events for these four modifier keys shouldn't be generated, nor should they be used in the `key_is_down` table.
	// They're purely for convenience when calling the IsDown/WentDown/WentDownOrRepeat/WentUp functions.
	Shift = 349,
	Control = 350,
	Alt = 351,
	Super = 352,

	MouseLeft = 353,
	MouseRight = 354,
	MouseMiddle = 355,
	Mouse_4 = 356,
	Mouse_5 = 357,
	Mouse_6 = 358,
	Mouse_7 = 359,
	Mouse_8 = 360,

	COUNT,
};

enum class EventKind {
	Press,
	Repeat,
	Release,
	TextCharacter,
};

struct Event {
	EventKind kind;
	union {
		Key key; // for Press, Repeat, Release events
		unsigned int text_character; // unicode character for TextCharacter event
	};
};

struct Frame {
	DS_DynArray<Event> events;
	bool key_is_down[(int)Key::COUNT]; // This is the key down state after the events for this frame have been applied
	float mouse_wheel_input[2]; // +1.0 means the wheel was rotated forward by one detent (scroll step)
	float raw_mouse_input[2];
	
	inline bool KeyIsDown(Key key) const;
	inline bool KeyWentDown(Key key) const;
	inline bool KeyWentDownOrRepeat(Key key) const;
	inline bool KeyWentUp(Key key) const;
};

// ---------------------------------------------------------------------------------

static void ResetFrame(Frame* frame, DS_Arena* temp_arena) {
	DS_ArrInit(&frame->events, temp_arena);
	frame->mouse_wheel_input[0] = 0.f;
	frame->mouse_wheel_input[1] = 0.f;
	frame->raw_mouse_input[0] = 0.f;
	frame->raw_mouse_input[1] = 0.f;
}

static bool KeyIsA(Key key, Key other_key) {
	if (key == other_key) return true;
	if (other_key >= Key::Shift && other_key <= Key::Super) {
		switch (other_key) {
		case Key::Shift:    return key == Key::LeftShift   || key == Key::RightShift;
		case Key::Control:  return key == Key::LeftControl || key == Key::RightControl;
		case Key::Alt:      return key == Key::LeftAlt     || key == Key::RightAlt;
		case Key::Super:    return key == Key::LeftSuper   || key == Key::RightSuper;
		default: break;
		}
	}
	return false;
}

inline bool Frame::KeyIsDown(Key key) const {
	if (key >= Key::Shift && key <= Key::Super) {
		switch (key) {
		case Key::Shift:    return key_is_down[(int)Key::LeftShift]   || key_is_down[(int)Key::RightShift];
		case Key::Control:  return key_is_down[(int)Key::LeftControl] || key_is_down[(int)Key::RightControl];
		case Key::Alt:      return key_is_down[(int)Key::LeftAlt]     || key_is_down[(int)Key::RightAlt];
		case Key::Super:    return key_is_down[(int)Key::LeftSuper]   || key_is_down[(int)Key::RightSuper];
		default: break;
		}
	}
	return key_is_down[(int)key];
};

inline bool Frame::KeyWentDown(Key key) const {
	if (KeyIsDown(key)) {
		for (int i = 0; i < events.count; i++) {
			if (events[i].kind == EventKind::Press && KeyIsA(events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}

inline bool Frame::KeyWentDownOrRepeat(Key key) const {
	if (KeyIsDown(key)) {
		for (int i = 0; i < events.count; i++) {
			if ((events[i].kind == EventKind::Press || events[i].kind == EventKind::Repeat) && KeyIsA(events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}

inline bool Frame::KeyWentUp(Key key) const {
	if (!KeyIsDown(key)) {
		for (int i = 0; i < events.count; i++) {
			if (events[i].kind == EventKind::Release && KeyIsA(events[i].key, key)) {
				return true;
			}
		}
	}
	return false;
}

} // namespace Input

namespace Input {

static void OS_AddEvent(Frame* frame, const OS_Event& event) {
	if (event.kind == OS_EventKind_Press) {
		Key key = (Key)event.key; // NOTE: OS_Key and Key must be kept in sync!
		frame->key_is_down[event.key] = true;
		
		Event input_event = {};
		input_event.kind = event.is_repeat ? EventKind::Repeat : EventKind::Press;
		input_event.key = key;
		DS_ArrPush(&frame->events, input_event);
	}
	if (event.kind == OS_EventKind_Release) {
		Key key = (Key)event.key; // NOTE: OS_Key and Key must be kept in sync!
		frame->key_is_down[event.key] = false;

		Event input_event = {};
		input_event.kind = EventKind::Release;
		input_event.key = key;
		DS_ArrPush(&frame->events, input_event);
	}
	if (event.kind == OS_EventKind_TextCharacter) {
		Event input_event = {};
		input_event.kind = EventKind::TextCharacter;
		input_event.text_character = event.text_character;
		DS_ArrPush(&frame->events, input_event);
	}
	if (event.kind == OS_EventKind_MouseWheel) {
		frame->mouse_wheel_input[1] += event.mouse_wheel;
	}
	if (event.kind == OS_EventKind_RawMouseInput) {
		frame->raw_mouse_input[0] += event.raw_mouse_input[0];
		frame->raw_mouse_input[1] += event.raw_mouse_input[1];
	}
}

} // namespace Input
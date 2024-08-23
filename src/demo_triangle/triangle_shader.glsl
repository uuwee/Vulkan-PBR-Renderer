#ifdef GPU_STAGE_VERTEX
	layout (location = 0) in vec2 in_position;
	layout (location = 1) in vec3 in_color;

	layout(location = 0) out vec3 out_color;
	
	void main() {
		out_color = in_color;
		gl_Position = vec4(in_position, 0, 1);
	}
#else
	layout(location = 0) in vec3 in_color;

	layout (location = 0) out vec4 out_color;
	
	void main() {
		out_color = vec4(in_color, 1);
	}
#endif
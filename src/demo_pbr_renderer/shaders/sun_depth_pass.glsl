struct Globals {
	mat4 clip_space_from_world;
	mat4 clip_space_from_view;
	mat4 world_space_from_clip;
	mat4 view_space_from_clip;
	mat4 view_space_from_world;
	mat4 world_space_from_view;
	mat4 sun_space_from_world;
	mat4 old_clip_space_from_world;
	vec4 sun_direction;
	vec3 camera_pos;
	float frame_idx_mod_59;
	float lightgrid_scale;
	uint visualize_light_grid;
};

#define PI 3.14159265358979323846
#define GOLDEN_RATIO 1.61803398875

#ifdef GPU_STAGE_VERTEX
	GPU_BINDING(GLOBALS) {
		Globals data;
	} GLOBALS;
	
	layout (location = 0) in vec3 vs_position;
	layout (location = 1) in vec3 vs_normal;
	layout (location = 2) in vec3 vs_tangent;
	layout (location = 3) in vec2 vs_tex_coord;
	
	void main() {
		gl_Position = GLOBALS.data.sun_space_from_world * vec4(vs_position, 1.);
	}
#else
	void main() {}
#endif
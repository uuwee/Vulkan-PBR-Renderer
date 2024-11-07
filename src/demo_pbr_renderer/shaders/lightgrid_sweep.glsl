layout (local_size_x = 1, local_size_y = 8, local_size_z = 8) in;

GPU_BINDING(IMG0) image3D LIGHTMAP_IMG;

layout(push_constant) uniform Constants {
	int X_direction;
} constants;

void main() {
	ivec3 x_dir = ivec3(1, 0, 0);
	ivec3 base_coord = ivec3(0, gl_GlobalInvocationID.yz);
	
	if (constants.X_direction == 0) {}
	else if (constants.X_direction == 1) {
		base_coord = base_coord.zxy;
		x_dir = ivec3(0, 1, 0);
	}
	else {
		base_coord = base_coord.yzx;
		x_dir = ivec3(0, 0, 1);
	}
	
	vec3 SKYLIGHT = vec3(1., 1.2, 2.);
	
	vec4 old_values[128];
	vec4 values[128];
	for (int x = 0; x < 128; x++) {
		values[x] = imageLoad(LIGHTMAP_IMG, base_coord + x*x_dir);
		old_values[x] = values[x];
	}
	
	float move_ratio = 0.5;
	
	// Sweep left to right
	vec3 moving_light = SKYLIGHT;
	for (int x = 0; x < 128; x++) {
		vec4 old_value = old_values[x];
		
		if (old_value.a > 0.5) {
			moving_light = old_value.xyz;
		}
		else {
			values[x].xyz += moving_light;
			moving_light = move_ratio*values[x].xyz;
			values[x].xyz -= moving_light;
		}
	}
	values[127].xyz += moving_light; // Make sure there's no energy loss
	
	// Sweep right to left
	moving_light = SKYLIGHT;
	for (int x = 128 - 1; x >= 0; x--) {
		vec4 old_value = old_values[x];
		
		if (old_value.a > 0.5) {
			moving_light = old_value.xyz;
		}
		else {
			values[x].xyz += moving_light;
			moving_light = move_ratio*values[x].xyz;
			values[x].xyz -= moving_light;
			
			// imageStore(LIGHTMAP_IMG, base_coord + x*x_dir, values[x]);
		}
	}
	values[0].xyz += moving_light; // Make sure there's no energy loss
	
	// Store results
	for (int x = 0; x < 128; x++) {
		vec4 mixed = mix(old_values[x], values[x], 0.35);
		if (old_values[x].a < 0.5) {
			imageStore(LIGHTMAP_IMG, base_coord + x*x_dir, mixed);
		}
	}
}

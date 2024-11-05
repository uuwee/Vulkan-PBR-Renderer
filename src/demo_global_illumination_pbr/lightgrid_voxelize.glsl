#extension GL_ARB_gpu_shader_int64 : require
#extension GL_AMD_gpu_shader_half_float : enable
#extension GL_EXT_shader_image_int64 : require

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
	float alt_is_held_down;
};

#ifdef GPU_STAGE_VERTEX
	GPU_BINDING(GLOBALS) { Globals data; } GLOBALS;

	GPU_BINDING(SSBO0) {
		float data[];
	} VERTEX_BUFFER;
	
	GPU_BINDING(SSBO1) {
		uint data[];
	} INDEX_BUFFER;
	
	layout(location = 0) out vec3 fs_position_ndc;
	layout(location = 1) out vec3 fs_position_ws;
	layout(location = 2) out vec3 fs_tri_normal;
	layout(location = 3) out vec2 fs_tex_coord;
	
	void main() {
		uint this_vertex = gl_VertexIndex % 3;
		uint base_idx = gl_VertexIndex - this_vertex;
		uint v0 = INDEX_BUFFER.data[base_idx];
		uint v1 = INDEX_BUFFER.data[base_idx+1];
		uint v2 = INDEX_BUFFER.data[base_idx+2];
		
		// We need to access the neighbouring index.
		// Vertex layout:
		// vec3 position, vec3 normal, vec3 tangent, vec2 uv
		// total size: 11 dwords
		vec3 positions[3] = {
			vec3(VERTEX_BUFFER.data[v0*11], VERTEX_BUFFER.data[v0*11 + 1], VERTEX_BUFFER.data[v0*11 + 2]),
			vec3(VERTEX_BUFFER.data[v1*11], VERTEX_BUFFER.data[v1*11 + 1], VERTEX_BUFFER.data[v1*11 + 2]),
			vec3(VERTEX_BUFFER.data[v2*11], VERTEX_BUFFER.data[v2*11 + 1], VERTEX_BUFFER.data[v2*11 + 2]),
		};
		
		vec2 tex_coord = vec2(VERTEX_BUFFER.data[(base_idx+this_vertex)*11 + 9], VERTEX_BUFFER.data[(base_idx+this_vertex)*11 + 10]);
		
		vec3 tri_normal = cross(positions[1] - positions[0], positions[2] - positions[0]);
		vec3 tri_normal_abs = abs(tri_normal);
		
		vec3 pos_grid_ndc = positions[this_vertex] * GLOBALS.data.lightgrid_scale;
		vec3 pos_ndc = pos_grid_ndc;
		
		float tri_normal_max = max(max(tri_normal_abs.x, tri_normal_abs.y), tri_normal_abs.z);
		if (tri_normal_max == tri_normal_abs.x) { // Triangle is largest when projected along X axis
			pos_ndc = pos_ndc.yzx;
		}
		else if (tri_normal_max == tri_normal_abs.y) { // // Triangle is largest when projected along Y axis
			pos_ndc = pos_ndc.zxy;
		}
		// Otherwise Z and we don't need to do anything.
		
		pos_ndc.z = pos_ndc.z*0.5 + 0.5; // Vulkan clipped viewport in Z is [0, 1]
		
		fs_position_ws = positions[this_vertex];
		fs_position_ndc = pos_grid_ndc;
		fs_tri_normal = normalize(tri_normal);
		fs_tex_coord = tex_coord;
		gl_Position = vec4(pos_ndc, 1);
	}

#else
	GPU_BINDING(GLOBALS) { Globals data; } GLOBALS;
	GPU_BINDING(SUN_DEPTH_MAP) texture2D SUN_DEPTH_MAP;
	GPU_BINDING(TEX0) texture2D TEX_BASE_COLOR;
	GPU_BINDING(TEX_EMISSIVE) texture2D TEX_EMISSIVE;
	GPU_BINDING(IMG0) image3D LIGHTMAP_IMG;
	GPU_BINDING(SAMPLER_PERCENTAGE_CLOSER) samplerShadow SAMPLER_PERCENTAGE_CLOSER;
	GPU_BINDING(SAMPLER_LINEAR_WRAP) sampler SAMPLER_LINEAR_WRAP;

	layout(location = 0) in vec3 fs_position_ndc;
	layout(location = 1) in vec3 fs_position_ws;
	layout(location = 2) in vec3 fs_tri_normal;
	layout(location = 3) in vec2 fs_tex_coord;
	
	void main() {
		// if (fs_tri_normal.z > 0.5) {
			// if (clamp(uvw_pos, vec3(0), vec3(1)) == uvw_pos) {
		
		// TODO: we need base color!!
		
		const float sun_shadow_map_pixel_size = (1. / 2048.);
		
		vec3 sun_emission = 5.*vec3(1, 0.9, 0.7);
		
		vec3 p0_sun_space = (GLOBALS.data.sun_space_from_world * vec4(fs_position_ws, 1)).xyz;
		p0_sun_space.xy = p0_sun_space.xy*0.5 + 0.5;
		p0_sun_space.z -= 0.001;
		// p0_sun_space.xy += 2.*vec2(noise_1 - 0.5, noise_2 - 0.5) * sun_shadow_map_pixel_size;
		
		// float shadow = texture(sampler2DShadow(SUN_DEPTH_MAP, SAMPLER_PERCENTAGE_CLOSER), p0_sun_space);
		float shadow = texture(sampler2DShadow(SUN_DEPTH_MAP, SAMPLER_PERCENTAGE_CLOSER), p0_sun_space + vec3(+1., +1., 0.) * sun_shadow_map_pixel_size);
		
		vec3 L = -GLOBALS.data.sun_direction.xyz;
		float LdotN = max(dot(L, fs_tri_normal), 0.);
		// float VdotN = max(dot(V, N), 0.);
		
		// vec3 emissive = 1.*texture(sampler2D(TEX_EMISSIVE, SAMPLER_LINEAR_WRAP), vec2(0.714571, 1.-0.85911)).xyz;
		vec3 base_color = texture(sampler2D(TEX_BASE_COLOR, SAMPLER_LINEAR_WRAP), fs_tex_coord).xyz;
		vec3 emissive = texture(sampler2D(TEX_EMISSIVE, SAMPLER_LINEAR_WRAP), fs_tex_coord).xyz;
		
		// if (emissive.b > 0.8) emissive = vec3(0, 0, 1);
		// else emissive *= 0.;
		
		vec3 uvw_pos = (fs_position_ndc)*0.5 + 0.5;
		ivec3 coord = ivec3(uvw_pos * 128.);
		if (clamp(coord, ivec3(0), ivec3(127)) == coord) {
			
			// let's do atomic min or atomic max just to get temporal stability. The op doesn't really matter.
			vec4 value = vec4(emissive + shadow*base_color*LdotN*sun_emission, 1);
			imageStore(LIGHTMAP_IMG, coord, value);
			
			// uint packed_a = packFloat2x16(f16vec2(value.rg));
			// uint packed_b = packFloat2x16(f16vec2(value.ba));
			// uint64_t value_packed = (uint64_t(packed_b) << uint64_t(32)) | uint64_t(packed_a);
			// imageAtomicMax(LIGHTMAP_IMG, coord, value_packed);
		}
	}

#endif
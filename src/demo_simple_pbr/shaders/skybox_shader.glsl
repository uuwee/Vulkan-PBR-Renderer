struct Globals {
	mat4 clip_space_from_world;
	vec3 camera_pos;
};

// from https://64.github.io/tonemapping/
vec3 aces_approx(vec3 v) {
	v *= 0.6;
	float a = 2.51;
	float b = 0.03;
	float c = 2.43;
	float d = 0.59;
	float e = 0.14;
	return clamp((v*(a*v+b))/(v*(c*v+d)+e), 0.0, 1.0);
}

layout(push_constant) uniform Constants {
	float time;
} constants;

#ifdef GPU_STAGE_VERTEX
	GPU_BINDING(GLOBALS) { Globals data; } GLOBALS;
	layout (location = 0) in vec3 vs_position;
	
	layout (location = 0) out vec3 fs_position;
	
	void main() {
		vec4 position_clip = GLOBALS.data.clip_space_from_world * vec4(500.*vs_position, 1.);
		position_clip.y *= -1;
		
		fs_position = vs_position;
		gl_Position = position_clip;
	}
#else
	GPU_BINDING(SAMPLER_LINEAR_CLAMP) sampler SAMPLER_LINEAR_CLAMP;
	GPU_BINDING(TEX_ENV_CUBE) textureCube TEX_ENV_CUBE;
	GPU_BINDING(PREFILTERED_ENV_MAP) textureCube PREFILTERED_ENV_MAP;
	
	layout (location = 0) in vec3 fs_position;
	
	layout (location = 0) out vec4 out_color;
	
	void main() {
		vec3 sample_dir = normalize(fs_position);
		
		// vec3 env_color = textureLod(samplerCube(PREFILTERED_ENV_MAP, SAMPLER_LINEAR_CLAMP), sample_dir, fract(constants.time/4.)*4.).rgb;
		vec3 env_color = textureLod(samplerCube(PREFILTERED_ENV_MAP, SAMPLER_LINEAR_CLAMP), sample_dir, 1.).rgb;
			
		//vec3 env_color = textureLod(samplerCube(TEX_ENV_CUBE, SAMPLER_LINEAR_CLAMP), sample_dir, 6.).rgb;
		// out_color = vec4(pow(AcesFilmicTonemap(env_color), vec3(1./2.2)), 1);
		// out_color = vec4(pow(ACESFitted(env_color), vec3(1./2.2)), 1);
		out_color = vec4(pow(aces_approx(env_color), vec3(1./2.2)), 1);
	}
#endif
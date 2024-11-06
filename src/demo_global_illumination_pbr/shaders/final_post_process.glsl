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

#ifdef GPU_STAGE_VERTEX
	layout(location = 0) out vec2 fs_uv;
	
	// Generate fullscreen triangle
	void main() {
		vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2. - vec2(1.);
		fs_uv = uv*0.5 + 0.5;
		gl_Position = vec4(uv.x, uv.y, 0, 1);
	}

#else
	// GPU_BINDING(TEX0) texture2D LIGHTING_RESULT;
	layout(location = 0) out vec4 out_color;
	
	layout(location = 0) in vec2 fs_uv;
	
	GPU_BINDING(TEX0) texture2D BLOOM_RESULT;
	GPU_BINDING(SAMPLER_LINEAR_CLAMP) sampler SAMPLER_LINEAR_CLAMP;
	
	void main() {
		vec3 color_hdr = 2.*texture(sampler2D(BLOOM_RESULT, SAMPLER_LINEAR_CLAMP), fs_uv).xyz;
		out_color = vec4(pow(aces_approx(color_hdr), vec3(1./2.2)), 1.);
	}
#endif
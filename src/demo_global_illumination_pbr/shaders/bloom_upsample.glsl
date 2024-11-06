layout(push_constant) uniform Constants {
	int dst_mip_level;
} PC;

#ifdef GPU_STAGE_VERTEX
	layout(location = 0) out vec2 fs_uv;

	// Generate fullscreen triangle
	void main() {
		vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2. - vec2(1.);
		fs_uv = uv*0.5 + 0.5;
		gl_Position = vec4(uv.x, uv.y, 0, 1);
	}

#else
	layout(location = 0) in vec2 fs_uv;
	
	layout(location = 0) out vec4 out_color;
	
	GPU_BINDING(TEX0) texture2D BLOOM_INPUT;
	GPU_BINDING(SAMPLER_LINEAR_CLAMP) sampler SAMPLER_LINEAR_CLAMP;

	void main() {
		vec2 src_texture_size = vec2(textureSize(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), 0));
		
		float radius = 1.5;
		float x = radius / src_texture_size.x;
		float y = radius / src_texture_size.y;

		// vec2 texture_size = vec2(textureSize(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), 0));
		// vec2 texel_size = vec2(1) / texture_size;
		// vec2 uv = gl_FragCoord.xy * texel_size;
		
		float factor = 1.;
		// factor *= float(PC.dst_mip_level);
		// float factor = 1.;
		if (PC.dst_mip_level == 0) factor = 0.06;
		// if (PC.dst_mip_level == 0) factor = 0.04;
		
		// a - b - c
		// d - e - f
		// g - h - i
		vec3 a = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(-x, -y), 0.).xyz;
		vec3 b = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(0,  -y), 0.).xyz;
		vec3 c = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(x,  -y), 0.).xyz;
		vec3 d = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(-x,  0), 0.).xyz;
		vec3 e = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(0,   0), 0.).xyz;
		vec3 f = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(x,   0), 0.).xyz;
		vec3 g = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(-x,  y), 0.).xyz;
		vec3 h = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(0,   y), 0.).xyz;
		vec3 i = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(x,   y), 0.).xyz;
		
		// see https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
		
		vec3 sum = e*4.0;
		sum += (b+d+f+h)*2.0;
		sum += (a+c+g+i);
		out_color = vec4(sum * factor / 16.0, 1);
	}
#endif
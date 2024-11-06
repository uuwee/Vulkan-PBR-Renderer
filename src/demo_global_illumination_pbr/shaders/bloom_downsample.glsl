layout(push_constant) uniform Constants {
	int dst_mip_level;
} PC;

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
	layout(location = 0) in vec2 fs_uv;
	
	layout(location = 0) out vec4 out_color;
	
	GPU_BINDING(TEX0) texture2D BLOOM_INPUT;
	GPU_BINDING(SAMPLER_LINEAR_CLAMP) sampler SAMPLER_LINEAR_CLAMP;

	float Luminance(vec3 v) {
		return dot(v, vec3(0.2127, 0.7152, 0.0722));
	}
	
	void main() {
		vec2 src_texture_size = vec2(textureSize(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), 0));
		float x = 1. / src_texture_size.x;
		float y = 1. / src_texture_size.y;
		
		// a - b - c
		// - j - k -
		// d - e - f
		// - l - m -
		// g - h - i
		
		vec3 a = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(-2.*x, -2.*y), 0.).xyz;
		vec3 b = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(0,     -2.*y), 0.).xyz;
		vec3 c = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(2.*x,  -2.*y), 0.).xyz;
		vec3 d = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(-2.*x,  0), 0.).xyz;
		vec3 e = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(0,      0), 0.).xyz;
		vec3 f = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(2.*x,   0), 0.).xyz;
		vec3 g = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(-2.*x,  2.*y), 0.).xyz;
		vec3 h = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(0,      2.*y), 0.).xyz;
		vec3 i = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(2.*x,   2.*y), 0.).xyz;
		
		vec3 j = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(-x, -y), 0.).xyz;
		vec3 k = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(x, -y), 0.).xyz;
		vec3 l = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(-x, y), 0.).xyz;
		vec3 m = textureLod(sampler2D(BLOOM_INPUT, SAMPLER_LINEAR_CLAMP), fs_uv + vec2(x, y), 0.).xyz;
		
		// see https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
		
		// if (PC.dst_mip_level == 1) { // try to reduce fireflies
		// // if (true) { // try to reduce fireflies
		// // if (false) {
		// 	vec3 block_1 = (a+b+d+e) * (0.125/4.0);
		// 	vec3 block_2 = (b+c+e+f) * (0.125/4.0);
		// 	vec3 block_3 = (d+e+g+h) * (0.125/4.0);
		// 	vec3 block_4 = (e+f+h+i) * (0.125/4.0);
		// 	vec3 block_5 = (j+k+l+m) * (0.5/4.0);
			
		// 	// weighted average (https://graphicrants.blogspot.com/2013/12/tone-mapping.html)
		// 	float weight_1 = 1. / (1. + max(Luminance(block_1), 0.));
		// 	float weight_2 = 1. / (1. + max(Luminance(block_2), 0.));
		// 	float weight_3 = 1. / (1. + max(Luminance(block_3), 0.));
		// 	float weight_4 = 1. / (1. + max(Luminance(block_4), 0.));
		// 	float weight_5 = 1. / (1. + max(Luminance(block_5), 0.));
			
		// 	// vec3 sum = block_1*weight_1 + block_2*weight_2 + block_3*weight_3 + block_4*weight_4 + block_5*weight_5;
		// 	// sum /= (weight_1 + weight_2 + weight_3 + weight_4 + weight_5);
		// 	// vec3 sum = aces_approx(block_1 + block_2 + block_3 + block_4 + block_5);
		// 	out_color = vec4(sum, 1);
		// }
		// else {
		// }
		
		vec3 sum = e*0.125;
		sum += (a+c+g+i)*0.03125;
		sum += (b+d+f+h)*0.0625;
		sum += (j+k+l+m)*0.125;
		if (PC.dst_mip_level == 1) {
			sum = min(sum, vec3(1)); // Reduce HDR range to get rid of fireflies
			// sum = aces_approx(sum); 
		}
		out_color = vec4(sum, 1);
	}
#endif
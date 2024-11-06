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

// https://github.com/h3r2tic/rtoy-samples/blob/master/assets/shaders/taa.glsl (2018 Tomasz Stachowiak, MIT-license)
float mitchell_netravali(float x) {
	float B = 1.0 / 3.0;
	float C = 1.0 / 3.0;

	float ax = abs(x);
	if (ax < 1) {
		return ((12 - 9 * B - 6 * C) * ax * ax * ax + (-18 + 12 * B + 6 * C) * ax * ax + (6 - 2 * B)) / 6;
	} else if ((ax >= 1) && (ax < 2)) {
		return ((-B - 6 * C) * ax * ax * ax + (6 * B + 30 * C) * ax * ax + (-12 * B - 48 * C) * ax + (8 * B + 24 * C)) / 6;
	} else {
		return 0;
	}
}

vec3 clip_aabb(vec3 aabb_min, vec3 aabb_max, vec3 p, vec3 q) {
// #if USE_OPTIMIZATIONS
// 	// note: only clips towards aabb center (but fast!)
// 	vec3 p_clip = 0.5 * (aabb_max + aabb_min);
// 	vec3 e_clip = 0.5 * (aabb_max - aabb_min) + FLT_EPS;

// 	vec3 v_clip = q - p_clip;
// 	vec3 v_unit = v_clip.xyz / e_clip;
// 	vec3 a_unit = abs(v_unit);
// 	float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

// 	if (ma_unit > 1.0)
// 		return p_clip + v_clip / ma_unit;
// 	else
// 		return q;// point inside aabb
// #else
	vec3 r = q - p;
	vec3 rmax = aabb_max - p.xyz;
	vec3 rmin = aabb_min - p.xyz;

	const float eps = 0.00000001;

	if (r.x > rmax.x + eps)
		r *= (rmax.x / r.x);
	if (r.y > rmax.y + eps)
		r *= (rmax.y / r.y);
	if (r.z > rmax.z + eps)
		r *= (rmax.z / r.z);

	if (r.x < rmin.x - eps)
		r *= (rmin.x / r.x);
	if (r.y < rmin.y - eps)
		r *= (rmin.y / r.y);
	if (r.z < rmin.z - eps)
		r *= (rmin.z / r.z);

	return p + r;
// #endif
}

// https://github.com/playdeadgames/temporal (2015 Playdead, MIT-license)
vec3 ClipAABB(vec3 aabbMin, vec3 aabbMax, vec3 prevSample, vec3 avg) {
	// note: only clips towards aabb center (but fast!)
	vec3 p_clip = 0.5 * (aabbMax + aabbMin);
	vec3 e_clip = 0.5 * (aabbMax - aabbMin);

	vec3 v_clip = prevSample - p_clip;
	vec3 v_unit = v_clip.xyz / e_clip;
	vec3 a_unit = abs(v_unit);
	float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

	if (ma_unit > 1.0)
		return p_clip + v_clip / ma_unit;
	else
		return prevSample;// point inside aabb
	// vec3 r = prevSample - avg;
	// vec3 rmax = aabbMax - avg.xyz;
	// vec3 rmin = aabbMin - avg.xyz;

	// const float eps = 0.000001;

	// if (r.x > rmax.x + eps)
	// 	r *= (rmax.x / r.x);
	// if (r.y > rmax.y + eps)
	// 	r *= (rmax.y / r.y);
	// if (r.z > rmax.z + eps)
	// 	r *= (rmax.z / r.z);

	// if (r.x < rmin.x - eps)
	// 	r *= (rmin.x / r.x);
	// if (r.y < rmin.y - eps)
	// 	r *= (rmin.y / r.y);
	// if (r.z < rmin.z - eps)
	// 	r *= (rmin.z / r.z);

	// return avg + r;
}

#ifdef GPU_STAGE_VERTEX
	// Generate fullscreen triangle
	void main() {
		vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2. - vec2(1.);
		gl_Position = vec4(uv.x, uv.y, 0, 1);
	}

#else
	GPU_BINDING(LIGHTING_RESULT) texture2D LIGHTING_RESULT;
	GPU_BINDING(GBUFFER_DEPTH) texture2D GBUFFER_DEPTH;
	GPU_BINDING(GBUFFER_VELOCITY) texture2D GBUFFER_VELOCITY;
	GPU_BINDING(GBUFFER_VELOCITY_PREV) texture2D GBUFFER_VELOCITY_PREV;
	GPU_BINDING(PREV_FRAME_RESULT) texture2D PREV_FRAME_RESULT;
	GPU_BINDING(SAMPLER_LINEAR_CLAMP) sampler SAMPLER_LINEAR_CLAMP;

	layout(location = 0) out vec4 out_color;
	// layout(location = 1) out vec4 out_color_2;

	// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
	// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ for more details
	vec4 SampleHistoryTextureCatmullRom(vec2 uv, vec2 texSize) {
		// source: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1 (2019 MJP, MIT-license)
		
		// We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
		// down the sample location to get the exact center of our "starting" texel. The starting texel will be at
		// location [1, 1] in the grid, where [0, 0] is the top left corner.
		vec2 samplePos = uv * texSize;
		vec2 texPos1 = floor(samplePos - 0.5) + 0.5;

		// Compute the fractional offset from our starting texel to our original sample location, which we'll
		// feed into the Catmull-Rom spline function to get our filter weights.
		vec2 f = samplePos - texPos1;

		// Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
		// These equations are pre-expanded based on our knowledge of where the texels will be located,
		// which lets us avoid having to evaluate a piece-wise function.
		vec2 w0 = f * (vec2(-0.5) + f * (vec2(1.0) - 0.5 * f));
		vec2 w1 = vec2(1.0) + f * f * (vec2(-2.5) + 1.5 * f);
		vec2 w2 = f * (vec2(0.5) + f * (vec2(2.0) - 1.5 * f));
		vec2 w3 = f * f * (vec2(-0.5) + 0.5 * f);

		// Work out weighting factors and sampling offsets that will let us use bilinear filtering to
		// simultaneously evaluate the middle 2 samples from the 4x4 grid.
		vec2 w12 = w1 + w2;
		vec2 offset12 = w2 / (w1 + w2);

		// Compute the final UV coordinates we'll use for sampling the texture
		vec2 texPos0 = texPos1 - vec2(1);
		vec2 texPos3 = texPos1 + vec2(2);
		vec2 texPos12 = texPos1 + offset12;

		texPos0 /= texSize;
		texPos3 /= texSize;
		texPos12 /= texSize;

		vec4 result = vec4(0.0);
		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos0.x, texPos0.y), 0.0) * w0.x * w0.y;
		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos12.x, texPos0.y), 0.0) * w12.x * w0.y;
		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos3.x, texPos0.y), 0.0) * w3.x * w0.y;

		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos0.x, texPos12.y), 0.0) * w0.x * w12.y;
		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos12.x, texPos12.y), 0.0) * w12.x * w12.y;
		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos3.x, texPos12.y), 0.0) * w3.x * w12.y;

		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos0.x, texPos3.y), 0.0) * w0.x * w3.y;
		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos12.x, texPos3.y), 0.0) * w12.x * w3.y;
		result += textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), vec2(texPos3.x, texPos3.y), 0.0) * w3.x * w3.y;

		return result;
	}
	
	float Luminance(vec3 v) {
		return dot(v, vec3(0.2127, 0.7152, 0.0722));
	}

	void main() {
		
		vec2 texture_size = vec2(textureSize(sampler2D(LIGHTING_RESULT, SAMPLER_LINEAR_CLAMP), 0));
		vec2 pixel_size = vec2(1.) / texture_size;
		
		vec2 uv = gl_FragCoord.xy * pixel_size;
		
		// https://alextardif.com/TAA.html
		
		vec3 source_sample_total = vec3(0);
		float source_sample_weight = 0.;
		vec3 neighborhood_min = vec3(10000);
		vec3 neighborhood_max = vec3(-10000);
		vec3 m1 = vec3(0);
		vec3 m2 = vec3(0);
		float closest_depth = 10000.;
		vec2 closest_depth_uv = vec2(0);
		
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				vec2 sample_offset = vec2(x, y);
				vec2 sample_uv = uv + sample_offset*pixel_size;
				
				vec3 neighbor = texture(sampler2D(LIGHTING_RESULT, SAMPLER_LINEAR_CLAMP), sample_uv).xyz;
				float sub_sample_weight = mitchell_netravali(length(sample_offset));
				
				source_sample_total += neighbor * sub_sample_weight;
				source_sample_weight += sub_sample_weight;
				
				neighborhood_min = min(neighborhood_min, neighbor);
				neighborhood_max = max(neighborhood_max, neighbor);
				
				m1 += neighbor;
				m2 += neighbor * neighbor;
				
				float current_depth = texture(sampler2D(GBUFFER_DEPTH, SAMPLER_LINEAR_CLAMP), uv).r;
				if (current_depth < closest_depth) { // I'm not using reversed Z buffers right now
					closest_depth = current_depth;
					closest_depth_uv = sample_uv;
				}
			}
		}
		vec3 source_sample = source_sample_total / source_sample_weight;
		
		vec2 velocity_ndc = texture(sampler2D(GBUFFER_VELOCITY, SAMPLER_LINEAR_CLAMP), closest_depth_uv).xy;
		vec2 reprojected_uv = uv - velocity_ndc*0.5;
		vec2 prev_velocity_ndc = texture(sampler2D(GBUFFER_VELOCITY_PREV, SAMPLER_LINEAR_CLAMP), reprojected_uv).xy;
		
		vec3 prevColor = SampleHistoryTextureCatmullRom(reprojected_uv, texture_size).xyz;
		// vec3 prevColor = textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), reprojected_uv, 0.0).xyz;
		
		float oneDividedBySampleCount = 1.0 / 9.0;
		float gamma = 1.0;
		vec3 neighborhood_avg = m1 * oneDividedBySampleCount;
		vec3 sigma = sqrt(abs((m2 * oneDividedBySampleCount) - (neighborhood_avg * neighborhood_avg)));
		vec3 minc = neighborhood_avg - gamma * sigma;
		vec3 maxc = neighborhood_avg + gamma * sigma;
		// prevColor = clamp(prevColor, neighborhood_min, neighborhood_max);
		prevColor = clamp(prevColor, minc, maxc);
		
		// prevColor = clamp(prevColor, neighborhood_min, neighborhood_max);
		// prevColor = ClipAABB(minc, maxc, prevColor, neighborhood_avg).xyz;
		// prevColor = clip_aabb(minc, maxc, neighborhood_avg, prevColor).xyz;
		// prevColor = ClipAABB(minc, maxc, clamp(prevColor, neighborhood_min, neighborhood_max), neighborhood_avg);
		// prevColor = ClipAABB(neighborhood_min, neighborhood_max, prevColor, neighborhood_avg);
		
		vec3 weightB = vec3(0.05);
		vec3 weightA = vec3(1.0 - weightB);
		// float weightB = 1.;
		
		// ok... Maybe the problem is that we're dealing with non-linear color.
		
		// {
		// 	vec3 compressedSource = source_sample / (max(max(source_sample.r, source_sample.g), source_sample.b) + vec3(1.0));
		// 	vec3 compressedHistory = prevColor / (max(max(prevColor.r, prevColor.g), prevColor.b) + vec3(1.0));
		// 	float luminanceSource = Luminance(compressedSource);
		// 	float luminanceHistory = Luminance(compressedHistory); 
		// 	weightB *= 1.0 / (1.0 + luminanceSource);
		// 	weightA *= 1.0 / (1.0 + luminanceHistory);
		// }
		
		// Velocity based rejection works better if there's no sudden jumps in the velocities, i.e. there's camera smoothing.
		// I have a feeling that depth-based rejection might work better...
		float velocity_diff = 1000.*length(prev_velocity_ndc - velocity_ndc);
		weightB += velocity_diff;
		
		if (any(notEqual(reprojected_uv, clamp(reprojected_uv, vec2(0), vec2(1))))) {
			weightA = vec3(0.);
			weightB = vec3(1.);
		}
		
		vec3 result = (source_sample * weightB + prevColor * weightA) / max(weightB + weightA, 0.00001);
		// result = source_sample;
		
		// if (length(prev_velocity_ndc - velocity_ndc) > 0.01) {
		// 	result = vec3(1, 0, 0);
		// }

		// vec3 prevColor = textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), reprojected_uv, 0.).xyz;
		
		// vec3 prev_frame_result = texture(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), reprojected_uv).xyz;
		// vec3 blended = mix(prevColor, source_sample_total, 0.05);
		
		out_color = vec4(result, 1); // this is our HDR result rendertarget
		
		// Apply tone-mapping and gamma correction
		// out_color = vec4(pow(aces_approx(result), vec3(1./2.2)), 1); // swapchain rendertarget
	}
#endif
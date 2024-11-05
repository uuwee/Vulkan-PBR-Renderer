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

//layout(push_constant) uniform Constants {
//	//vec2 sun_jitter;
//} PC;

#define PI 3.14159265358979323846
#define GOLDEN_RATIO 1.61803398875

// Approximate the relative surface area of microfacets exactly aligned to the halfway vector H
float DistributionGGX(float NdotH_max0, float roughness) {
	float a = roughness * roughness;
	float a2     = a*a;
	float NdotH2 = NdotH_max0*NdotH_max0;

	float nom    = a2;
	float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
	denom        = PI * denom * denom;

	return nom / denom;
}

float DTermGGXTR(float NdotH, float alpha) {
	alpha = NdotH * alpha;
	float kappa = alpha / (NdotH * NdotH * (alpha * alpha - 1.0) + 1.0);
	return kappa * kappa / PI;
}

// Approximate the relative surface area where the light is unoccluded by micro-surface details.
// k is a remapping of the roughness based on if we're using this function for direct lighting or image-based lighting:
//   k_direct = (roughness + 1)^2 / 8
//   k_ibl = roughness^2 / 2
float GeometrySchlickGGX(float NdotV, float k) {
	float nom   = NdotV;
	float denom = NdotV * (1.0 - k) + k;
	return nom / denom;
}

float GeometrySmith(float NdotV_max0, float NdotL_max0, float k) {
	// from https://learnopengl.com/PBR/IBL/Specular-IBL
	float ggx1 = GeometrySchlickGGX(NdotV_max0, k);
	float ggx2 = GeometrySchlickGGX(NdotL_max0, k);
	return ggx1 * ggx2;
}

float GeometrySmithIBL(float NdotV_max0, float NdotL_max0, float roughness) {
	float k = (roughness * roughness) / 2.0;
	float ggx1 = GeometrySchlickGGX(NdotV_max0, k);
	float ggx2 = GeometrySchlickGGX(NdotL_max0, k);
	return ggx1 * ggx2;
}

// Combine the micro-surface occlusions for the path from the light to the surface and the surface to the eye
float GeometrySmithDirect(float VdotN, float LdotN_max0, float roughness) {
	float r = roughness + 1.;
	float k = r*r / 8.;
	float ggx1 = GeometrySchlickGGX(VdotN, k);
	float ggx2 = GeometrySchlickGGX(LdotN_max0, k);
	return ggx1 * ggx2;
}

float GeometryMikkelsen(float NdotH, float VdotN, float LdotN, float VdotH) {
	return min(1., min(2.*NdotH*VdotN / VdotH, 2.*NdotH*LdotN / VdotH));
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
  // TODO: expand the pow into a series of multiplies, rather than have it as pow()
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// More accurate fresnel
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
	// TODO: expand the pow into a series of multiplies, rather than have it as pow()
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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

vec3 Rotate(vec3 v, vec3 n, float theta) {
	// For derivation, see "3D Rotation about an Arbitrary Axis" from the book "3D Math Primer for Graphics and Game Development" (F. Dunn).
	return cos(theta)*(v - dot(v, n)*n) + sin(theta)*cross(n, v) + dot(v, n)*n;
}

float D_GTR(float roughness, float NoH, float k) {
	float a2 = pow(roughness, 2.);
	return a2 / (PI * pow((NoH*NoH)*(a2*a2-1.)+1., k));
}

float DistributionBeckmann(float NdotH, float m) {
	float m2 = m*m;
	float a = tan(acos(NdotH));
	float NdotH2 = NdotH*NdotH;
	return exp(-(a*a) / m2) / (PI*m2*NdotH2*NdotH2);
}

float DistributionPhong(float NdotH, float n) {
	return ((n + 2) / (2*PI)) * pow(NdotH, n);
}

float InterleavedGradientNoise(vec2 p) { // https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
	return fract(52.9829189 * fract(0.06711056*p.x + 0.00583715*p.y));
}
uvec3 MurmurHash31(uint src) { // https://www.shadertoy.com/view/ttc3zr
	const uint M = 0x5bd1e995u;
	uvec3 h = uvec3(1190494759u, 2147483647u, 3559788179u);
	src *= M; src ^= src>>24u; src *= M;
	h *= M; h ^= src;
	h ^= h>>13u; h *= M; h ^= h>>15u;
	return h;
}
vec3 Hash31(float src) { // https://www.shadertoy.com/view/ttc3zr
	uvec3 h = MurmurHash31(floatBitsToUint(src));
	return uintBitsToFloat(h & 0x007fffffu | 0x3f800000u) - 1.0;
}
#define FK(k) floatBitsToInt(cos(k)) ^ floatBitsToInt(k)
float Hash12(vec2 p) {
	uvec2 fk = FK(p);
	return float((fk.x + fk.y*fk.y)*(fk.y + fk.x*fk.x)) / 4.28e9;
}
float Hash13(vec3 p) {
	uvec3 fk = FK(p);
	return float((fk.x + fk.y*fk.y)*(fk.y + fk.z*fk.z)*(fk.z + fk.x*fk.x)) / 4.28e9;
}

// http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
vec2 r2_sequence(float n) {
	return fract(n * vec2(0.7548776662466927, 0.5698402909980532));
}

// TODO: do the same thing in fire_ui_shader!
#ifdef GPU_STAGE_VERTEX
	layout(location = 0) out vec2 fs_uv;
	
	// Generate fullscreen triangle
	void main() {
		vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2. - vec2(1.);
		fs_uv = uv*0.5 + 0.5;
		gl_Position = vec4(uv.x, uv.y, 0, 1);
	}

#else
	GPU_BINDING(GLOBALS) { Globals data; } GLOBALS;
	
	GPU_BINDING(GBUFFER_BASE_COLOR)   texture2D GBUFFER_BASE_COLOR;
	GPU_BINDING(GBUFFER_NORMAL)       texture2D GBUFFER_NORMAL;
	GPU_BINDING(GBUFFER_ORM)          texture2D GBUFFER_ORM;
	GPU_BINDING(GBUFFER_EMISSIVE)     texture2D GBUFFER_EMISSIVE;
	GPU_BINDING(GBUFFER_DEPTH)        texture2D GBUFFER_DEPTH;
	GPU_BINDING(PREV_FRAME_RESULT)    texture2D PREV_FRAME_RESULT;
	
	GPU_BINDING(SAMPLER_LINEAR_WRAP)   sampler SAMPLER_LINEAR_WRAP;
	GPU_BINDING(SAMPLER_LINEAR_CLAMP)  sampler SAMPLER_LINEAR_CLAMP;
	GPU_BINDING(SAMPLER_NEAREST_CLAMP) sampler SAMPLER_NEAREST_CLAMP;
	GPU_BINDING(SAMPLER_PERCENTAGE_CLOSER) samplerShadow SAMPLER_PERCENTAGE_CLOSER;
	GPU_BINDING(TEX_IRRADIANCE_MAP) textureCube TEX_IRRADIANCE_MAP;
	GPU_BINDING(PREFILTERED_ENV_MAP) textureCube PREFILTERED_ENV_MAP;
	GPU_BINDING(BRDF_INTEGRATION_MAP) texture2D BRDF_INTEGRATION_MAP;
	GPU_BINDING(SUN_DEPTH_MAP) texture2D SUN_DEPTH_MAP;
	GPU_BINDING(LIGHTGRID) texture3D LIGHTGRID;
	
	layout(location = 0) in vec2 fs_uv;
	
	layout(location = 0) out vec4 out_color;

	const vec3 lights[] = {
		vec3(1., -3.5, 5.),
		vec3(-2, 5, -5),
		vec3(4, 2, 15),
	};
	
	#define HORIZON_LOOP_NUM_STEPS 16

	uint HorizonLoop(vec2 ray_pos_ndc, vec3 p0, vec3 slice_bisector, vec3 slice_tangent, float dir, vec2 rd, uint occluded_bits) {
		float thickness = 100.01;
		vec3 V = -normalize(p0);
		
		// vec4 gi_accumulation = vec4(0);
		
		for (int i = 1; i <= HORIZON_LOOP_NUM_STEPS; i++) {
			ray_pos_ndc += rd;
			rd *= 1.2;
			thickness *= 1.2;
			if (ray_pos_ndc.x > 1 || ray_pos_ndc.x < -1 || ray_pos_ndc.y > 1 || ray_pos_ndc.y < -1) break;
			float d_front_ndc = texture(sampler2D(GBUFFER_DEPTH, SAMPLER_NEAREST_CLAMP), ray_pos_ndc*0.5 + 0.5).r;
			
			vec4 p_front_ = GLOBALS.data.view_space_from_clip * vec4(ray_pos_ndc, d_front_ndc, 1);
			vec3 p_front = p_front_.xyz / p_front_.w;
			
			// then we just need to find the angles
			vec3 p_front_delta = p_front - p0;
			vec3 p_back_delta = p_front_delta - V*thickness;
			
			vec2 front_tb = vec2(dot(p_front_delta, slice_tangent), dot(p_front_delta, slice_bisector));
			vec2 back_tb = vec2(dot(p_back_delta, slice_tangent), dot(p_back_delta, slice_bisector));
			
			// when dir = 1 (ray dir is towards slice tangent), a horizon on the hemisphere will be between 0.5 and 1
			float front_horizon = clamp((acos(front_tb.y / length(front_tb)) * dir) / PI + 0.5, 0., 1.);
			float back_horizon  = clamp((acos(back_tb.y / length(back_tb))   * dir) / PI + 0.5, 0., 1.);
			
			float min_horizon = min(front_horizon, back_horizon);
			float max_horizon = max(front_horizon, back_horizon);
			
			// We want to get the bits between min_horizon and max_horizon
			// Let's go with 31 sectors
			uint min_bit_idx = uint(min_horizon*31.);
			uint max_bit_idx = uint(ceil(max_horizon*31.));
			
			// TODO: try screenspace GI. For this, I need to move the SSGI & SSAO code into a post process pass, rather than doing it here in this lighting pass. But that also means that we won't have bent normals to sample indirect illumination in the lighting pass. So maybe we should render direct light in its own pass, then have a pass after that for applying indirect light.
			
			// if ((occluded_bits & (1 << uint(front_horizon*31.))) == 0) {
			// 	// front horizon is visible so add GI contribution
			// 	gi_accumulation.xyz += texture(sampler2D(GBUFFER_));
			// 	gi_accumulation.w += 1.;
			// }
			
			
			// make a mask with bits (min_bit_idx <= x < max_bit_idx) set to 1
			uint in_between_bits = ((1 << max_bit_idx) - 1) & ~((1 << min_bit_idx) - 1);
			occluded_bits |= in_between_bits;
		}
		return occluded_bits;
	}
	
	vec3 SampleRadiance(vec3 ray_origin, vec3 ray_direction, int num_steps, float step_scale) {
		float voxel_scale = 2./128.;
		vec3 rd = ray_direction*voxel_scale;
		vec3 ro = ray_origin * GLOBALS.data.lightgrid_scale;
		
		vec4 sum = vec4(0, 0, 0, 0.0001);
		
		// skip the initial blockage
		for (int i = 0; i < 12; i++) {
			ro += rd;
			vec4 radiance = texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), ro*0.5 + 0.5);
			if (radiance.a < 0.1) { // expand the voxel a lot so that we can do large steps
				sum += vec4(radiance.xyz, 1);
				break;
			}
		}
		
		if (sum.a < 0.5) {
			return vec3(0, 0, 0);
		}
		
		rd *= step_scale;
		for (int i = 0; i < num_steps; i++) {
			ro += rd;
			
			vec4 radiance = texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), ro*0.5 + 0.5);
			if (radiance.a > 0.3) break;
			
			sum += vec4(radiance.xyz, 1.);
		}
		
		sum /= sum.w;
		float luminance = 0.299*sum.x + 0.587*sum.y + 0.114*sum.z;
		sum *= sqrt(luminance) / max(luminance, 0.0001);
		return sum.xyz;
	}
	
	vec3 SampleRadianceWithScreenSpaceTrace(vec3 V, vec4 p0_vs, vec3 ray_origin, vec3 ray_direction, int num_steps, float step_scale, float noise_01, float foggyness, float ss_intensity) {
		float voxel_scale = 2./128.;
		vec3 rd = ray_direction*voxel_scale;
		vec3 ro = ray_origin * GLOBALS.data.lightgrid_scale;
		
		vec4 sum = vec4(0, 0, 0, 0.0001);
		
		// skip the initial blockage
		for (int i = 0; i < 4; i++) {
			ro += rd;
			vec4 radiance = texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), ro*0.5 + 0.5);
			if (radiance.a < 0.3) { // expand the voxel a lot so that we can do large steps
				sum += vec4(radiance.xyz, 1);
				break;
			}
		}
		
		vec4 open_point_vs = GLOBALS.data.view_space_from_world * vec4(ro / GLOBALS.data.lightgrid_scale, 1.);
		
		// vec3 p0_to_open_point_ndc = open_point_ndc.xyz - p0_ndc.xyz;
		
		// float ssray_length_ndc = length(ssray_dir);
		// ssray_dir /= ssray_length_ndc;
		// float p0_to_open_point_slope_ndc = p0_to_open_point_ndc.z / length(p0_to_open_point_ndc.xy);
		
		vec4 p0_to_open_point_vs = open_point_vs - p0_vs;
		
		float step_length = max(p0_vs.z, 1.) * (1. + noise_01)/100.;
		vec3 ssray_dir = p0_to_open_point_vs.xyz / length(p0_to_open_point_vs.xy);
		vec3 ssray_step = ssray_dir * step_length;
		
		vec3 ssray_pos_vs = p0_vs.xyz;
		
		// vec4 ssray_step = p0_to_open_point_vs * (step_length / length(p0_to_open_point_vs.xy));
		// float ssray_dist_to_travel = length(p0_to_open_point_vs.xy);
		float ssray_dist_to_travel = length(p0_to_open_point_vs.xyz);
		
		float ssray_dist_travelled = 0.;
		
		// ssray_pos += ssray_step*noise_01;
		// ssray_dist_travelled += step_length*noise_01;
		
		for (;;) {
			ssray_pos_vs += ssray_step;
			ssray_dist_travelled += step_length;
			
			vec4 ssray_pos_ndc = GLOBALS.data.clip_space_from_view * vec4(ssray_pos_vs, 1);
			ssray_pos_ndc /= ssray_pos_ndc.w;
			
			vec2 clamped = clamp(ssray_pos_ndc.xy, vec2(-1, -1), vec2(1, 1));
			if (clamped != ssray_pos_ndc.xy) { // fallback
				vec3 fallback_pos = ray_origin * GLOBALS.data.lightgrid_scale + 2.5*V*voxel_scale;
				sum = texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), fallback_pos*0.5 + 0.5);
				
				float luminance = 0.299*sum.x + 0.587*sum.y + 0.114*sum.z;
				sum *= sqrt(luminance) / max(luminance, 0.0001);
				return sum.xyz;
			}
			
			ssray_step  *= 1.2;
			step_length *= 1.2;
			
			float depth_ndc = texture(sampler2D(GBUFFER_DEPTH, SAMPLER_NEAREST_CLAMP), ssray_pos_ndc.xy*0.5 + 0.5).r;
			
			// it's a bit dumb that we need two matrix multiplies per step now...
			vec4 surface_p_vs = GLOBALS.data.view_space_from_clip * vec4(ssray_pos_ndc.xy, depth_ndc, 1);
			surface_p_vs /= surface_p_vs.w;
			
			// we want to know if the ray position is occluded or not due to the surface.
			
			if (length(surface_p_vs.xyz) < length(ssray_pos_vs.xyz)) {
				// we should do a solidness check - look at a few points in the voxel map to check for solidness along the depth of this pixel.
				// If this surface is solid all the way through, then stop here and return the screen-space radiance. Otherwise,
				// break out of this screen space trace and continue stepping in voxel space.
				
				vec4 thickness_start = (GLOBALS.data.world_space_from_view * surface_p_vs) * GLOBALS.data.lightgrid_scale * 0.5 + 0.5;
				vec4 thickness_end = (GLOBALS.data.world_space_from_view * vec4(ssray_pos_vs, 1))   * GLOBALS.data.lightgrid_scale * 0.5 + 0.5;
				
				float noise_offset = noise_01*0.2;
				float alpha =
					(texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), mix(thickness_start.xyz, thickness_end.xyz, noise_offset+0.2)).a +
					texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), mix(thickness_start.xyz, thickness_end.xyz, noise_offset+0.4)).a +
					texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), mix(thickness_start.xyz, thickness_end.xyz, noise_offset+0.6)).a);
					
				if (alpha < 1.5) {
					ssray_step  *= 2. + noise_01;
					step_length *= 2. + noise_01;
					continue;
				}
				
				// hmm... so here we would like to sample the light at this pixel value.
				// let's just do a dumb lighting calculation inline for now
				
				// we have the approximate ambient in `sum` ... except that it's incorrect, since the ray hasn't reached that point yet.
				// How can we get an approximated radiance value here? I guess we should look up the previous frame's screen-space radiance texture.
				
				// We could look at the TAA output of previous frame, as that's required to be alive during this time, and it contains the radiance of the previous frame
				
				vec2 uv = ssray_pos_ndc.xy*0.5 + 0.5;
				// we want to figure out the reprojected UV.
				
				// I think right now I should just make it possible to sample at different mip levels. That would eliminate the flickering.
				// The further the ray, the higher the mip level should be.
				
				vec3 sampled_radiance = textureLod(sampler2D(PREV_FRAME_RESULT, SAMPLER_LINEAR_CLAMP), uv, min(step_length*5., 5.)).xyz;
				// sampled_radiance = min(sampled_radiance, vec3(1.));
				// sampled_radiance = aces_approx(sampled_radiance);
				
				if (GLOBALS.data.alt_is_held_down > 0.5) sampled_radiance *= 0.;
				
				// vec4 base_color = texture(sampler2D(GBUFFER_BASE_COLOR, SAMPLER_LINEAR_CLAMP), uv);
				return sampled_radiance*ss_intensity;
				// return sampled_radiance * 0.9;
				
				// float luminance = 0.299*sum.x + 0.587*sum.y + 0.114*sum.z;
				// sum *= sqrt(luminance) / max(luminance, 0.0001);
				// return sum.xyz * base_color.xyz;
			}
			
			if (ssray_dist_travelled > ssray_dist_to_travel) break;
			
			// if (slope - 0.01 > p0_to_open_point_slope_ndc) { // hit!
			// 	return vec3(0, 0, 1);
			// }
		}
		
		if (sum.a < 0.5) {
			// We couldn't find an open point AND we didn't hit anything in screen space. Just ignore this sample.
			return vec3(0, 0, 0);
		}
		// return vec3(0, 0, 0);
		// return vec3(0, 0, 0);
		
		rd *= step_scale;
		ro += rd*noise_01;
		
		// continue until hitting a voxel
		for (int i = 0; i < num_steps; i++) {
			ro += 0.5*rd;
			
			vec4 radiance = texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), ro*0.5 + 0.5);
			if (radiance.a > 0.3) break;
			
			// sum += vec4(radiance.xyz, 1.);
			sum = sum*foggyness + vec4(radiance.xyz, 1.);
		}
		
		sum /= sum.w;
		float luminance = 0.299*sum.x + 0.587*sum.y + 0.114*sum.z;
		sum *= sqrt(luminance) / max(luminance, 0.0001);
		return sum.xyz;
	}
	
	const mat4 bayerIndex = mat4(
		vec4(00.0/16.0, 12.0/16.0, 03.0/16.0, 15.0/16.0),
		vec4(08.0/16.0, 04.0/16.0, 11.0/16.0, 07.0/16.0),
		vec4(02.0/16.0, 14.0/16.0, 01.0/16.0, 13.0/16.0),
		vec4(10.0/16.0, 06.0/16.0, 09.0/16.0, 05.0/16.0));
	
	void main() {
		vec3 base_color = texture(sampler2D(GBUFFER_BASE_COLOR, SAMPLER_LINEAR_WRAP), fs_uv).rgb;
		vec3 N = texture(sampler2D(GBUFFER_NORMAL, SAMPLER_LINEAR_WRAP), fs_uv).rgb;
		N = N*2. - 1.;
		
		vec3 normal_vs = (GLOBALS.data.view_space_from_world * vec4(N, 0)).xyz;
		
		vec3 orm = texture(sampler2D(GBUFFER_ORM, SAMPLER_LINEAR_WRAP), fs_uv).rgb;
		vec3 emissive = texture(sampler2D(GBUFFER_EMISSIVE, SAMPLER_LINEAR_WRAP), fs_uv).rgb * 10.;
		float roughness = orm.y;
		float metallic = orm.z;
		
		float origin_pixel_depth_ndc = texture(sampler2D(GBUFFER_DEPTH, SAMPLER_LINEAR_WRAP), fs_uv).r;
		vec4 p0_ndc = vec4(fs_uv*2.-1., origin_pixel_depth_ndc, 1.);
		
		vec4 p0_view = GLOBALS.data.view_space_from_clip * p0_ndc;
		p0_view /= p0_view.w;
		
		vec4 p0_world = GLOBALS.data.world_space_from_clip * p0_ndc;
		p0_world /= p0_world.w;
		
		// lightgrid size... we should pass that in globals.
		// vec3 lightgrid_sample_p = p0_lightgrid_ndc + N*2./128.;
		
		float noise_offset = 1000*1.61803398875*GLOBALS.data.frame_idx_mod_59;
		float noise_1 = fract(InterleavedGradientNoise(gl_FragCoord.xy) + noise_offset);
		float noise_2 = fract(InterleavedGradientNoise(gl_FragCoord.xy + vec2(90, 20)) + noise_offset);
		float noise_3 = fract(InterleavedGradientNoise(gl_FragCoord.xy + vec2(522, 55)) + noise_offset);
		// float hash_1 = fract(Hash12(gl_FragCoord.xy) + noise_offset);
		// vec3 hash_xyz = Hash31(hash_1);
		
		// ---------------- VOXEL DEBUG RAY TRACER ----------------
		if (false) {
			// vec4 near_p = GLOBALS.data.world_space_from_clip * vec4(fs_uv*2.-1., 0.997, 1);
			vec4 near_p = GLOBALS.data.world_space_from_clip * vec4(fs_uv*2.-1., 0., 1);
			near_p /= near_p.w;
			
			vec3 ro = near_p.xyz * GLOBALS.data.lightgrid_scale;
			vec3 rd = normalize(near_p.xyz - GLOBALS.data.camera_pos) * (1. / 128.);
			ro += noise_1*rd;
			// ro += rd*20.;
			
			// it'd be cool to also see how accumulated rays work
			vec4 sum = vec4(0, 0, 0, 0.00001);
			for (int i = 0; i < 512; i++) {
				ro += rd;
				vec4 radiance = texture(sampler3D(LIGHTGRID, SAMPLER_LINEAR_CLAMP), ro*0.5 + 0.5);
				
				sum += vec4(radiance.xyz, 1);
				if (radiance.a > 0.3) {
					// if (radiance == vec4(0, 0, 0, 1) && fract(gl_FragCoord.x*(1./50.) + 0.1) > 0.98) sum = vec4(1., 0., 1., 1);
					sum += 10.*vec4(radiance.xyz, 1);
					break;
				}
			}
			sum /= sum.w;
			
			float luminance = 0.299*sum.x + 0.587*sum.y + 0.114*sum.z;
			sum *= sqrt(luminance) / max(luminance, 0.0001);
			
			out_color = vec4(sum.xyz, 1);
			return;
		}
		
		// hmm... so we can't have back faces because the sunlight hitting it will bleed into the interior of the model...
		// {
		// 	vec3 p = p0_world.xyz * GLOBALS.data.lightgrid_scale;
		// 	vec3 radiance = texture(sampler3D(LIGHTGRID, SAMPLER_NEAREST_CLAMP), p*0.5 + 0.5).xyz;
			
		// 	float luminance = 0.299*radiance.x + 0.587*radiance.y + 0.114*radiance.z;
		// 	radiance *= sqrt(luminance) / max(luminance, 0.0001);
		// 	out_color = vec4(radiance, 1);
		// 	return;
		// }
		
		// -- SSAO ----------------------------------------------------
		
#if 0
		// float ray_angle_ndc = PI*0.5;
		float ray_angle_ndc = PI*2.*noise_1;
		vec2 ray_dir_ndc = vec2(cos(ray_angle_ndc), sin(ray_angle_ndc));
		
		vec3 slice_normal = normalize(cross(vec3(ray_dir_ndc, 0), p0_view.xyz)); // rotated 90 degrees CCW from the ray dir
		vec3 slice_bisector = normalize(normal_vs - slice_normal*dot(normal_vs, slice_normal));
		vec3 slice_tangent = cross(slice_normal, slice_bisector); // points roughly towards the ray direction
		
		vec2 rd = ray_dir_ndc * ((1. + 1.*noise_2)/1000.);
		vec2 ro = p0_ndc.xy;// + rd*(hash1 - 0.5);
		
		// horizon of 1 points towards slice tangent
		// uint occluded_bits = 0 | (1 << 31);
		uint occluded_bits = 0;
		// occluded_bits = HorizonLoop(ro, p0_view.xyz, slice_bisector, slice_tangent, 1., rd, occluded_bits);
		// occluded_bits = HorizonLoop(ro, p0_view.xyz, slice_bisector, slice_tangent, -1., -rd, occluded_bits);
		
		float ao = (1.0 - float(bitCount(occluded_bits)) / 32.);
#endif
		
		// -- Bent normal ----------------------------------------------------
		
		// I'm doing something very wrong here. If I look at the bent_normal.x value on a flat ceiling while rotating the camera around, weird stuff happens.
		
		// vec3 bent_normal = N;
		
		// if (occluded_bits != 0xFFFFFFFF) {
		// 	// TODO: IMPROVE THIS! This is not very random... If half of the sectors are closed, then the next open slot right after that is likely picked.
			
		// 	// Fast random idea: if there are more than 10 zero bits, do stochastic random. Otherwise
		// 	// count the zero bits, get a random number between, then find it.
			
		// 	uint empty_sectors = ~occluded_bits;
			
		// 	float edge_limit = 5./31.; // hmm, we could randomize this to emulate cos(theta) weight
			
		// 	float angle = 0.5*PI; // go with half pi
		// 	float scale = 0.;
		// 	for (int i = 0; i < 16; i++) {
		// 		float x = mix(edge_limit, 1.-edge_limit, fract(noise_1 + i*1.6180339887498948482));
		// 		uint bit_idx = uint(x * 30.9999);
		// 		if (((1 << bit_idx) & empty_sectors) != 0) {
		// 			angle = PI - x*PI;
		// 			scale = 1.;
		// 			break;
		// 		}
		// 	}
			
		// 	vec3 slice_bisector_ws = (GLOBALS.data.world_space_from_view * vec4(slice_bisector, 0)).xyz;
		// 	vec3 slice_tangent_ws = (GLOBALS.data.world_space_from_view * vec4(slice_tangent, 0)).xyz;
		// 	bent_normal = cos(angle)*slice_tangent_ws + sin(angle)*slice_bisector_ws;
		// 	// bent_normal = slice_bisector_ws;
		// }
		// // With vis-bitmask, an angle of pi/2 usually doesn't match with the surface normal.
		
		// for now, let's instead come up with a random direction.
		vec3 some_vector = normalize(vec3(0.7128864983, 0.8217892113, 0.948912748));
		vec3 tangent = normalize(cross(some_vector, N));
		vec3 bitangent = cross(N, tangent);
		mat3 TBN = mat3(tangent, bitangent, N);
		// vec3 random_v_xy = tangent*cos(noise_1) + bitangent*sin(noise_1);
		
		// this is not good. I think we should use a more proper low-discrepency hemisphere sampling method.
		
		// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
		// ok, so we can first get uniform random positions in a 2D square using R2,
		
		// so we want an integer offset that varies per pixel, and we want these integer offsets to stay the same across frames.
		// float integer_offset = floor(noise);
		
		vec2 bayer_coord = gl_FragCoord.xy + noise_1*vec2(50502.0501253, 2052.213);//*GLOBALS.data.frame_idx_mod_59;
		float noise_constant = bayerIndex[int(bayer_coord.x) % 4][int(bayer_coord.y) % 4] * 16.;
		// out_color = vec4(vec3(noise_constant), 1);
		// return;
		// float noise_constant = InterleavedGradientNoise(gl_FragCoord.xy);
		
		// 1000*1.61803398875*GLOBALS.data.frame_idx_mod_59
		float rand_v_pitch  = acos(sqrt(1. - noise_1)); // cosine-distribution
		float rand_v_yaw    = 2.*PI*noise_3;
		
		// rand_v_yaw = 2.*PI*noise_1;
		// rand_v_pitch = 0.5*0.7*PI*noise_2;
		
		// rand_v_pitch *= 2.;
		// rand_v_pitch = 0.5*PI*(1. - cos(rand_v_pitch)); // weight the pitch closer towards zero
		
		// let's calculate a random point along local Z-up hemisphere
		vec3 rand_v_local = vec3(sin(rand_v_pitch) * vec2(cos(rand_v_yaw), sin(rand_v_yaw)), cos(rand_v_pitch));
		// vec3 rand_v_local = vec3(vec2(cos(rand_v_yaw), sin(rand_v_yaw)), cos(rand_v_pitch));
		// rand_v_local = vec3(0, 0, 1);
		
		vec3 bent_normal = TBN*rand_v_local;
		// bent_normal = N;
		
		// out_color = vec4(vec3(bent_normal), 1);
		// out_color = clamp(out_color, vec4(0), vec4(1));
		// return;
		
		// --- sample shadow map ---------------------------------------------------------
		
		const float sun_shadow_map_pixel_size = (1. / 2048.);
		
		vec3 sun_p_ws = p0_world.xyz + N*0.1;
		vec3 p0_sun_space = (GLOBALS.data.sun_space_from_world * vec4(sun_p_ws, 1)).xyz;
		vec3 sun_p = vec3(p0_sun_space.xy*0.5 + 0.5, p0_sun_space.z);
		
		sun_p.xy += 2.*vec2(noise_2 - 0.5, noise_1 - 0.5) * sun_shadow_map_pixel_size;
		// Shadow Normal Offset Bias
		
		// float shadow = texture(sampler2DShadow(SUN_DEPTH_MAP, SAMPLER_PERCENTAGE_CLOSER), sun_p);
		float shadow = 
			(texture(sampler2DShadow(SUN_DEPTH_MAP, SAMPLER_PERCENTAGE_CLOSER), sun_p + vec3(0.75, 0.25, 0.) * sun_shadow_map_pixel_size) +
			texture(sampler2DShadow(SUN_DEPTH_MAP, SAMPLER_PERCENTAGE_CLOSER),  sun_p + vec3(-0.25, 0.75, 0.) * sun_shadow_map_pixel_size) +
			texture(sampler2DShadow(SUN_DEPTH_MAP, SAMPLER_PERCENTAGE_CLOSER),  sun_p + vec3(0.25, -0.75, 0.) * sun_shadow_map_pixel_size) +
			texture(sampler2DShadow(SUN_DEPTH_MAP, SAMPLER_PERCENTAGE_CLOSER),  sun_p + vec3(-0.75, -0.25, 0.) * sun_shadow_map_pixel_size)) * 0.25;
		
		// -------------------------------------------------------------------------------
		
		vec3 V = normalize(GLOBALS.data.camera_pos.xyz - p0_world.xyz);
		float VdotN = max(dot(V, N), 0.);
		
		// so... PBR directional light
		vec3 sun_emission = 25.*vec3(1, 0.9, 0.7);
		
		vec3 outgoing_light = vec3(0);
		
		// ---- light shaft ray ----------------------------------------------------------
		
#if 1
		// do tracing in shadow map space.
		vec3 shaft_ray_pos = (GLOBALS.data.sun_space_from_world * vec4(GLOBALS.data.camera_pos.xyz, 1)).xyz;
		// vec3 shaft_ray_goal = p0_sun_space;
		
		vec3 shaft_delta = p0_sun_space - shaft_ray_pos;
		float distance_to_travel = length(shaft_delta);
		float distance_traveled = 0.;
		
		const float step_size = 1./16.;
		vec3 shaft_ray_step = step_size * (shaft_delta / distance_to_travel);
		
		shaft_ray_pos += shaft_ray_step * noise_1;
		distance_traveled += step_size * noise_1;
		
		while (true) {
			shaft_ray_pos += shaft_ray_step;
			distance_traveled += step_size;
			if (distance_traveled > distance_to_travel) break;
			
			// vec3 sample_pos = mix(shaft_ray_pos, shaft_ray_goal, t);
			vec3 sample_pos = vec3(shaft_ray_pos.xy*0.5 + 0.5, shaft_ray_pos.z);
			
			float visibility = texture(sampler2DShadow(SUN_DEPTH_MAP, SAMPLER_PERCENTAGE_CLOSER), sample_pos);
			outgoing_light += 0.001 * visibility * sun_emission;
			
		}
#endif

		// -------------------------------------------------------------------------------

		// F0 is the reflectivity of the surface when viewed directly from the front at a 0-degree angle.
		// We can use this reflectivity to determine the ratio of reflected light vs refracted light.
		vec3 F0 = vec3(0.04);
		F0 = mix(F0, base_color.xyz, metallic);
		
		vec3 kS = FresnelSchlick(max(dot(N, V), 0.0), F0);
		vec3 kD = (1.0 - kS) * (1. - metallic);
		
		// This is probably not physically correct right now, I haven't derived it myself but rather took it from LearnOpenGL
		{
			vec3 L = -GLOBALS.data.sun_direction.xyz;
			vec3 H = normalize(L + V);
			float NdotL = max(dot(N, L), 0.);
			if (NdotL > 0.) {
				float VdotH = max(dot(V, H), 0.);
				float NdotH = max(dot(N, H), 0.);
				
				float D = DistributionGGX(NdotH, roughness);
				float G = GeometryMikkelsen(NdotH, VdotN, NdotL, VdotH);
				vec3 F = FresnelSchlick(VdotH, F0);
				
				vec3 brdf = F * G * vec3(D) / max(4. * NdotL * VdotN, 0.0001);
				outgoing_light += shadow * (kD * base_color / PI + brdf) * sun_emission * NdotL;
			}
		}

		vec2 fresnel_scale_bias = textureLod(sampler2D(BRDF_INTEGRATION_MAP, SAMPLER_LINEAR_CLAMP), vec2(VdotN, max(roughness, 0.05)), 0.).xy;
		
		vec3 ambient = vec3(0);
		// ambient = SampleRadiance(p0_world.xyz, bent_normal, 12, 1.);
		ambient = SampleRadianceWithScreenSpaceTrace(V, p0_view, p0_world.xyz, bent_normal, 12, 1., noise_3, 0.5, 0.75);
		// vec3 ambient = SampleRadiance(p0_world.xyz, N, 16, 1.);
		outgoing_light += kD * ambient * base_color.xyz;
		
		// vec3 irradiance = textureLod(samplerCube(TEX_IRRADIANCE_MAP, SAMPLER_LINEAR_WRAP), bent_normal, 0.).rgb;
		// vec3 irradiance = textureLod(samplerCube(TEX_IRRADIANCE_MAP, SAMPLER_LINEAR_WRAP), N, 0.).rgb;
		// vec3 diffuse = irradiance * base_color.xyz;
		
		vec3 R = reflect(-V, N);
		R = normalize(R + 0.6*roughness*(vec3(noise_1, noise_2, noise_3) - vec3(0.5))); // add a random offset to R depending on the roughness.
		
		float roughness2 = roughness*roughness;
		R = mix(R, N, roughness2*roughness2); // bias the sample direction to avoid reflection leaking from negative angles.
		
		// vec3 prefilter_spec_color = textureLod(samplerCube(PREFILTERED_ENV_MAP, SAMPLER_LINEAR_WRAP), R, roughness*4.).rgb;
		vec3 prefilter_spec_color = vec3(0);
		prefilter_spec_color = SampleRadianceWithScreenSpaceTrace(V, p0_view, p0_world.xyz, R, 16, 2., noise_3, roughness, 0.9);
		outgoing_light += prefilter_spec_color*(F0*fresnel_scale_bias.x + fresnel_scale_bias.y);
		
		// currently, prefilter_spec_color has the biggest leaks...
		
		outgoing_light += emissive;// + 0.25*(kD*ao*diffuse + prefilter_spec_color*(F0*fresnel_scale_bias.x + fresnel_scale_bias.y));
		// outgoing_light += emissive + 0.5*ao*diffuse;
		// outgoing_light += emissive + 0.5*diffuse;
		
		if (clamp(p0_world.xyz, vec3(-99), vec3(99)) != p0_world.xyz) {
			outgoing_light = textureLod(samplerCube(PREFILTERED_ENV_MAP, SAMPLER_LINEAR_CLAMP), -V, 1.).rgb;
		}
		
		outgoing_light = max(outgoing_light, vec3(0));
		out_color = vec4(outgoing_light, 1);
		// out_color = vec4(prefilter_spec_color, 1);
		// out_color = vec4(ambient, 1);
	}
	

#endif
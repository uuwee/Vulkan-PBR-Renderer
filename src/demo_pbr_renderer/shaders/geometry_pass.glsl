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

layout(push_constant) uniform Constants {
	vec2 taa_jitter;
	vec2 taa_jitter_prev;
} PC;

// TODO: do the same thing in fire_ui_shader!
#ifdef GPU_STAGE_VERTEX
	GPU_BINDING(GLOBALS) { Globals data; } GLOBALS;
	
	layout(location = 0) in vec3 vs_position;
	layout(location = 1) in vec3 vs_normal;
	layout(location = 2) in vec3 vs_tangent;
	layout(location = 3) in vec2 vs_tex_coord;
	
	layout(location = 0) out vec3 fs_position;
	layout(location = 1) out vec3 fs_normal;
	layout(location = 2) out vec3 fs_tangent;
	layout(location = 3) out vec2 fs_tex_coord;
	layout(location = 4) out vec4 fs_position_cs; // TODO: couldn't we send just the vec2 velocity...?
	layout(location = 5) out vec4 fs_position_cs_old;
	
	void main() {
		vec4 position_clip = GLOBALS.data.clip_space_from_world * vec4(vs_position, 1.);
		position_clip.xy += PC.taa_jitter * position_clip.w;
		
		vec4 old_position_clip = GLOBALS.data.old_clip_space_from_world * vec4(vs_position, 1.);
		old_position_clip.xy += PC.taa_jitter_prev * old_position_clip.w;
		
		fs_position_cs = position_clip;
		fs_position_cs_old = old_position_clip;
		
		fs_position = vs_position;
		fs_normal = vs_normal;
		fs_tangent = vs_tangent;
		fs_tex_coord = vs_tex_coord;
		gl_Position = position_clip;
	}

#else
	GPU_BINDING(GLOBALS) { Globals data; } GLOBALS;
	
	GPU_BINDING(TEX0) texture2D TEX_BASE_COLOR;
	GPU_BINDING(TEX1) texture2D TEX_NORMAL;
	GPU_BINDING(TEX_ORM) texture2D TEX_ORM;
	GPU_BINDING(TEX_EMISSIVE) texture2D TEX_EMISSIVE;
	GPU_BINDING(SAMPLER_LINEAR_WRAP) sampler SAMPLER_LINEAR_WRAP;
	GPU_BINDING(SAMPLER_LINEAR_CLAMP) sampler SAMPLER_LINEAR_CLAMP;
	// GPU_BINDING(TEX_IRRADIANCE_MAP) textureCube TEX_IRRADIANCE_MAP;
	// GPU_BINDING(PREFILTERED_ENV_MAP) textureCube PREFILTERED_ENV_MAP;
	// GPU_BINDING(BRDF_INTEGRATION_MAP) texture2D BRDF_INTEGRATION_MAP;
	
	layout(location = 0) in vec3 fs_position;
	layout(location = 1) in vec3 fs_normal;
	layout(location = 2) in vec3 fs_tangent;
	layout(location = 3) in vec2 fs_tex_coord;
	layout(location = 4) in vec4 fs_position_cs;
	layout(location = 5) in vec4 fs_position_cs_old;
	
	layout(location = 0) out vec4 out_base_color;
	layout(location = 1) out vec4 out_normal;
	layout(location = 2) out vec4 out_orm;
	layout(location = 3) out vec4 out_emissive;
	layout(location = 4) out vec2 out_velocity;

	const vec3 lights[] = {
		vec3(1., -3.5, 5.),
		vec3(-2, 5, -5),
		vec3(4, 2, 15),
	};
	
	// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
	vec3 AcesFilmicTonemap(vec3 x) {
		float a = 2.51;
		float b = 0.03;
		float c = 2.43;
		float d = 0.59;
		float e = 0.14;
		return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0, 1);
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
	
	vec3 CalculateSpecular(vec3 F0, vec3 N, vec3 V, float roughness, float metallic) {
		float NdotV = dot(N, V);
		vec3 some_vector = vec3(12.123825810901, 6.11831989512, -5.12039214121);
		vec3 tangent = normalize(cross(N, some_vector));
	
		// Why is the top-left not completely white? It's a complete mirror, and thus should fully reflect everything, right??? So the reason for the darkening in the center is, then NdotV = 1, and so then the integral gets divided by a big number.
		
		const uint SAMPLE_COUNT = 512;
		
		vec3 sum = vec3(0);
		
		// I still don't get it... The only way for me to figure this crap out is to write a ray tracer / reference implementation... Why is there a white halo in the mirror metal sphere? That shouldn't be there! The halo does not exist, because Karis doesn't follow the math in the RBDF generator.
		//float D_total = 0.;
		float dw = 2.*PI / float(SAMPLE_COUNT);
		
		for (int i = 0; i < SAMPLE_COUNT; i++) {
			float x = float(i) / float(SAMPLE_COUNT); // https://extremelearning.com.au/evenly-distributing-points-on-a-sphere/
			float y = float(i) / GOLDEN_RATIO;
			float pitch = PI - acos(x - 1.); // Cylindrical equal area projection on the top half of the sphere
			float yaw = 2.*PI*y;
			
			vec3 L = Rotate(Rotate(N, tangent, pitch), N, yaw);
			vec3 H = normalize(L + V);
			float NdotL = max(dot(N, L), 0.);
			float VdotH = max(dot(V, H), 0.);
			float NdotH = max(dot(N, H), 0.);
			
			// let's just assume that all microfacets are perfectly oriented
			//float D = NdotH > 0.990 ? 1. : 0.;//50.*DistributionGGX(NdotH, roughness);
			//float D = DistributionGGX(NdotH, roughness);
			//float D = DTermGGXTR(NdotH, roughness);
			//float D = D_GTR(roughness, NdotH, 2.);
			
			vec3 radiance_in = vec3(1);//textureLod(samplerCube(PREFILTERED_ENV_MAP, SAMPLER_LINEAR_WRAP), L, roughness*3.).rgb;
			
			//float D = DistributionPhong(NdotH, 100.);
			float D = DistributionBeckmann(NdotH, sqrt(2)*roughness);
			vec3 F = FresnelSchlick(VdotH, F0);
			float G = GeometryMikkelsen(NdotH, NdotV, NdotL, VdotH);
			//float G = GeometrySmithDirect(NdotV, NdotL, roughness);
			//float G = GeometrySmithIBL(NdotV, NdotL, roughness);
			//float G = GeometrySmith(NdotV, NdotL, roughness);
			
			vec3 brdf = F * G * vec3(D) / (4. * NdotL * NdotV);
			sum += brdf * radiance_in * NdotL * dw; // equation (3) in Mikkelsen's paper
			
			//D_total += D * cos(pitch) * dw;
			
			// I think our problem is somewhere here.
			// If we include D, then the total energy goes down with our current math.
			//D_correction_weight += D * NdotH * dw; // This should end up summing up to 1
		}
		//return abs(vec3(D_total) - vec3(1));
		return sum;
	}

	void main() {
		// vec3 base_color = texture(sampler2D(TEX_BASE_COLOR, SAMPLER_LINEAR_WRAP), fs_tex_coord).rgb;
		
		vec4 base_color = texture(sampler2D(TEX_BASE_COLOR, SAMPLER_LINEAR_WRAP), fs_tex_coord);
		if (base_color.a < 0.3) discard;
		base_color = pow(base_color, vec4(2.2)); // sRGB space -> linear
		
		vec3 orm = texture(sampler2D(TEX_ORM, SAMPLER_LINEAR_WRAP), fs_tex_coord).rgb;
		vec3 emissive = texture(sampler2D(TEX_EMISSIVE, SAMPLER_LINEAR_WRAP), fs_tex_coord).rgb;
		// float roughness = orm.y;
		// float metallic = orm.z;
		
		vec3 V = normalize(GLOBALS.data.camera_pos.xyz - fs_position);
		
		vec3 N = normalize(fs_normal);
		vec3 T = normalize(fs_tangent);
		float VdotN = dot(V, N);
		
		// Technique for deriving tangent basis from:
		// https://irrlicht.sourceforge.io/forum/viewtopic.php?t=52284
		// I took the code and tweaked it until it worked. I don't have a better math-based explanation for it, sorry!
		
		vec3 tangent_space_normal = texture(sampler2D(TEX_NORMAL, SAMPLER_LINEAR_WRAP), fs_tex_coord).xyz;
		tangent_space_normal = tangent_space_normal*2. - 1.;
		tangent_space_normal.z = sqrt(1. - dot(tangent_space_normal.xy, tangent_space_normal.xy)); // Derive Z from XY such that the vector is unit length
		// tangent_space_normal.y *= -1.;
		
		// tangent_space_normal.xy = tangent_space_normal.xy*2. - 1.; // To tangent-space normal
		
		vec2 dx_tex_coord = dFdx(fs_tex_coord);
		vec2 dy_tex_coord = dFdy(fs_tex_coord);
		vec3 dx_position = dFdx(fs_position);
		vec3 dy_position = dFdy(fs_position);
		
		// https://www.gamedeveloper.com/programming/three-normal-mapping-techniques-explained-for-the-mathematically-uninclined
		// Tangent is the +U direction in world space, bitangent is +V direction in world space.
		
		mat3 TBN;
		// TBN = mat3(T, cross(T, N), N); // ... this doesn't currently seem to work for mirrored things
#if 1
		if (dx_tex_coord.x*dy_tex_coord.y - dx_tex_coord.y*dy_tex_coord.x < 0) {
			// Derive tangent using texcoord-X
			vec3 denormB = (dx_position*dy_tex_coord.x - dy_position*dx_tex_coord.x);
			vec3 B = normalize(denormB - N*dot(N, denormB)); // Tangent-space +Y direction
			vec3 T = cross(B, N);
			TBN = mat3(T, B, N);
		}
		else {
			// Derive bitangent using texcoord-Y
			vec3 denormT = dx_position*dy_tex_coord.y - dy_position*dx_tex_coord.y;
			vec3 T = normalize(denormT - N*dot(N, denormT)); // Tangent-space +X direction
			vec3 B = cross(T, N);
			TBN = mat3(T, B, N);
		}
#endif
		N = TBN * tangent_space_normal;
		
		vec2 velocity = (fs_position_cs.xy/fs_position_cs.w - PC.taa_jitter) - (fs_position_cs_old.xy/fs_position_cs_old.w - PC.taa_jitter_prev);
		
		// orm.z = 1.;
		
		out_base_color = base_color;
		out_normal = vec4(N*0.5 + 0.5, 1);
		out_orm = vec4(orm, 1);
		out_emissive = vec4(emissive, 1);
		out_velocity = velocity;
		
		/*
		
		// F0 is the reflectivity of the surface when viewed directly from the front at a 0-degree angle.
		// We can use this reflectivity to determine the ratio of reflected light vs refracted light.
		vec3 F0 = vec3(0.04);
		F0 = mix(F0, base_color.xyz, metallic);
		
		// Environment based lighting.
		
		vec2 fresnel_scale_bias = textureLod(sampler2D(BRDF_INTEGRATION_MAP, SAMPLER_LINEAR_CLAMP), vec2(VdotN, max(roughness, 0.05)), 0.).xy;
		
		// Fresnel gives the ratio of reflected light
		vec3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
		vec3 kS = F;
		vec3 kD = (1.0 - kS) * (1. - metallic);
		
		vec3 irradiance = textureLod(samplerCube(TEX_IRRADIANCE_MAP, SAMPLER_LINEAR_WRAP), N, 0.).rgb;
		vec3 diffuse = irradiance * base_color.xyz;
		
		vec3 R = reflect(-V, N);
		float roughness2 = roughness*roughness;
		R = mix(R, N, roughness2*roughness2); // bias the sample direction to avoid reflection leaking from negative angles.
		
		vec3 prefilter_spec_color = textureLod(samplerCube(PREFILTERED_ENV_MAP, SAMPLER_LINEAR_WRAP), R, roughness*4.).rgb;
		vec3 outgoing_light = emissive + kD * diffuse + prefilter_spec_color * (F0*fresnel_scale_bias.x + fresnel_scale_bias.y);
		
		// Apply tone-mapping and gamma correction
		vec3 color = aces_approx(outgoing_light);
		color = pow(color, vec3(1./2.2));
		out_color = vec4(color, 1);*/
	}
	

#endif
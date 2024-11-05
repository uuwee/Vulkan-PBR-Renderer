layout (local_size_x = 8, local_size_y = 8, local_size_z = 6) in;

GPU_BINDING(SAMPLER_LINEAR_CLAMP) sampler SAMPLER_LINEAR_CLAMP;
GPU_BINDING(TEX_ENV_CUBE) textureCube TEX_ENV_CUBE;

GPU_BINDING(OUTPUT) imageCube OUTPUT;

#define PI 3.14159265358979323846
#define GOLDEN_RATIO 1.61803398875

vec3 CubemapSampleDirFromFaceUV(int face_index, vec2 face_uv) {
	// From vulkan spec:
	// layer idx | face |   sc |   tc |  rc
	//         0 |   +X | -r.z | -r.y | r.x
	//         1 |   -X | +r.z | -r.y | r.x
	//         2 |   +Y | +r.x | +r.z | r.y
	//         3 |   -Y | +r.x | -r.z | r.y
	//         4 |   +Z | +r.x | -r.y | r.z
	//         5 |   -Z | -r.x | -r.y | r.z
	//
	// per-face UV coordinates
	// face_u = 0.5*sc/abs(rc) + 0.5
	// face_v = 0.5*tc/abs(rc) + 0.5
	
	// Going the other way (and replacing abs(rc) with 1, because we want the direction to lie on the unit cube):
	float sc = 2*(face_uv.x - 0.5);
	float tc = 2*(face_uv.y - 0.5);
	
	vec3 r;
	
	// This could be done faster without a switch statement by using an array lookup, but we'll leave it like this for clarity.
	switch (face_index) {
	case 0:
		r.z = -sc;
		r.y = -tc;
		r.x = +1;
		break;
	case 1:
		r.z = sc;
		r.y = -tc;
		r.x = -1;
		break;
	case 2:
		r.x = sc;
		r.z = tc;
		r.y = +1;
		break;
	case 3:
		r.x = sc;
		r.z = -tc;
		r.y = -1;
		break;
	case 4:
		r.x = sc;
		r.y = -tc;
		r.z = +1;
		break;
	case 5:
		r.x = -sc;
		r.y = -tc;
		r.z = -1;
		break;
	}
	
	return normalize(r);
}

vec3 Rotate(vec3 v, vec3 n, float theta) {
	// For derivation, see "3D Rotation about an Arbitrary Axis" from the book "3D Math Primer for Graphics and Game Development" (F. Dunn).
	return cos(theta)*(v - dot(v, n)*n) + sin(theta)*cross(n, v) + dot(v, n)*n;
}

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

float DistributionBeckmann(float NdotH, float m) {
	float m2 = m*m;
	float a = tan(acos(NdotH));
	float NdotH2 = NdotH*NdotH;
	return exp(-(a*a) / m2) / (PI*m2*NdotH2*NdotH2);
}
	
float GeometrySchlickGGX(float NdotV, float k) {
	float nom   = NdotV;
	float denom = NdotV * (1.0 - k) + k;
	return nom / denom;
}

layout(push_constant) uniform Constants {
	int mip_level;
} constants;

void main() {
	ivec2 output_size = imageSize(OUTPUT);
	vec2 uv = (vec2(gl_GlobalInvocationID.xy) + vec2(0.5)) / vec2(output_size);
	
	vec3 R = CubemapSampleDirFromFaceUV(int(gl_GlobalInvocationID.z), uv);
	vec3 some_vector = vec3(12.123825810901, 6.11831989512, -5.12039214121);
	vec3 tangent = normalize(cross(R, some_vector));
	
	vec4 sum = vec4(0);
	if (constants.mip_level == 0) { // roughness = 0
		sum = textureLod(samplerCube(TEX_ENV_CUBE, SAMPLER_LINEAR_CLAMP), R, 1.);
	}
	else {
		// the cubemap is 128x128 and we'll have mip levels 256, 128, 64, 32, 16
		float roughnesses[5] = {0, 0.03, 0.15, 0.4, 0.6};
		float roughness = roughnesses[constants.mip_level];
		
		#define SAMPLE_COUNT 4096*2
		
		float dw = 2.*PI / float(SAMPLE_COUNT);

		for (int i = 0; i < SAMPLE_COUNT; i++) {
			float x = float(i) / float(SAMPLE_COUNT); // https://extremelearning.com.au/evenly-distributing-points-on-a-sphere/
			float y = float(i) / GOLDEN_RATIO;
			float pitch = PI - acos(x - 1.); // Cylindrical equal area projection on the top half of the sphere
			float yaw = 2.*PI*y;
			
			// We assume N = R
			vec3 L = R;
			L = Rotate(L, tangent, pitch);
			L = Rotate(L, R, yaw);
			vec3 H = normalize(L + R);
			
			// the mip level is just a heuristic I came up with
			//vec3 L_radiance = textureLod(samplerCube(TEX_ENV_CUBE, SAMPLER_LINEAR_CLAMP), L, 2. + max(sqrt(r), roughness)*4.).rgb;
			vec3 L_radiance = textureLod(samplerCube(TEX_ENV_CUBE, SAMPLER_LINEAR_CLAMP), L, 3. + float(constants.mip_level)).rgb;
			
			//float weight = DistributionGGX(dot(H, R), max(0.015, roughness)) * cos_theta;
			float D = DistributionBeckmann(cos(pitch*0.5), roughness);
			// float D = DistributionBeckmann(max(dot(H, R), 0.), roughness*roughness);
			sum += D * vec4(L_radiance, 1.) * cos(pitch) * dw;
		}
		sum /= PI; // we store the result divided by PI
	}
	
	//if (sum.x < 0 || sum.y < 0 || sum.z < 0) sum = vec4(1, 0, 0, 1);
	//else sum = vec4(1, 1, 1, 1);
	
	imageStore(OUTPUT, ivec3(gl_GlobalInvocationID), sum);
}
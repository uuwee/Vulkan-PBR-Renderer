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

void main() {
	vec2 uv = (vec2(gl_GlobalInvocationID.xy) + vec2(0.5)) / vec2(32.);
	
	vec3 some_vector = vec3(12.123825810901, 6.11831989512, -5.12039214121);
	
	vec3 N = CubemapSampleDirFromFaceUV(int(gl_GlobalInvocationID.z), uv);
	vec3 tangent = normalize(cross(N, some_vector));
	
	#define SAMPLE_COUNT 1024
	
	vec4 sum = vec4(0);
	for (int i = 0; i < SAMPLE_COUNT; i++) {
		float x = float(i) / float(SAMPLE_COUNT); // https://extremelearning.com.au/evenly-distributing-points-on-a-sphere/
		float y = float(i) / GOLDEN_RATIO;
		float pitch = PI - acos(x - 1.); // Cylindrical equal area projection on the top half of the sphere
		float yaw = 2.*PI*y;
		
		vec3 sample_dir = N;
		sample_dir = Rotate(sample_dir, tangent, pitch);
		sample_dir = Rotate(sample_dir, N, yaw);
		
		vec3 sample_val = textureLod(samplerCube(TEX_ENV_CUBE, SAMPLER_LINEAR_CLAMP), sample_dir, 6.).rgb;
		sum += vec4(cos(pitch) * sample_val, 0);
	}
	sum /= SAMPLE_COUNT;
	
	//vec3 sum = textureLod(samplerCube(TEX_ENV_CUBE, SAMPLER_LINEAR_CLAMP), N, 0.).rgb;
	
	imageStore(OUTPUT, ivec3(gl_GlobalInvocationID), sum);
}
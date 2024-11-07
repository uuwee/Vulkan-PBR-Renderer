layout (local_size_x = 8, local_size_y = 8, local_size_z = 6) in;

GPU_BINDING(OUTPUT) image2D OUTPUT;

#define PI 3.14159265358979323846
#define GOLDEN_RATIO 1.61803398875

float GeometrySchlickGGX(float NdotV, float roughness) {
	// from https://learnopengl.com/PBR/IBL/Specular-IBL
	float a = roughness;
	float k = (a * a) / 2.0;
	float nom   = NdotV;
	float denom = NdotV * (1.0 - k) + k;
	return nom / denom;
}

// https://google.github.io/filament/Filament.html#materialsystem/energyconservation
//float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
//    float a2 = roughness * roughness;
//    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
//    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
//    return 0.5 / (GGXV + GGXL);
//}

float GeometrySmith(float NdotV_max0, float NdotL_max0, float k) {
	//return V_SmithGGXCorrelated(NdotV_max0, NdotL_max0, k);
	
	// from https://learnopengl.com/PBR/IBL/Specular-IBL
	float ggx1 = GeometrySchlickGGX(NdotV_max0, k);
	float ggx2 = GeometrySchlickGGX(NdotL_max0, k);
	return ggx1 * ggx2;
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
	
float DistributionGGX(float NdotH_max0, float roughness) {
	float a = roughness * roughness;
	float a2     = a*a;
	float NdotH2 = NdotH_max0*NdotH_max0;

	float nom    = a2;
	float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
	denom        = PI * denom * denom;

	return nom / denom;
}

float GeometryMikkelsen(float NdotH, float VdotN, float LdotN, float VdotH) {
	return min(1., min(2.*NdotH*VdotN / VdotH, 2.*NdotH*LdotN / VdotH));
}

vec3 Rotate(vec3 v, vec3 n, float theta) {
	// For derivation, see "3D Rotation about an Arbitrary Axis" from the book "3D Math Primer for Graphics and Game Development" (F. Dunn).
	return cos(theta)*(v - dot(v, n)*n) + sin(theta)*cross(n, v) + dot(v, n)*n;
}
	
	float RadicalInverse_VdC(uint bits) 
	{
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return float(bits) * 2.3283064365386963e-10; // / 0x100000000
	}
	// ----------------------------------------------------------------------------
	vec2 Hammersley(uint i, uint N)
	{
		return vec2(float(i)/float(N), RadicalInverse_VdC(i));
	}
	vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
	{
		float a = roughness*roughness;
		
		float phi = 2.0 * PI * Xi.x;
		float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
		float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
		
		// from spherical coordinates to cartesian coordinates
		vec3 H;
		H.x = cos(phi) * sinTheta;
		H.y = sin(phi) * sinTheta;
		H.z = cosTheta;
		
		// from tangent-space vector to world-space sample vector
		vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
		vec3 tangent   = normalize(cross(up, N));
		vec3 bitangent = cross(N, tangent);
		
		vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
		return normalize(sampleVec);
	}
	
	vec2 IntegrateBRDF(float NdotV, float roughness)
	{
		vec3 V;
		V.x = sqrt(1.0 - NdotV*NdotV);
		V.y = 0.0;
		V.z = NdotV; // V is correct. dot(N, V) really is NdotV.

		float A = 0.0;
		float B = 0.0;

		vec3 N = vec3(0.0, 0.0, 1.0);

		const uint SAMPLE_COUNT = 1024u;
		for(uint i = 0u; i < SAMPLE_COUNT; ++i)
		{
			vec2 Xi = Hammersley(i, SAMPLE_COUNT);
			vec3 H  = ImportanceSampleGGX(Xi, N, roughness);
			vec3 L  = 2.0 * dot(V, H) * H - V;

			float NdotL = max(L.z, 0.0);
			float NdotH = max(H.z, 0.0);
			float VdotH = max(dot(V, H), 0.0);

			if(NdotL > 0.0)
			{
				float G = GeometrySmith(NdotV, NdotL, roughness);
				float G_Vis = (G * VdotH) / (NdotH * NdotV);
				float Fc = pow(1.0 - VdotH, 5.0);

				A += G_Vis * (1.0 - Fc);
				B += Fc * G_Vis;
			}
		}
		A /= float(SAMPLE_COUNT);
		B /= float(SAMPLE_COUNT);
			//A = abs((dot(N, V) - NdotV)*1000.);
		return vec2(A, B);
	}

void main() {
	vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / 256.;
	
	// ... Yeah I think we should importance sample this. On low roughnesses it's bad.
	const int SAMPLE_COUNT = 4096*1;

	// The equation we're integrating:
	// kS * dot(N, L) * D(N, H) * F(V, H) * G(N, L, V, H) / (4 * dot(N, L) * dot(N, V))
	// Assume F(V, H) = 1 which also means kS = 1
	// Simplifies to:
	// D(N, H) * G(N, L, V, H) / (4 * dot(N, V))
	// 
	float NdotV = uv.x;
	float roughness = uv.y;
	
	// Let's do all calculations in tangent space.
	vec3 N = vec3(0, 0, 1);
	vec3 V = N;
	V = Rotate(V, vec3(1, 0, 0), acos(NdotV)); // The yaw shouldn't matter here.
	//V.x = sqrt(1.0 - NdotV*NdotV);
	//V.y = 0.0;
	//V.z = NdotV; // V is correct. dot(N, V) really is NdotV.
	
	float scale = 0.;
	float bias = 0.;
	
	float dw = 2*PI/SAMPLE_COUNT;
	
	for (int i = 0; i < SAMPLE_COUNT; i++) {
		float x = float(i) / float(SAMPLE_COUNT); // https://extremelearning.com.au/evenly-distributing-points-on-a-sphere/
		float y = float(i) / GOLDEN_RATIO;
		float pitch = PI - acos(x - 1.); // Cylindrical equal area projection on the top half of the sphere
		float yaw = 2.*PI*y;

		vec3 L = N;
		L = Rotate(L, vec3(1, 0, 0), pitch);
		L = Rotate(L, N, yaw);
		vec3 H = normalize(L + V); // We should try to spread H uniformly.

		float NdotL = dot(N, L);
		float NdotH = dot(N, H);
		float VdotH = dot(V, H);
		
		//if (VdotH < 0. || NdotH < 0. || NdotL < 0.) { // this shouldnt happen
		//	continue; // ... except in Karis' model it can.
		//	//imageStore(OUTPUT, ivec2(gl_GlobalInvocationID.xy), vec4(1, 1, 0, 1));
		//	//return;
		//}
		
		//float D = DistributionPhong(NdotH, 1.);
		float D = DistributionBeckmann(NdotH, roughness);
		float G = GeometryMikkelsen(NdotH, NdotV, NdotL, VdotH);
		//float G = GeometrySchlickGGX(NdotV, roughness);
		
		float Fc = pow(1. - VdotH, 5.);
		
		scale += D * G * (1-Fc) * dw / (4. * NdotV);
		bias += D * G * (0+Fc) * dw / (4. * NdotV);
	}
	
	//{
	//	vec2 karis = IntegrateBRDF(NdotV, roughness);
	//	scale = karis.x;
	//	bias = karis.y;
	//}
	
	//scale = abs((dot(N, V) - NdotV)*1000.);
	imageStore(OUTPUT, ivec2(gl_GlobalInvocationID.xy), vec4(scale, bias, 0, 1));
}

#define _CRT_SECURE_NO_WARNINGS

#include "third_party/HandmadeMath.h"

#include "src/fire/fire_ds.h"

#define STR_USE_FIRE_DS
#include "src/fire/fire_string.h"

#define FIRE_OS_WINDOW_IMPLEMENTATION
#include "src/fire/fire_os_window.h"

#include "src/gpu/gpu.h"

#define GPU_VALIDATION_ENABLED false

// gpu vulkan implementation
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include "src/gpu/gpu_vulkan.h"

#define ASSIMP_DLL
#define ASSIMP_API 
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <assimp/cexport.h>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

#include "third_party/ddspp.h"

#define CAMERA_VIEW_SPACE_IS_POSITIVE_Y_DOWN
#include "src/utils/key_input/key_input.h"
#include "src/utils/key_input/key_input_fire_os.h"
#include "src/utils/camera.h"

#define TODO() __debugbreak()

// ----------------

static const uint32_t window_w = 1920;
static const uint32_t window_h = 1080;

static const float Z_NEAR = 0.02f;
static const float Z_FAR = 10000.f;

#define LIGHTGRID_SIZE 128
static const float LIGHTGRID_EXTENT_WS = 40.f;

static const HMM_Vec3 world_import_offset = {0, 25.f, 0};

// ----------------

#define MESH_WORLD_PATH  "../resources/SunTemple/SunTemple.fbx"
//#define MESH_WORLD_PATH  "(path to bistro)/Bistro_v5_2/BistroExterior.fbx"

// Define all your assets here!
// To add a new automatically hotreloaded asset, add it to this list. You can then access it through ASSETS.[name], e.g. ASSETS.TEX_BASE_COLOR
#define ASSETS_LIST\
	ASSET(TEX_ENV_CUBE,                    "C:/EeroSampleAssets/HDRIs/shipyard_cranes_track_cube.hdr")\
	ASSET(MESH_SKYBOX,                     "C:/EeroSampleAssets/basic/Skybox_200x200x200.fbx")\
	ASSET(SHADER_LIGHTGRID_VOXELIZE,       "../src/demo_global_illumination_pbr/lightgrid_voxelize.glsl")\
	ASSET(SHADER_LIGHTGRID_SWEEP,          "../src/demo_global_illumination_pbr/lightgrid_sweep.glsl")\
	ASSET(SHADER_GEOMETRY_PASS,            "../src/demo_global_illumination_pbr/geometry_pass.glsl")\
	ASSET(SHADER_LIGHTING_PASS,            "../src/demo_global_illumination_pbr/lighting_pass.glsl")\
	ASSET(SHADER_TAA_RESOLVE,              "../src/demo_global_illumination_pbr/taa_resolve.glsl")\
	ASSET(SHADER_BLOOM_DOWNSAMPLE,         "../src/demo_global_illumination_pbr/bloom_downsample.glsl")\
	ASSET(SHADER_BLOOM_UPSAMPLE,           "../src/demo_global_illumination_pbr/bloom_upsample.glsl")\
	ASSET(SHADER_FINAL_POST_PROCESS,       "../src/demo_global_illumination_pbr/final_post_process.glsl")\
	ASSET(SHADER_SUN_DEPTH_PASS,           "../src/demo_global_illumination_pbr/sun_depth_pass.glsl")\
	ASSET(SHADER_GEN_IRRADIANCE_MAP,       "../src/demo_global_illumination_pbr/gen_irradiance_map.glsl")\
	ASSET(SHADER_GEN_PREFILTERED_ENV_MAP,  "../src/demo_global_illumination_pbr/gen_prefiltered_env_map.glsl")\
	ASSET(SHADER_GEN_BRDF_INTEGRATION_MAP, "../src/demo_global_illumination_pbr/gen_brdf_integration_map.glsl")\
	ASSET(MESH_WORLD,                      MESH_WORLD_PATH)\

typedef int AssetIndex;
	
// This struct will be populated with an array of integers going from 0 to the last asset idx. It's a bit hacky.
static struct {
	#define ASSET(NAME, PATH) AssetIndex NAME;
	ASSETS_LIST
	#undef ASSET
} ASSETS;

#define ASSET(NAME, PATH) STR_V(PATH),
static const STR_View ASSET_PATHS[] = {ASSETS_LIST};
#undef ASSET

typedef struct {
	HMM_Vec3 position;
	HMM_Vec3 normal; // this could be packed better!
	HMM_Vec3 tangent; // this could be packed better!
	HMM_Vec2 tex_coord;
} Vertex;

typedef struct {
	GPU_PipelineLayout* pipeline_layout;
	uint32_t sampler_binding;
	uint32_t tex_env_cube_binding;
	uint32_t output_binding;
	GPU_ComputePipeline* pipeline;
} GenIrradianceMapCtx;

typedef struct {
	GPU_PipelineLayout* pipeline_layout;
	uint32_t sampler_binding;
	uint32_t tex_env_cube_binding;
	uint32_t output_binding;
	GPU_ComputePipeline* pipeline;
} GenPrefilteredEnvMapCtx;

typedef struct {
	GPU_PipelineLayout* pipeline_layout;
	uint32_t output_binding;
	GPU_ComputePipeline* pipeline;
} GenBRDFintegrationMapCtx;

typedef struct {
	GPU_PipelineLayout* pipeline_layout;
	
	// TODO: it's a bit dumb that we might forget to initialize a binding and it'll still be a valid binding.
	// Maybe we should instead have  `struct GPU_Binding { U16 binding; bool has_initialized; }`

	uint32_t globals_binding;
	uint32_t tex0_binding; // base color
	uint32_t tex1_binding; // normal
	uint32_t tex2_binding; // orm
	uint32_t tex3_binding; // emissive
	uint32_t sun_depth_map_binding_;

	uint32_t sampler_linear_clamp_binding;
	uint32_t sampler_linear_wrap_binding;
	uint32_t sampler_percentage_closer;
	
	uint32_t ssbo0_binding; // for vertex buffer in lightgrid voxelize
	uint32_t ssbo1_binding; // for index buffer in lightgrid voxelize
	uint32_t img0_binding; // for lightmap image in lightgrid voxelize
	// uint32_t img0_binding_uint;

	// TAA resolve stuff
	uint32_t prev_frame_result_binding;
	uint32_t lighting_result_rt;
	uint32_t gbuffer_depth_binding;
	uint32_t gbuffer_depth_prev_binding;
	uint32_t gbuffer_velocity_binding;
	uint32_t gbuffer_velocity_prev_binding;
} MainPassLayout;

typedef struct {
	GPU_PipelineLayout* pipeline_layout;
	uint32_t globals_binding;
	uint32_t gbuffer_base_color_binding;
	uint32_t gbuffer_normal_binding;
	uint32_t gbuffer_orm_binding;
	uint32_t gbuffer_emissive_binding;
	uint32_t gbuffer_depth_binding;
	uint32_t lightgrid_binding;
	uint32_t prev_frame_result_binding;
	uint32_t tex_irradiance_map_binding;
	uint32_t sampler_linear_clamp_binding;
	uint32_t sampler_linear_wrap_binding;
	uint32_t sampler_nearest_clamp_binding;
	uint32_t sampler_percentage_closer;
	uint32_t prefiltered_env_map_binding;
	uint32_t brdf_integration_map_binding;
	uint32_t sun_depth_map_binding;
	//uint32_t prev_frame_result_rt_binding;
} LightingPassLayout;

typedef uint64_t StringHash;

typedef struct {
	bool is_outdated[DS_ArrayCount(ASSET_PATHS)];
	
	// Getting the file modtime is relatively slow, so we only call it on one asset each frame. A better solution would be to have a separate thread doing this.
	// Or, I wonder if it's possible to ask for modtime on directories.
	AssetIndex next_check_asset_idx;
	uint64_t file_modtimes[DS_ArrayCount(ASSET_PATHS)];
} Hotreloader;

typedef struct {
	GPU_Texture* tex_base_color;
	GPU_Texture* tex_normal;
	GPU_Texture* tex_orm;
	GPU_Texture* tex_emissive;

	uint32_t first_index;
	uint32_t index_count;
	GPU_DescriptorSet* descriptor_set;
} RenderObjectPart;

typedef struct {
	GPU_Buffer* vertex_buffer;
	GPU_Buffer* index_buffer;
	DS_DynArray<RenderObjectPart> parts;
} RenderObject;

#define BLOOM_PASS_COUNT 6

typedef struct {
	GPU_RenderPass* render_pass[2];
	GPU_GraphicsPipeline* pipeline[2];
	GPU_DescriptorSet* desc_set[2];
} BloomDownsamplePass;

typedef struct {
	GPU_RenderPass* render_pass[2];
	GPU_GraphicsPipeline* pipeline[2];
	GPU_DescriptorSet* desc_set[2];
} BloomUpsamplePass;

typedef struct Scene Scene;
struct Scene {
	GPU_RenderPass* lightgrid_voxelize_render_pass;
	GPU_RenderPass* geometry_render_pass[2];
	GPU_RenderPass* lighting_render_pass;
	GPU_RenderPass* taa_resolve_render_pass[2];
	GPU_RenderPass* sun_depth_render_pass;
	GPU_RenderPass* final_post_process_render_pass;
	GPU_Buffer* globals_buffer;

	GPU_Sampler* sampler_percentage_closer;

	//GenIrradianceMapCtx gen_irradiance_map_ctx;
	//GenPrefilteredEnvMapCtx gen_prefiltered_env_map_ctx;
	//GenBRDFintegrationMapCtx gen_brdf_integration_map_ctx;
	MainPassLayout main_pass_layout;
	LightingPassLayout lighting_pass_layout;
	
	// Let's have one giant vertex buffer and index buffer for the entire scene.
	// - One descriptor set per material
	// so when importing a mesh, I should batch everything of the same material into one vertex buffer.
	// This gives us 48 draw calls for an imported bistro
	RenderObject ro_world;
	
	RenderObject ro_skybox;

	GPU_Texture* sun_depth_rt;
	GPU_Texture* lightgrid;

	GPU_Texture* gbuffer_base_color;
	GPU_Texture* gbuffer_normal;
	GPU_Texture* gbuffer_orm;
	GPU_Texture* gbuffer_emissive;
	GPU_Texture* gbuffer_depth[2];
	GPU_Texture* gbuffer_velocity[2];

	GPU_Texture* lighting_result_rt;
	GPU_Texture* taa_output_rt[2]; // TAA output

	// After TAA we do bloom
	BloomDownsamplePass bloom_downsamples[BLOOM_PASS_COUNT];
	BloomUpsamplePass bloom_upsamples[BLOOM_PASS_COUNT];

	// hmm... when we're doing bloom in compute, we still need to have all mips bound as sampled, but then also have one mip bound as general.
	// So for this, we do need to support sampled texture views that only include one mip level.
	// So let's also support it for non-compute.
	
	GPU_Texture* bloom_downscale_rt;
	GPU_Texture* bloom_upscale_rt;

	// in the upsampling shader, we need to read from and write to the same mip level. So the only way to do bloom without compute shaders is to have a separate texture per each mip level. OR use additive blending.
	// GPU_Texture* bloom_mips[BLOOM_PASS_COUNT + 1]; // first element is NULL
	
	GPU_Texture* dummy_normal_map;
	GPU_Texture* dummy_black;
	GPU_Texture* dummy_white;
	GPU_Texture* tex_env_cube;
	GPU_Texture* irradiance_map;
	GPU_Texture* brdf_lut;
	GPU_Texture* tex_specular_env_map;

	GPU_GraphicsPipeline* lightgrid_voxelize_pipeline;
	GPU_ComputePipeline* lightgrid_sweep_pipeline;
	GPU_DescriptorSet* lightgrid_sweep_desc_set;
	
	GPU_GraphicsPipeline* lighting_pass_pipeline;
	GPU_DescriptorSet* lighting_pass_descriptor_set[2];

	GPU_GraphicsPipeline* taa_resolve_pipeline[2];
	GPU_DescriptorSet* taa_resolve_descriptor_set[2];

	GPU_GraphicsPipeline* final_post_process_pipeline;
	GPU_DescriptorSet* final_post_process_desc_set[2];

	GPU_Texture* textures[DS_ArrayCount(ASSET_PATHS)];
	//GPU_DescriptorSet* desc_set;
	GPU_GraphicsPipeline* sun_depth_pipeline;
	GPU_GraphicsPipeline* geometry_pass_pipeline[2];
	//GPU_GraphicsPipeline* skybox_pipeline;
};

// ---------------------------------------------------------------------

// Global arena for per-frame allocations
static DS_Arena* TEMP;

// -- OS-specific ------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static bool OS_FileLastModificationTime(STR_View filepath, uint64_t* out_modtime) {
	wchar_t filepath_wide[MAX_PATH];
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, STR_ToC(TEMP, filepath.data), -1, filepath_wide, MAX_PATH);

	HANDLE h = CreateFileW(filepath_wide, 0, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	bool ok = h != INVALID_HANDLE_VALUE;
	if (ok) {
		ok = GetFileTime(h, NULL, NULL, (FILETIME*)out_modtime) != 0;
		CloseHandle(h);
	}
	return ok;
}

static void OS_SleepMilliseconds(uint32_t ms) {
	Sleep(ms);
}

static bool OS_ReadEntireFile(DS_Arena* arena, STR_View filepath, STR_View* out_data) {
	FILE* f = fopen(STR_ToC(TEMP, filepath), "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		long fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		char* data = DS_ArenaPush(arena, fsize);
		fread(data, fsize, 1, f);

		fclose(f);
		STR_View result = {data, fsize};
		*out_data = result;
	}
	return f != NULL;
}

static void OS_MessageBox(STR_View message) {
	MessageBoxA(NULL, STR_ToC(TEMP, message), "INFO", 0);
}

// ---------------------------------------------------------------------

// may return NULL
static GPU_Texture* LoadMeshTexture(STR_View base_directory, struct aiMaterial* mat, enum aiTextureType type) {
	if (aiGetMaterialTextureCount(mat, type) != 0) {
		struct aiString path;
		uint32_t flags;
		if (aiGetMaterialTexture(mat, type, 0, &path, NULL, NULL, NULL, NULL, NULL, &flags) == AI_SUCCESS) {
			DS_ArenaMark dds_file_mark = DS_ArenaGetMark(TEMP);
			STR_View texture_path = STR_Form(TEMP, "%v/%s", base_directory, path.data);

			STR_View tex_file_data;
			if (!OS_ReadEntireFile(TEMP, texture_path, &tex_file_data)) TODO();

			DDSPP_Descriptor desc = {};
			DDSPP_Result result = ddspp_decode_header((uint8_t*)tex_file_data.data, &desc);
			const void* tex_data = tex_file_data.data + desc.headerSize + ddspp_get_offset(&desc, 0, 0);
			
			GPU_Format format;
			if (desc.format == BC1_UNORM)           format = GPU_Format_BC1_RGBA_UN;
			else if (desc.format == BC3_UNORM)      format = GPU_Format_BC3_RGBA_UN;
			else if (desc.format == R8G8B8A8_UNORM) format = GPU_Format_RGBA8UN;
			else if (desc.format == BC5_UNORM)      format = GPU_Format_BC5_UN;
			else TODO();

			GPU_Texture* texture = GPU_MakeTexture(format, desc.width, desc.height, 1, 0, tex_data);
			DS_ArenaSetMark(TEMP, dds_file_mark);

			//RL_FREE(tex_data);
			return texture;
		}
	}
	return NULL;
}

static bool ReloadMesh(DS_Arena* arena, Scene* s, RenderObject* render_object, STR_View filepath) {
	// free existing data
	{
		GPU_DestroyBuffer(render_object->vertex_buffer);
		GPU_DestroyBuffer(render_object->index_buffer);
		
		for (int i = 0; i < render_object->parts.count; i++) {
			RenderObjectPart* part = &render_object->parts[i];
			GPU_DestroyDescriptorSet(part->descriptor_set);
			GPU_DestroyTexture(part->tex_base_color);
			GPU_DestroyTexture(part->tex_normal);
			GPU_DestroyTexture(part->tex_orm);
			GPU_DestroyTexture(part->tex_emissive);
		}

		*render_object = {};
	}

	// init render object
	DS_ArrInit(&render_object->parts, arena);
	
	DS_ArenaMark T = DS_ArenaGetMark(TEMP);
	
	assert(!STR_ContainsU(filepath, '\\')); // we should use / for path separators
	STR_View base_directory = STR_BeforeLast(filepath, '/');

	char* filepath_cstr = STR_ToC(TEMP, filepath);
	
	// aiProcess_GlobalScale uses the scale settings from the file. It looks like the blender exporter uses it too.
	const struct aiScene* scene = aiImportFile(filepath_cstr, aiProcess_Triangulate|aiProcess_PreTransformVertices|aiProcess_GlobalScale|aiProcess_CalcTangentSpace);
	assert(scene != NULL);

	typedef struct {
		DS_DynArray<Vertex> vertices;
		DS_DynArray<uint32_t> indices;
	} MatMesh;
	
	DS_DynArray<MatMesh> mat_meshes = {TEMP};
	
	MatMesh empty_mat_mesh = {};
	DS_ArrResize(&mat_meshes, empty_mat_mesh, scene->mNumMaterials);
	
	uint32_t total_vertex_count = 0;
	uint32_t total_index_count = 0;

	for (uint32_t mesh_idx = 0; mesh_idx < scene->mNumMeshes; mesh_idx++) {
		aiMesh* mesh = scene->mMeshes[mesh_idx];
		
		MatMesh* mat_mesh = &mat_meshes[mesh->mMaterialIndex];
		DS_ArrInit(&mat_mesh->vertices, TEMP);
		DS_ArrInit(&mat_mesh->indices, TEMP);
		uint32_t first_new_vertex = (uint32_t)mat_mesh->vertices.count;

		for (uint32_t i = 0; i < mesh->mNumVertices; i++) {
			if (mesh->mNormals == NULL) TODO();
			if (mesh->mTextureCoords[0] == NULL) TODO();
			aiVector3D pos = mesh->mVertices[i];
			aiVector3D normal = mesh->mNormals[i];
			aiVector3D tangent = mesh->mTangents[i];
			aiVector3D tex_coord = mesh->mTextureCoords[0][i];
			
			// NOTE: flip Y and Z and texcoord y
			Vertex v;
			v.position  = {pos.x + world_import_offset.X, pos.z * -1.f + world_import_offset.Y, pos.y + world_import_offset.Z};
			v.normal    = {normal.x,  normal.z  * -1.f, normal.y};
			v.tangent   = {tangent.x, tangent.z * -1.f, tangent.y};
			v.tex_coord = {tex_coord.x, 1.f - tex_coord.y};
			DS_ArrPush(&mat_mesh->vertices, v);
		}

		for (uint32_t i = 0; i < mesh->mNumFaces; i++) {
			aiFace face = mesh->mFaces[i];
			if (face.mNumIndices != 3) TODO();

			DS_ArrPush(&mat_mesh->indices, first_new_vertex + face.mIndices[0]);
			DS_ArrPush(&mat_mesh->indices, first_new_vertex + face.mIndices[1]);
			DS_ArrPush(&mat_mesh->indices, first_new_vertex + face.mIndices[2]);
		}
		
		total_vertex_count += mesh->mNumVertices;
		total_index_count += mesh->mNumFaces*3;
	}

	void* merged_vertices = DS_ArenaPush(TEMP, total_vertex_count * sizeof(Vertex));
	void* merged_indices = DS_ArenaPush(TEMP, total_index_count * sizeof(uint32_t));
	
	{
		uint32_t first_vertex = 0;
		uint32_t first_index = 0;
		
		for (int mat_mesh_i = 0; mat_mesh_i < mat_meshes.count; mat_mesh_i++) {
			MatMesh* mat_mesh = &mat_meshes[mat_mesh_i];

			RenderObjectPart part = {};
			part.first_index = first_index;
			part.index_count = (uint32_t)mat_mesh->indices.count;

			memcpy((Vertex*)merged_vertices + first_vertex, mat_mesh->vertices.data, mat_mesh->vertices.count * sizeof(Vertex));

			for (int i = 0; i < mat_mesh->indices.count; i++) {
				((uint32_t*)merged_indices)[first_index] = first_vertex + mat_mesh->indices[i];
				first_index++;
			}
			first_vertex += (uint32_t)mat_mesh->vertices.count;
			
			DS_ArrPush(&render_object->parts, part);
		}

		assert(first_vertex == total_vertex_count);
		assert(first_index == total_index_count);
	}

	render_object->vertex_buffer = GPU_MakeBuffer(total_vertex_count * sizeof(Vertex), GPU_BufferFlag_GPU | GPU_BufferFlag_StorageBuffer, merged_vertices);
	render_object->index_buffer = GPU_MakeBuffer(total_index_count * sizeof(uint32_t), GPU_BufferFlag_GPU | GPU_BufferFlag_StorageBuffer, merged_indices);
	
	for (int i = 0; i < mat_meshes.count; i++) {
		MatMesh* mat_mesh = &mat_meshes[i];
		RenderObjectPart* part = &render_object->parts[i];

		aiMaterial* mat = scene->mMaterials[i];
		part->tex_base_color = LoadMeshTexture(base_directory, mat, aiTextureType_DIFFUSE);
		part->tex_normal     = LoadMeshTexture(base_directory, mat, aiTextureType_NORMALS);
		part->tex_orm        = LoadMeshTexture(base_directory, mat, aiTextureType_SPECULAR);
		part->tex_emissive   = LoadMeshTexture(base_directory, mat, aiTextureType_EMISSIVE);

		MainPassLayout* pass = &s->main_pass_layout;

		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);

		GPU_SetBufferBinding(desc_set, pass->globals_binding, s->globals_buffer);
		GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
		GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
		GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, s->sampler_percentage_closer);

		GPU_SetTextureBinding(desc_set, pass->tex0_binding, part->tex_base_color ? part->tex_base_color : s->dummy_white);
		GPU_SetTextureBinding(desc_set, pass->tex1_binding, part->tex_normal ? part->tex_normal : s->dummy_normal_map);
		GPU_SetTextureBinding(desc_set, pass->tex2_binding, part->tex_orm ? part->tex_orm : s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex3_binding, part->tex_emissive ? part->tex_emissive : s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, s->sun_depth_rt);

		// for lightgrid voxelization
		GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, render_object->vertex_buffer);
		GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, render_object->index_buffer);
		GPU_SetStorageImageBinding(desc_set, pass->img0_binding, s->lightgrid, 0);
		//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, s->lightgrid, 0);

		// ... unused descriptors. This is stupid.
		GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, s->dummy_black);

		GPU_FinalizeDescriptorSet(desc_set);
		part->descriptor_set = desc_set;
	}

	aiReleaseImport(scene);

	assert(arena != TEMP);
	DS_ArenaSetMark(TEMP, T);
	return true;
}

// Should we move the hotreloader into Utils? The same way camera is an util.
static void UpdateHotreloader(Hotreloader* hotreloader) {
	AssetIndex check_idx = hotreloader->next_check_asset_idx;
	hotreloader->next_check_asset_idx = (hotreloader->next_check_asset_idx + 1) % DS_ArrayCount(ASSET_PATHS);

	uint64_t modtime;
	bool ok = OS_FileLastModificationTime(ASSET_PATHS[check_idx], &modtime);
	assert(ok);

	if (hotreloader->file_modtimes[check_idx] == 0) { // Initialize modtime for this file
		hotreloader->file_modtimes[check_idx] = modtime;
	}

	if (modtime != hotreloader->file_modtimes[check_idx]) {
		if (check_idx == 1) TODO();
		hotreloader->is_outdated[check_idx] = true;
		hotreloader->file_modtimes[check_idx] = modtime;
	}
}

static GPU_Texture* RefreshTexture(Hotreloader* r, Scene* s, AssetIndex asset, bool* reloaded) {
	if (r->is_outdated[asset]) {
		int x, y, comp;
		uint8_t* data = stbi_load(ASSET_PATHS[asset].data, &x, &y, &comp, 4);
		
		GPU_DestroyTexture(s->textures[asset]);
		s->textures[asset] = GPU_MakeTexture(GPU_Format_RGBA8UN, (uint32_t)x, (uint32_t)y, 1, GPU_TextureFlag_HasMipmaps, data);
		stbi_image_free(data);
		
		*reloaded = true;
	}
	return s->textures[asset];
}

static GPU_ComputePipeline* LoadComputeShader(AssetIndex asset, GPU_PipelineLayout* pipeline_layout, GPU_ShaderDesc* cs_desc) {
	for (;;) {
		STR_View shader;
		while (!OS_ReadEntireFile(TEMP, ASSET_PATHS[asset], &shader)) {}

		cs_desc->glsl = {shader.data, shader.size};
		cs_desc->glsl_debug_filepath = {ASSET_PATHS[asset].data, ASSET_PATHS[asset].size};

		GPU_GLSLErrorArray errors = {};
		cs_desc->spirv = GPU_SPIRVFromGLSL(TEMP, GPU_ShaderStage_Compute, pipeline_layout, cs_desc, &errors);
		if (cs_desc->spirv.length == 0) {
			STR_View err = STR_Form(TEMP, "Error in \"%v\": %v", ASSET_PATHS[asset], GPU_JoinGLSLErrorString(TEMP, errors));
			OS_MessageBox(err);
			continue;
		}
		break;
	}

	GPU_ComputePipeline* result = GPU_MakeComputePipeline(pipeline_layout, cs_desc);
	return result;
}

static void LoadVertexAndFragmentShader(DS_Arena* arena, AssetIndex asset, GPU_PipelineLayout* pipeline_layout, GPU_ShaderDesc* vs_desc, GPU_ShaderDesc* fs_desc) {
	for (;;) {
		STR_View shader;
		while (!OS_ReadEntireFile(TEMP, ASSET_PATHS[asset], &shader)) {}

		fs_desc->glsl = {shader.data, shader.size};
		vs_desc->glsl = {shader.data, shader.size};
		fs_desc->glsl_debug_filepath = {ASSET_PATHS[asset].data, ASSET_PATHS[asset].size};
		vs_desc->glsl_debug_filepath = {ASSET_PATHS[asset].data, ASSET_PATHS[asset].size};

		GPU_GLSLErrorArray errors = {};
		vs_desc->spirv = GPU_SPIRVFromGLSL(arena, GPU_ShaderStage_Vertex, pipeline_layout, vs_desc, &errors);
		if (vs_desc->spirv.length == 0) {
			STR_View err = STR_Form(TEMP, "Error in \"%v\": %v", ASSET_PATHS[asset], GPU_JoinGLSLErrorString(TEMP, errors));
			OS_MessageBox(err);
			continue;
		}
		fs_desc->spirv = GPU_SPIRVFromGLSL(arena, GPU_ShaderStage_Fragment, pipeline_layout, fs_desc, &errors);
		if (fs_desc->spirv.length == 0) {
			STR_View err = STR_Form(TEMP, "Error in \"%v\": %v", ASSET_PATHS[asset], GPU_JoinGLSLErrorString(TEMP, errors));
			OS_MessageBox(err);
			continue;
		}
		break;
	}
}

static void UpdateScene(DS_Arena* persistent_arena, DS_Arena* TEMP, Hotreloader* r, Scene* s) {
	DS_ArenaMark T = DS_ArenaGetMark(TEMP);
	
	static bool first = true;

	if (r->is_outdated[ASSETS.TEX_ENV_CUBE]) {
		assert(first); // if the env cube is reloaded, we would need to recreate all mesh descriptor sets... That'd be dumb and a good reason to split the descriptor sets
		int x, y, comp;
		void* data = stbi_loadf(ASSET_PATHS[ASSETS.TEX_ENV_CUBE].data, &x, &y, &comp, 4);
		assert(y == x*6);
	
		GPU_DestroyTexture(s->textures[ASSETS.TEX_ENV_CUBE]);
		s->textures[ASSETS.TEX_ENV_CUBE] = GPU_MakeTexture(GPU_Format_RGBA32F, (uint32_t)x, (uint32_t)x, 1, GPU_TextureFlag_Cubemap|GPU_TextureFlag_HasMipmaps, data);
		
		stbi_image_free(data);
	}
	s->tex_env_cube = s->textures[ASSETS.TEX_ENV_CUBE];

	if (r->is_outdated[ASSETS.SHADER_SUN_DEPTH_PASS]) {
		MainPassLayout* pass = &s->main_pass_layout;
		GPU_WaitUntilIdle();
		GPU_DestroyGraphicsPipeline(s->sun_depth_pipeline);

		GPU_Access accesses[] = {GPU_Read(pass->globals_binding)};
		
		GPU_ShaderDesc vs_desc = {};
		vs_desc.accesses = accesses; vs_desc.accesses_count = DS_ArrayCount(accesses);
		
		GPU_ShaderDesc fs_desc = {};
		LoadVertexAndFragmentShader(TEMP, ASSETS.SHADER_SUN_DEPTH_PASS, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_Format vertex_formats[] = { GPU_Format_RGB32F, GPU_Format_RGB32F, GPU_Format_RGB32F, GPU_Format_RG32F };

		GPU_GraphicsPipelineDesc desc = {};
		desc.layout = pass->pipeline_layout;
		desc.render_pass = s->sun_depth_render_pass;
		desc.vs = vs_desc;
		desc.fs = fs_desc;
		desc.vertex_input_formats = vertex_formats;
		desc.vertex_input_formats_count = DS_ArrayCount(vertex_formats);
		desc.enable_depth_test = true;
		desc.enable_depth_write = true;
		// desc.cull_mode = GPU_CullMode_DrawCCW,
		s->sun_depth_pipeline = GPU_MakeGraphicsPipeline(&desc);
	}

	if (r->is_outdated[ASSETS.SHADER_LIGHTGRID_VOXELIZE]) {
		MainPassLayout* pass = &s->main_pass_layout;
		GPU_WaitUntilIdle();
		GPU_DestroyGraphicsPipeline(s->lightgrid_voxelize_pipeline);
		
		GPU_Access vs_accesses[] = {
			GPU_Read(pass->globals_binding),
			GPU_Read(pass->ssbo0_binding),
			GPU_Read(pass->ssbo1_binding)
		};

		GPU_Access fs_accesses[] = {
			GPU_Read(pass->globals_binding),
			GPU_Read(pass->sun_depth_map_binding_),
			GPU_Read(pass->sampler_percentage_closer),
			GPU_Read(pass->sampler_linear_wrap_binding),
			GPU_Read(pass->tex0_binding), // base color
			GPU_Read(pass->tex3_binding), // emissive
			GPU_ReadWrite(pass->img0_binding)
		};
		
		GPU_ShaderDesc vs_desc = {};
		vs_desc.accesses = vs_accesses; vs_desc.accesses_count = DS_ArrayCount(vs_accesses);
		
		GPU_ShaderDesc fs_desc = {};
		fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);

		LoadVertexAndFragmentShader(TEMP, ASSETS.SHADER_LIGHTGRID_VOXELIZE, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_GraphicsPipelineDesc desc = {};
		desc.layout = pass->pipeline_layout;
		desc.render_pass = s->lightgrid_voxelize_render_pass;
		desc.vs = vs_desc;
		desc.fs = fs_desc;
		desc.enable_conservative_rasterization = true;
		s->lightgrid_voxelize_pipeline = GPU_MakeGraphicsPipeline(&desc);
	}

	if (r->is_outdated[ASSETS.SHADER_LIGHTGRID_SWEEP]) {
		MainPassLayout* pass = &s->main_pass_layout;
		GPU_WaitUntilIdle();
		GPU_DestroyComputePipeline(s->lightgrid_sweep_pipeline);

		GPU_Access cs_accesses[] = {
			GPU_ReadWrite(pass->img0_binding),
		};

		GPU_ShaderDesc cs_desc = {};
		cs_desc.accesses = cs_accesses; cs_desc.accesses_count = DS_ArrayCount(cs_accesses);
		LoadComputeShader(ASSETS.SHADER_LIGHTGRID_SWEEP, pass->pipeline_layout, &cs_desc);
		s->lightgrid_sweep_pipeline = GPU_MakeComputePipeline(pass->pipeline_layout, &cs_desc);

		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
		GPU_SetStorageImageBinding(desc_set, pass->img0_binding, s->lightgrid, 0);
		//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, s->lightgrid, 0);
		
		// ... unused descriptors. This is stupid.
		GPU_SetBufferBinding(desc_set, pass->globals_binding, s->globals_buffer);
		GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, s->globals_buffer);
		GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, s->globals_buffer);
		GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
		GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
		GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, s->sampler_percentage_closer);
		GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex0_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex1_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex2_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex3_binding, s->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, s->dummy_black);
		//GPU_SetTextureBinding(desc_set, pass->tex4_binding, s->dummy_black);

		GPU_FinalizeDescriptorSet(desc_set);
		s->lightgrid_sweep_desc_set = desc_set;
	}

	if (r->is_outdated[ASSETS.SHADER_GEOMETRY_PASS]) {
		MainPassLayout* pass = &s->main_pass_layout;
		GPU_WaitUntilIdle();
		for (int i = 0; i < 2; i++) {
			GPU_DestroyGraphicsPipeline(s->geometry_pass_pipeline[i]);

			GPU_Access vs_accesses[] = {
				GPU_Read(pass->globals_binding),
			};

			GPU_Access fs_acceses[] = {
				GPU_Read(pass->globals_binding),
				GPU_Read(pass->tex0_binding),
				GPU_Read(pass->tex1_binding),
				GPU_Read(pass->tex2_binding),
				GPU_Read(pass->tex3_binding),
				GPU_Read(pass->sampler_linear_clamp_binding),
				GPU_Read(pass->sampler_linear_wrap_binding),
			};
			
			GPU_ShaderDesc vs_desc = {};
			vs_desc.accesses = vs_accesses; vs_desc.accesses_count = DS_ArrayCount(vs_accesses);
			GPU_ShaderDesc fs_desc = {};
			fs_desc.accesses = fs_acceses; fs_desc.accesses_count = DS_ArrayCount(fs_acceses);
			LoadVertexAndFragmentShader(TEMP, ASSETS.SHADER_GEOMETRY_PASS, pass->pipeline_layout, &vs_desc, &fs_desc);

			GPU_Format vertex_inputs[] = {
				GPU_Format_RGB32F,
				GPU_Format_RGB32F,
				GPU_Format_RGB32F,
				GPU_Format_RG32F,
			};

			GPU_GraphicsPipelineDesc desc = {};
			desc.layout = pass->pipeline_layout;
			desc.render_pass = s->geometry_render_pass[i];
			desc.vs = vs_desc;
			desc.fs = fs_desc;
			desc.vertex_input_formats = vertex_inputs;
			desc.vertex_input_formats_count = DS_ArrayCount(vertex_inputs);
			desc.enable_depth_test = true;
			desc.enable_depth_write = true;
			desc.cull_mode = GPU_CullMode_DrawCCW;
			s->geometry_pass_pipeline[i] = GPU_MakeGraphicsPipeline(&desc);
		}
	}
	
	if (r->is_outdated[ASSETS.SHADER_LIGHTING_PASS]) {
		LightingPassLayout* pass = &s->lighting_pass_layout;
		GPU_WaitUntilIdle();
		GPU_DestroyGraphicsPipeline(s->lighting_pass_pipeline);
		
		GPU_Access vs_accesses[] = {
			GPU_Read(pass->globals_binding),
		};

		GPU_Access fs_accesses[] = {
			GPU_Read(pass->globals_binding),
			GPU_Read(pass->gbuffer_base_color_binding),
			GPU_Read(pass->gbuffer_normal_binding),
			GPU_Read(pass->gbuffer_orm_binding),
			GPU_Read(pass->gbuffer_emissive_binding),
			GPU_Read(pass->gbuffer_depth_binding),
			GPU_Read(pass->sampler_linear_clamp_binding),
			GPU_Read(pass->sampler_linear_wrap_binding),
			GPU_Read(pass->sampler_nearest_clamp_binding),
			GPU_Read(pass->sampler_percentage_closer),
			GPU_Read(pass->tex_irradiance_map_binding),
			GPU_Read(pass->prefiltered_env_map_binding),
			GPU_Read(pass->brdf_integration_map_binding),
			GPU_Read(pass->lightgrid_binding),
			GPU_Read(pass->prev_frame_result_binding),
			GPU_Read(pass->sun_depth_map_binding),
		};

		GPU_ShaderDesc vs_desc = {};
		vs_desc.accesses = vs_accesses; vs_desc.accesses_count = DS_ArrayCount(vs_accesses);
		
		GPU_ShaderDesc fs_desc = {};
		fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);

		LoadVertexAndFragmentShader(TEMP, ASSETS.SHADER_LIGHTING_PASS, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_GraphicsPipelineDesc desc = {};
		desc.layout = pass->pipeline_layout;
		desc.render_pass = s->lighting_render_pass;
		desc.vs = vs_desc;
		desc.fs = fs_desc;
		s->lighting_pass_pipeline = GPU_MakeGraphicsPipeline(&desc);
	}

	if (r->is_outdated[ASSETS.SHADER_TAA_RESOLVE]) {
		MainPassLayout* pass = &s->main_pass_layout; // let's use the main pass layout for now.
		GPU_WaitUntilIdle();

		for (int i = 0; i < 2; i++) {
			GPU_DestroyGraphicsPipeline(s->taa_resolve_pipeline[i]);
			GPU_DestroyDescriptorSet(s->taa_resolve_descriptor_set[i]);

			GPU_Access fs_accesses[] = {
				GPU_Read(pass->globals_binding),
				GPU_Read(pass->lighting_result_rt),
				GPU_Read(pass->prev_frame_result_binding),
				GPU_Read(pass->gbuffer_depth_binding),
				GPU_Read(pass->gbuffer_depth_prev_binding),
				GPU_Read(pass->gbuffer_velocity_binding),
				GPU_Read(pass->gbuffer_velocity_prev_binding),
				GPU_Read(pass->sampler_linear_clamp_binding),
			};

			GPU_ShaderDesc vs_desc = {};
			GPU_ShaderDesc fs_desc = {};
			fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
			LoadVertexAndFragmentShader(TEMP, ASSETS.SHADER_TAA_RESOLVE, pass->pipeline_layout, &vs_desc, &fs_desc);

			GPU_GraphicsPipelineDesc desc = {};
			desc.layout = pass->pipeline_layout;
			desc.render_pass = s->taa_resolve_render_pass[i];
			desc.vs = vs_desc;
			desc.fs = fs_desc;
			s->taa_resolve_pipeline[i] = GPU_MakeGraphicsPipeline(&desc);
			
			GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
			GPU_SetBufferBinding(desc_set, pass->globals_binding, s->globals_buffer);
			GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
			GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
			GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, s->sampler_percentage_closer);
			GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, s->taa_output_rt[1 - i]);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, s->gbuffer_depth[i]);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, s->gbuffer_depth[1 - i]);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, s->gbuffer_velocity[i]);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, s->gbuffer_velocity[1 - i]);
			GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, s->lighting_result_rt);
			
			// ... unused descriptors. This is stupid.
			GPU_SetStorageImageBinding(desc_set, pass->img0_binding, s->lightgrid, 0);
			//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, s->lightgrid, 0);
			GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, s->globals_buffer);
			GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, s->globals_buffer);
			GPU_SetTextureBinding(desc_set, pass->tex0_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex1_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex2_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex3_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, s->dummy_black);

			GPU_FinalizeDescriptorSet(desc_set);
			s->taa_resolve_descriptor_set[i] = desc_set;
		}
	}

	if (r->is_outdated[ASSETS.SHADER_BLOOM_DOWNSAMPLE]) {
		MainPassLayout* pass = &s->main_pass_layout; // let's use the main pass layout for now.
		GPU_WaitUntilIdle();

		for (uint32_t step = 0; step < BLOOM_PASS_COUNT; step++) {
			
			GPU_Access fs_accesses[] = {GPU_Read(pass->tex0_binding), GPU_Read(pass->sampler_linear_clamp_binding)};
			GPU_ShaderDesc vs_desc = {};
			GPU_ShaderDesc fs_desc = {};
			fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
			LoadVertexAndFragmentShader(TEMP, ASSETS.SHADER_BLOOM_DOWNSAMPLE, pass->pipeline_layout, &vs_desc, &fs_desc);

			for (int i = 0; i < 2; i++) {
				GPU_DestroyGraphicsPipeline(s->bloom_downsamples[step].pipeline[i]);

				// In pure vulkan since we have the concept of framebuffers, we would only need one pipeline here...
				GPU_GraphicsPipelineDesc desc = {};
				desc.layout = pass->pipeline_layout;
				desc.render_pass = s->bloom_downsamples[step].render_pass[i];
				desc.vs = vs_desc;
				desc.fs = fs_desc;
				s->bloom_downsamples[step].pipeline[i] = GPU_MakeGraphicsPipeline(&desc);

				GPU_DestroyDescriptorSet(s->bloom_downsamples[step].desc_set[i]);
				GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);

				if (step == 0) {
					GPU_SetTextureBinding(desc_set, pass->tex0_binding, s->taa_output_rt[i]);
				} else {
					GPU_SetTextureMipBinding(desc_set, pass->tex0_binding, s->bloom_downscale_rt, step-1);
				}

				GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());

				// ... unused descriptors. This is stupid.
				GPU_SetStorageImageBinding(desc_set, pass->img0_binding, s->lightgrid, 0);
				//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, s->lightgrid, 0);
				GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, s->globals_buffer);
				GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, s->globals_buffer);
				GPU_SetBufferBinding(desc_set, pass->globals_binding, s->globals_buffer);
				GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
				GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, s->sampler_percentage_closer);
				GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex1_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex2_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex3_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, s->dummy_black);
				GPU_FinalizeDescriptorSet(desc_set);
				s->bloom_downsamples[step].desc_set[i] = desc_set;
			}
		}
	}

	if (r->is_outdated[ASSETS.SHADER_BLOOM_UPSAMPLE]) {
		MainPassLayout* pass = &s->main_pass_layout; // let's use the main pass layout for now.
		GPU_WaitUntilIdle();
		
		GPU_Access fs_accesses[] = {GPU_Read(pass->tex0_binding), GPU_Read(pass->sampler_linear_clamp_binding)};
		GPU_ShaderDesc vs_desc = {};
		GPU_ShaderDesc fs_desc = {};
		fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
		LoadVertexAndFragmentShader(TEMP, ASSETS.SHADER_BLOOM_UPSAMPLE, pass->pipeline_layout, &vs_desc, &fs_desc);

		for (uint32_t step = 0; step < BLOOM_PASS_COUNT; step++) {
			for (int i = 0; i < 2; i++) {
				GPU_DestroyGraphicsPipeline(s->bloom_upsamples[step].pipeline[i]);
				GPU_GraphicsPipelineDesc desc = {};
				desc.layout = pass->pipeline_layout;
				desc.render_pass = s->bloom_upsamples[step].render_pass[i];
				desc.vs = vs_desc;
				desc.fs = fs_desc;
				desc.enable_blending = true;
				desc.blending_mode_additive = true;
				s->bloom_upsamples[step].pipeline[i] = GPU_MakeGraphicsPipeline(&desc);

				GPU_DestroyDescriptorSet(s->bloom_upsamples[step].desc_set[i]);
				GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
				
				if (step == 0) {
					GPU_SetTextureMipBinding(desc_set, pass->tex0_binding, s->bloom_downscale_rt, BLOOM_PASS_COUNT-1);
				}
				else {
					GPU_SetTextureMipBinding(desc_set, pass->tex0_binding, s->bloom_upscale_rt, BLOOM_PASS_COUNT-step);
				}
				GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());

				// ... unused descriptors. This is stupid.
				GPU_SetStorageImageBinding(desc_set, pass->img0_binding, s->lightgrid, 0);
				//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, s->lightgrid, 0);
				GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, s->globals_buffer);
				GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, s->globals_buffer);
				GPU_SetBufferBinding(desc_set, pass->globals_binding, s->globals_buffer);
				GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
				GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, s->sampler_percentage_closer);
				GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex1_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex2_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex3_binding, s->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, s->dummy_black);
				GPU_FinalizeDescriptorSet(desc_set);
				s->bloom_upsamples[step].desc_set[i] = desc_set;
			}
		}
	}

	if (r->is_outdated[ASSETS.SHADER_FINAL_POST_PROCESS]) {
		MainPassLayout* pass = &s->main_pass_layout; // let's use the main pass layout for now.
		GPU_WaitUntilIdle();

		GPU_DestroyGraphicsPipeline(s->final_post_process_pipeline);

		GPU_Access fs_accesses[] = {GPU_Read(pass->tex0_binding), GPU_Read(pass->sampler_linear_clamp_binding)};
		GPU_ShaderDesc vs_desc = {};
		GPU_ShaderDesc fs_desc = {};
		fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
		LoadVertexAndFragmentShader(TEMP, ASSETS.SHADER_FINAL_POST_PROCESS, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_GraphicsPipelineDesc desc = {};
		desc.layout = pass->pipeline_layout;
		desc.render_pass = s->final_post_process_render_pass;
		desc.vs = vs_desc;
		desc.fs = fs_desc;
		s->final_post_process_pipeline = GPU_MakeGraphicsPipeline(&desc);

		for (int i = 0; i < 2; i++) {
			GPU_DestroyDescriptorSet(s->final_post_process_desc_set[i]);
			GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
			GPU_SetTextureBinding(desc_set, pass->tex0_binding, s->bloom_upscale_rt);
			GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());

			// ... unused descriptors. This is stupid.
			GPU_SetStorageImageBinding(desc_set, pass->img0_binding, s->lightgrid, 0);
			//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, s->lightgrid, 0);
			GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, s->globals_buffer);
			GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, s->globals_buffer);
			GPU_SetBufferBinding(desc_set, pass->globals_binding, s->globals_buffer);
			GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
			GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, s->sampler_percentage_closer);
			GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, s->lighting_result_rt);
			GPU_SetTextureBinding(desc_set, pass->tex1_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex2_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex3_binding, s->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, s->dummy_black);

			GPU_FinalizeDescriptorSet(desc_set);
			s->final_post_process_desc_set[i] = desc_set;
		}
	}

	if (r->is_outdated[ASSETS.SHADER_GEN_IRRADIANCE_MAP]) {
		GenIrradianceMapCtx ctx = {};
		ctx.pipeline_layout = GPU_InitPipelineLayout();
		ctx.sampler_binding = GPU_SamplerBinding(ctx.pipeline_layout, "SAMPLER_LINEAR_CLAMP");
		ctx.tex_env_cube_binding = GPU_TextureBinding(ctx.pipeline_layout, "TEX_ENV_CUBE");
		ctx.output_binding = GPU_StorageImageBinding(ctx.pipeline_layout, "OUTPUT", GPU_Format_RGBA32F);
		GPU_FinalizePipelineLayout(ctx.pipeline_layout);
		
		GPU_Access cs_accesses[] = {
			GPU_Read(ctx.sampler_binding),
			GPU_Read(ctx.tex_env_cube_binding),
			GPU_Write(ctx.output_binding),
		};
		GPU_ShaderDesc cs_desc = {};
		cs_desc.accesses = cs_accesses;
		cs_desc.accesses_count = DS_ArrayCount(cs_accesses);
		ctx.pipeline = LoadComputeShader(ASSETS.SHADER_GEN_IRRADIANCE_MAP, ctx.pipeline_layout, &cs_desc);
		
		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, ctx.pipeline_layout);
		GPU_SetSamplerBinding(desc_set, ctx.sampler_binding, GPU_SamplerLinearClamp());
		GPU_SetTextureBinding(desc_set, ctx.tex_env_cube_binding, s->tex_env_cube);
		GPU_SetStorageImageBinding(desc_set, ctx.output_binding, s->irradiance_map, 0);
		GPU_FinalizeDescriptorSet(desc_set);
		
		GPU_Graph* graph = GPU_MakeGraph(); // TODO: merge all graphs that should execute within this UpdateScene function into one.
		GPU_OpBindComputePipeline(graph, ctx.pipeline);
		GPU_OpBindComputeDescriptorSet(graph, desc_set);

		GPU_OpDispatch(graph, s->irradiance_map->width / 8, s->irradiance_map->height / 8, 1);
		GPU_GraphSubmit(graph);
		GPU_GraphWait(graph);
		GPU_DestroyGraph(graph);
		GPU_DestroyDescriptorSet(desc_set);
	}

	if (r->is_outdated[ASSETS.SHADER_GEN_PREFILTERED_ENV_MAP]) {
		GenPrefilteredEnvMapCtx ctx = {};
		
		ctx.pipeline_layout = GPU_InitPipelineLayout();
		ctx.sampler_binding = GPU_SamplerBinding(ctx.pipeline_layout, "SAMPLER_LINEAR_CLAMP");
		ctx.tex_env_cube_binding = GPU_TextureBinding(ctx.pipeline_layout, "TEX_ENV_CUBE");
		ctx.output_binding = GPU_StorageImageBinding(ctx.pipeline_layout, "OUTPUT", GPU_Format_RGBA32F);
		GPU_FinalizePipelineLayout(ctx.pipeline_layout);
		
		GPU_Access cs_accesses[] = {
			GPU_Read(ctx.sampler_binding),
			GPU_Read(ctx.tex_env_cube_binding),
			GPU_Write(ctx.output_binding),
		};
		GPU_ShaderDesc cs_desc = {};
		cs_desc.accesses = cs_accesses;
		cs_desc.accesses_count = DS_ArrayCount(cs_accesses);
		ctx.pipeline = LoadComputeShader(ASSETS.SHADER_GEN_PREFILTERED_ENV_MAP, ctx.pipeline_layout, &cs_desc);
		
		GPU_Graph* graph = GPU_MakeGraph();
		GPU_OpBindComputePipeline(graph, ctx.pipeline);

		GPU_DescriptorArena* descriptor_arena = GPU_MakeDescriptorArena();

		uint32_t size = s->tex_specular_env_map->width;
		for (uint32_t i = 0; i < s->tex_specular_env_map->mip_level_count; i++) {
			if (size < 16) break; // stop at 16x16

			GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(descriptor_arena, ctx.pipeline_layout);
			GPU_SetSamplerBinding(desc_set, ctx.sampler_binding, GPU_SamplerLinearClamp());
			GPU_SetTextureBinding(desc_set, ctx.tex_env_cube_binding, s->tex_env_cube);
			GPU_SetStorageImageBinding(desc_set, ctx.output_binding, s->tex_specular_env_map, i);
			GPU_FinalizeDescriptorSet(desc_set);

			GPU_OpBindComputePipeline(graph, ctx.pipeline);
			GPU_OpBindComputeDescriptorSet(graph, desc_set);
			GPU_OpPushComputeConstants(graph, ctx.pipeline_layout, &i, sizeof(i));

			GPU_OpDispatch(graph, size / 8, size / 8, 1);
			size /= 2;
		}
		
		GPU_GraphSubmit(graph);
		GPU_GraphWait(graph);
		GPU_DestroyGraph(graph);
		GPU_DestroyDescriptorArena(descriptor_arena);
	}
	
	if (r->is_outdated[ASSETS.SHADER_GEN_BRDF_INTEGRATION_MAP]) {
		GenBRDFintegrationMapCtx ctx = {};
		
		ctx.pipeline_layout = GPU_InitPipelineLayout();
		ctx.output_binding = GPU_StorageImageBinding(ctx.pipeline_layout, "OUTPUT", GPU_Format_RG16F);
		GPU_FinalizePipelineLayout(ctx.pipeline_layout);
		
		GPU_Access cs_accesses[] = {GPU_Write(ctx.output_binding)};
		GPU_ShaderDesc cs_desc = {};
		cs_desc.accesses = cs_accesses; cs_desc.accesses_count = DS_ArrayCount(cs_accesses);
		ctx.pipeline = LoadComputeShader(ASSETS.SHADER_GEN_BRDF_INTEGRATION_MAP, ctx.pipeline_layout, &cs_desc);

		GPU_Graph* graph = GPU_MakeGraph();
		
		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, ctx.pipeline_layout);
		GPU_SetStorageImageBinding(desc_set, ctx.output_binding, s->brdf_lut, 0);
		GPU_FinalizeDescriptorSet(desc_set);

		GPU_OpBindComputePipeline(graph, ctx.pipeline);
		GPU_OpBindComputeDescriptorSet(graph, desc_set);

		GPU_OpDispatch(graph, 256 / 8, 256 / 8, 1);

		GPU_GraphSubmit(graph);
		GPU_GraphWait(graph);
		GPU_DestroyGraph(graph);
		GPU_DestroyDescriptorSet(desc_set);
	}

	if (r->is_outdated[ASSETS.MESH_WORLD]) {
		assert(first);
		bool ok = ReloadMesh(persistent_arena, s, &s->ro_world, ASSET_PATHS[ASSETS.MESH_WORLD]);
		assert(ok);
	}

	if (r->is_outdated[ASSETS.MESH_SKYBOX]) {
		assert(ReloadMesh(persistent_arena, s, &s->ro_skybox, ASSET_PATHS[ASSETS.MESH_SKYBOX]));
	}

	// if (update_desc_set) {
	// 	MainPassLayout* pass = &s->main_pass_layout;
	// 	GPU_DestroyDescriptorSet(s->desc_set);
	// 	s->desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
	// 	GPU_SetBufferBinding(s->desc_set, pass->globals_binding, s->globals_buffer);
	// 	GPU_SetSamplerBinding(s->desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
	// 	GPU_SetSamplerBinding(s->desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
	// 	// GPU_SetTextureBinding(s->desc_set, pass->tex0_binding, tex_base_color);
	// 	// GPU_SetTextureBinding(s->desc_set, pass->tex_x_rough_metal_binding, tex_x_rough_metal);
	// 	// GPU_SetTextureBinding(s->desc_set, pass->tex1_binding, tex_normal);
	// 	GPU_SetTextureBinding(s->desc_set, pass->tex_env_cube_binding, tex_env_cube);
	// 	GPU_SetTextureBinding(s->desc_set, pass->tex_irradiance_map_binding, s->irradiance_map);
	// 	GPU_SetTextureBinding(s->desc_set, pass->prefiltered_env_map_binding, s->tex_specular_env_map);
	// 	GPU_SetTextureBinding(s->desc_set, pass->brdf_integration_map_binding, s->brdf_lut);
	// 	GPU_FinalizeDescriptorSet(s->desc_set);
	// }

	// Finally, tell the hotreloader that we have checked everything
	for (int i = 0; i < DS_ArrayCount(r->is_outdated); i++) r->is_outdated[i] = false;

	DS_ArenaSetMark(TEMP, T);
	first = false;
}

static HMM_Vec3 Rotate(HMM_Vec3 v, HMM_Vec3 n, float theta) {
	// For derivation, see "3D Rotation about an Arbitrary Axis" from the book "3D Math Primer for Graphics and Game Development" (F. Dunn).
	HMM_Vec3 result = HMM_MulV3F(HMM_SubV3(v, HMM_MulV3F(n, HMM_DotV3(v, n))), cosf(theta));
	result = HMM_AddV3(result, HMM_MulV3F(HMM_Cross(n, v), sinf(theta)));
	result = HMM_AddV3(result, HMM_MulV3F(n, HMM_DotV3(v, n)));
	return result;
}

static uint64_t RandomU64(uint64_t* seed) {
	// https://gist.github.com/Leandros/6dc334c22db135b033b57e9ee0311553
	uint64_t z = (*seed += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return (z ^ (z >> 31)) >> 31;
}

static float RandomFloat(uint64_t* seed, float min, float max) {
	return min + (max - min) * (float)((double)RandomU64(seed) / (double)0xFFFFFFFFFFFFFFFFLLU);
}

static HMM_Vec2 r2_sequence(float n) {
	HMM_Vec2 v = HMM_Vec2{0.7548776662466927f, 0.5698402909980532f} * n;
	return {fmodf(v.X, 1.f), fmodf(v.Y, 1.f)};
}

// static float halton(int base, int index) {
// 	float result = 0.f;
// 	float f = 1.f;
// 	while (index > 0) {
// 		f = f / (float)base;
// 		result += f * (float)(index % base);
// 		index /= base; 
// 	}
// 	return result;
// }

// int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
int main() {
	// TODO: Add shader and asset hot reloading! That would make experimentation a lot easier.
	// We should turn this into mesh_render_pbr_simple, and then have a mesh_render_pbr_advanced. Maybe I shouldn't open source the advanced version.
	
	DS_Arena temp_arena;
	DS_ArenaInit(&temp_arena, 4096, DS_HEAP);
	TEMP = &temp_arena;
	
	DS_Arena persistent_arena;
	DS_ArenaInit(&persistent_arena, 4096, DS_HEAP);

	OS_WINDOW window = OS_WINDOW_Create(window_w, window_h, "PBR Renderer");
	OS_WINDOW_SetFullscreen(&window, true);

	GPU_Init(window.handle);

	// Init asset index table
	for (int i = 0; i < DS_ArrayCount(ASSET_PATHS); i++) ((AssetIndex*)&ASSETS)[i] = i;
	
	Scene scene = {};
	Scene* s = &scene;
	
	Hotreloader hotreloader = {};
	Hotreloader* r = &hotreloader;
	for (int i = 0; i < DS_ArrayCount(r->is_outdated); i++) r->is_outdated[i] = true;

	Camera camera = {};
	camera.pos = {0.f, 5.f, 5.f};
	
	struct {
		HMM_Mat4 clip_space_from_world;
		HMM_Mat4 clip_space_from_view;
		HMM_Mat4 world_space_from_clip;
		HMM_Mat4 view_space_from_clip;
		HMM_Mat4 view_space_from_world;
		HMM_Mat4 world_space_from_view;
		HMM_Mat4 sun_space_from_world;
		HMM_Mat4 old_clip_space_from_world;
		HMM_Vec4 sun_direction;
		HMM_Vec3 camera_pos;
		float frame_idx_mod_59;
		float lightgrid_scale;
		float shift_is_held_down;
	} globals;
	
	const float sun_half_size = 40.f;
	float sun_pitch = 56.5f;
	float sun_yaw = 97.f;
	
	// Init scene
	{
		GPU_SamplerDesc sampler_pcf_desc = {};
		sampler_pcf_desc.min_filter = GPU_Filter_Linear;
		sampler_pcf_desc.mag_filter = GPU_Filter_Linear;
		sampler_pcf_desc.mipmap_mode = GPU_Filter_Linear;
		sampler_pcf_desc.address_modes[0] = GPU_AddressMode_Clamp;
		sampler_pcf_desc.address_modes[1] = GPU_AddressMode_Clamp;
		sampler_pcf_desc.address_modes[2] = GPU_AddressMode_Clamp;
		sampler_pcf_desc.max_lod = 1000.f;
		sampler_pcf_desc.compare_op = GPU_CompareOp_Less;
		s->sampler_percentage_closer = GPU_MakeSampler(&sampler_pcf_desc);

		s->globals_buffer = GPU_MakeBuffer(sizeof(globals), GPU_BufferFlag_CPU|GPU_BufferFlag_GPU|GPU_BufferFlag_StorageBuffer, NULL);
		
		s->sun_depth_rt       = GPU_MakeTexture(GPU_Format_D32F_Or_X8D24UN, 2048, 2048, 1, GPU_TextureFlag_RenderTarget, NULL);
		s->lightgrid          = GPU_MakeTexture(GPU_Format_RGBA16F, LIGHTGRID_SIZE, LIGHTGRID_SIZE, LIGHTGRID_SIZE, GPU_TextureFlag_StorageImage, NULL);

		s->gbuffer_base_color = GPU_MakeTexture(GPU_Format_RGBA8UN, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);
		s->gbuffer_normal     = GPU_MakeTexture(GPU_Format_RGBA8UN, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);
		s->gbuffer_orm        = GPU_MakeTexture(GPU_Format_RGBA8UN, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);
		s->gbuffer_emissive   = GPU_MakeTexture(GPU_Format_RGBA8UN, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);

		// We keep track of the previous frame's velocity buffer for depth/location based rejection
		s->gbuffer_depth[0]   = GPU_MakeTexture(GPU_Format_D32F_Or_X8D24UN, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);
		s->gbuffer_depth[1]   = s->gbuffer_depth[0]; // for now, lets not do depth based rejection. // GPU_MakeTexture(GPU_Format_D32F_Or_X8D24UN, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);
		
		// We keep track of the previous frame's velocity buffer for velocity-based rejection
		s->gbuffer_velocity[0] = GPU_MakeTexture(GPU_Format_RG16F, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);
		s->gbuffer_velocity[1] = GPU_MakeTexture(GPU_Format_RG16F, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);

		s->lighting_result_rt = GPU_MakeTexture(GPU_Format_RGBA16F, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);

		// in the TAA resolve, we need to be writing to one resulting RT and read from the previous frame's result, so we need two RTs.
		s->taa_output_rt[0] = GPU_MakeTexture(GPU_Format_RGBA16F, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);
		s->taa_output_rt[1] = GPU_MakeTexture(GPU_Format_RGBA16F, window_w, window_h, 1, GPU_TextureFlag_RenderTarget, NULL);

		for (int i = 0; i < 2; i++) {
			GPU_TextureView geometry_color_targets[] = {{s->gbuffer_base_color}, {s->gbuffer_normal}, {s->gbuffer_orm}, {s->gbuffer_emissive}, {s->gbuffer_velocity[i]}};
			
			GPU_RenderPassDesc desc = {};
			desc.width = window_w;
			desc.height = window_h;
			desc.color_targets = geometry_color_targets;
			desc.color_targets_count = DS_ArrayCount(geometry_color_targets);
			desc.depth_stencil_target = s->gbuffer_depth[i];
			s->geometry_render_pass[i] = GPU_MakeRenderPass(&desc);
		}
		
		GPU_RenderPassDesc lightgrid_voxelize_pass_desc = {};
		lightgrid_voxelize_pass_desc.width = LIGHTGRID_SIZE;
		lightgrid_voxelize_pass_desc.height = LIGHTGRID_SIZE;
		s->lightgrid_voxelize_render_pass = GPU_MakeRenderPass(&lightgrid_voxelize_pass_desc);

		GPU_TextureView lighting_color_targets[] = {{s->lighting_result_rt}};
		
		GPU_RenderPassDesc lighting_pass_desc = {};
		lighting_pass_desc.width = window_w;
		lighting_pass_desc.height = window_h;
		lighting_pass_desc.color_targets = lighting_color_targets;
		lighting_pass_desc.color_targets_count = DS_ArrayCount(lighting_color_targets);
		s->lighting_render_pass = GPU_MakeRenderPass(&lighting_pass_desc);
		
		GPU_RenderPassDesc sun_render_pass_desc = {};
		sun_render_pass_desc.width = s->sun_depth_rt->width;
		sun_render_pass_desc.height = s->sun_depth_rt->height;
		sun_render_pass_desc.depth_stencil_target = s->sun_depth_rt;
		s->sun_depth_render_pass = GPU_MakeRenderPass(&sun_render_pass_desc);

		for (int i = 0; i < 2; i++) {
			GPU_TextureView taa_resolve_color_targets[] = {s->taa_output_rt[i]};
			
			GPU_RenderPassDesc resolve_pass_desc = {};
			resolve_pass_desc.width = window_w;
			resolve_pass_desc.height = window_h;
			resolve_pass_desc.color_targets = taa_resolve_color_targets;
			resolve_pass_desc.color_targets_count = DS_ArrayCount(taa_resolve_color_targets);
			s->taa_resolve_render_pass[i] = GPU_MakeRenderPass(&resolve_pass_desc);
		}

		s->bloom_downscale_rt = GPU_MakeTexture(GPU_Format_RGBA16F, window_w/2, window_h/2, 1,
			GPU_TextureFlag_RenderTarget|GPU_TextureFlag_HasMipmaps|GPU_TextureFlag_PerMipBinding, NULL);

		s->bloom_upscale_rt = GPU_MakeTexture(GPU_Format_RGBA16F, window_w, window_h, 1,
			GPU_TextureFlag_RenderTarget|GPU_TextureFlag_HasMipmaps|GPU_TextureFlag_PerMipBinding, NULL);
		
		for (int i = 0; i < 2; i++) {
			uint32_t width = window_w, height = window_h;

			for (int step = 0; step < BLOOM_PASS_COUNT; step++) {
				GPU_TextureView bloom_downsample_color_targets[] = {{s->bloom_downscale_rt, (uint32_t)step}};
				
				width /= 2;
				height /= 2;

				GPU_RenderPassDesc pass_desc = {};
				pass_desc.width = width;
				pass_desc.height = height;
				pass_desc.color_targets = bloom_downsample_color_targets;
				pass_desc.color_targets_count = DS_ArrayCount(bloom_downsample_color_targets);
				s->bloom_downsamples[step].render_pass[i] = GPU_MakeRenderPass(&pass_desc);
			}

			width = window_w, height = window_h;
			for (int step = BLOOM_PASS_COUNT - 1; step >= 0; step--) {
				uint32_t dst_level = BLOOM_PASS_COUNT - 1 - (uint32_t)step;
				GPU_TextureView bloom_upsample_color_targets[] = {{s->bloom_upscale_rt, dst_level}};

				GPU_RenderPassDesc pass_desc = {};
				pass_desc.width = width;
				pass_desc.height = height;
				pass_desc.color_targets = bloom_upsample_color_targets;
				pass_desc.color_targets_count = DS_ArrayCount(bloom_upsample_color_targets);
				s->bloom_upsamples[step].render_pass[i] = GPU_MakeRenderPass(&pass_desc);
				
				width /= 2;
				height /= 2;
			}
		}

		GPU_RenderPassDesc final_pp_pass_desc = {};
		final_pp_pass_desc.color_targets = GPU_SWAPCHAIN_COLOR_TARGET;
		final_pp_pass_desc.color_targets_count = 1;
		s->final_post_process_render_pass = GPU_MakeRenderPass(&final_pp_pass_desc);

		uint32_t normal_up = 0xFFFF7F7F;
		uint32_t black = 0x00000000;
		uint32_t white = 0xFFFFFFFF;

		s->dummy_normal_map = GPU_MakeTexture(GPU_Format_RGBA8UN, 1, 1, 1, 0, &normal_up);
		s->dummy_black = GPU_MakeTexture(GPU_Format_RGBA8UN, 1, 1, 1, 0, &black);
		s->dummy_white = GPU_MakeTexture(GPU_Format_RGBA8UN, 1, 1, 1, 0, &white);
		s->irradiance_map = GPU_MakeTexture(GPU_Format_RGBA32F, 32, 32, 1, GPU_TextureFlag_Cubemap|GPU_TextureFlag_StorageImage, NULL);
		s->brdf_lut = GPU_MakeTexture(GPU_Format_RG16F, 256, 256, 1, GPU_TextureFlag_StorageImage, NULL);
		s->tex_specular_env_map = GPU_MakeTexture(GPU_Format_RGBA32F, 256, 256, 1, GPU_TextureFlag_Cubemap|GPU_TextureFlag_HasMipmaps|GPU_TextureFlag_StorageImage, NULL);

		{
			MainPassLayout* lo = &s->main_pass_layout;
			lo->pipeline_layout = GPU_InitPipelineLayout();
			lo->globals_binding = GPU_BufferBinding(lo->pipeline_layout, "GLOBALS");
			lo->tex0_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX0");
			lo->tex1_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX1");
			lo->tex2_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX_ORM");
			lo->tex3_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX_EMISSIVE");
			lo->sun_depth_map_binding_ = GPU_TextureBinding(lo->pipeline_layout, "SUN_DEPTH_MAP");
			
			lo->sampler_linear_clamp_binding = GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_LINEAR_CLAMP");
			lo->sampler_linear_wrap_binding = GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_LINEAR_WRAP");
			lo->sampler_percentage_closer = GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_PERCENTAGE_CLOSER");
			
			lo->ssbo0_binding = GPU_BufferBinding(lo->pipeline_layout, "SSBO0");
			lo->ssbo1_binding = GPU_BufferBinding(lo->pipeline_layout, "SSBO1");
			
			// lo->img0_binding_uint = GPU_StorageImageBinding(lo->pipeline_layout, "IMG0_UINT", GPU_Format_R64I);
			lo->img0_binding = GPU_StorageImageBinding(lo->pipeline_layout, "IMG0", s->lightgrid->format);
			
			// TAA resolve stuff
			lo->prev_frame_result_binding = GPU_TextureBinding(lo->pipeline_layout, "PREV_FRAME_RESULT");
			lo->gbuffer_depth_binding = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_DEPTH");
			lo->gbuffer_depth_prev_binding = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_DEPTH_PREV");
			lo->gbuffer_velocity_binding = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_VELOCITY");
			lo->gbuffer_velocity_prev_binding = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_VELOCITY_PREV");
			lo->lighting_result_rt = GPU_TextureBinding(lo->pipeline_layout, "LIGHTING_RESULT");
			
			GPU_FinalizePipelineLayout(lo->pipeline_layout);
		}

		{
			LightingPassLayout* lo = &s->lighting_pass_layout;
			lo->pipeline_layout = GPU_InitPipelineLayout();
			lo->globals_binding              = GPU_BufferBinding(lo->pipeline_layout, "GLOBALS");
			lo->gbuffer_base_color_binding   = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_BASE_COLOR");
			lo->gbuffer_normal_binding       = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_NORMAL");
			lo->gbuffer_orm_binding          = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_ORM");
			lo->gbuffer_emissive_binding     = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_EMISSIVE");
			lo->gbuffer_depth_binding        = GPU_TextureBinding(lo->pipeline_layout, "GBUFFER_DEPTH");
			lo->tex_irradiance_map_binding   = GPU_TextureBinding(lo->pipeline_layout, "TEX_IRRADIANCE_MAP");
			lo->prefiltered_env_map_binding  = GPU_TextureBinding(lo->pipeline_layout, "PREFILTERED_ENV_MAP");
			lo->brdf_integration_map_binding = GPU_TextureBinding(lo->pipeline_layout, "BRDF_INTEGRATION_MAP");
			lo->lightgrid_binding            = GPU_TextureBinding(lo->pipeline_layout, "LIGHTGRID");
			lo->prev_frame_result_binding    = GPU_TextureBinding(lo->pipeline_layout, "PREV_FRAME_RESULT");
			lo->sun_depth_map_binding        = GPU_TextureBinding(lo->pipeline_layout, "SUN_DEPTH_MAP");
			lo->sampler_linear_clamp_binding = GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_LINEAR_CLAMP");
			lo->sampler_linear_wrap_binding  = GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_LINEAR_WRAP");
			lo->sampler_nearest_clamp_binding= GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_NEAREST_CLAMP");
			lo->sampler_percentage_closer    = GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_PERCENTAGE_CLOSER");
			GPU_FinalizePipelineLayout(lo->pipeline_layout);
			
			for (int i = 0; i < 2; i++) {
				GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, lo->pipeline_layout);
				GPU_SetBufferBinding(desc_set, lo->globals_binding, s->globals_buffer);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_base_color_binding, s->gbuffer_base_color);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_normal_binding, s->gbuffer_normal);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_orm_binding, s->gbuffer_orm);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_emissive_binding, s->gbuffer_emissive);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_depth_binding, s->gbuffer_depth[i]);
				GPU_SetTextureBinding(desc_set, lo->tex_irradiance_map_binding, s->irradiance_map);
				GPU_SetTextureBinding(desc_set, lo->prefiltered_env_map_binding, s->tex_specular_env_map);
				GPU_SetTextureBinding(desc_set, lo->brdf_integration_map_binding, s->brdf_lut);
				GPU_SetTextureBinding(desc_set, lo->lightgrid_binding, s->lightgrid);
				GPU_SetTextureBinding(desc_set, lo->prev_frame_result_binding, s->bloom_downscale_rt);
				GPU_SetTextureBinding(desc_set, lo->sun_depth_map_binding, s->sun_depth_rt);
				GPU_SetSamplerBinding(desc_set, lo->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
				GPU_SetSamplerBinding(desc_set, lo->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
				GPU_SetSamplerBinding(desc_set, lo->sampler_nearest_clamp_binding, GPU_SamplerNearestClamp());
				GPU_SetSamplerBinding(desc_set, lo->sampler_percentage_closer, s->sampler_percentage_closer);
				GPU_FinalizeDescriptorSet(desc_set);
				s->lighting_pass_descriptor_set[i] = desc_set;
			}
		}
	}

	float time = 0.f;

	GPU_Graph* graphs[2];
	GPU_MakeSwapchainGraphs(2, &graphs[0]);
	int graph_idx = 0;
	uint32_t frame_idx = 0;
	uint32_t sweep_direction = 0;
	
	HMM_Vec2 taa_jitter_prev = {};
	bool revoxelize = true;
	
	Input_Frame input_frame = {};

	for (;;) {
		DS_ArenaReset(&temp_arena);

		// poll inputs
		{
			Input_OS_State input_os_state;
			Input_OS_BeginEvents(&input_os_state, &input_frame, &temp_arena);

			OS_WINDOW_Event event;
			while (OS_WINDOW_PollEvent(&window, &event, NULL, NULL)) {
				Input_OS_AddEvent(&input_os_state, &event);
			}

			Input_OS_EndEvents(&input_os_state);

			if (OS_WINDOW_ShouldClose(&window)) break;
		}

		if (Input_IsDown(&input_frame, Input_Key_9)) { sun_pitch -= 0.5f; revoxelize = true; }
		if (Input_IsDown(&input_frame, Input_Key_0)) { sun_pitch += 0.5f; revoxelize = true; }
		if (Input_IsDown(&input_frame, Input_Key_8)) { sun_yaw -= 0.5f; revoxelize = true; }
		if (Input_IsDown(&input_frame, Input_Key_7)) { sun_yaw += 0.5f; revoxelize = true; }
		
		UpdateHotreloader(r);
		UpdateScene(&persistent_arena, TEMP, r, s);

		HMM_Mat4 old_clip_space_from_world = camera.clip_from_world;

		float movement_speed = 0.05f;
		float mouse_speed = 0.001f;
		float FOV = 75.f;
		Camera_Update(&camera, &input_frame, movement_speed, mouse_speed, FOV, (float)window_w / (float)window_h, Z_NEAR, Z_FAR);

		HMM_Mat4 sun_ori = HMM_Rotate_RH(HMM_AngleDeg(sun_pitch), HMM_V3(cosf(HMM_AngleDeg(sun_yaw)), sinf(HMM_AngleDeg(sun_yaw)), 0.f));
		HMM_Vec3 sun_dir = HMM_MulM4V4(sun_ori, HMM_V4(0, 0, -1, 0)).XYZ;

		HMM_Mat4 sun_space_from_world = HMM_InvGeneralM4(sun_ori);
		sun_space_from_world = HMM_MulM4(HMM_Orthographic_RH_ZO(-sun_half_size, sun_half_size, -sun_half_size, sun_half_size, -sun_half_size, sun_half_size), sun_space_from_world);
		// sun_space_from_world = HMM_MulM4(HMM_Translate(HMM_V3(sun_jitter.X, sun_jitter.Y, 0)), sun_space_from_world);

		HMM_Vec4 test = HMM_MulM4V4(sun_space_from_world, HMM_V4(-0.03431f, -0.238f, -0.10725f, 1));
		
		HMM_Vec2 taa_jitter = r2_sequence((float)frame_idx);
		// if (FOS_IsPressed(&window.inputs, FOS_Input_R)) taa_jitter = r2_sequence((float)frame_idx);
		taa_jitter.X = (taa_jitter.X * 2.f - 1.f) / (float)window_w;
		taa_jitter.Y = (taa_jitter.Y * 2.f - 1.f) / (float)window_h;

		globals.clip_space_from_world = camera.clip_from_world;
		globals.clip_space_from_view = camera.clip_from_view;
		globals.world_space_from_clip = camera.world_from_clip;
		globals.view_space_from_clip = camera.view_from_clip;
		globals.view_space_from_world = camera.view_from_world;
		globals.world_space_from_view = camera.world_from_view;
		globals.sun_space_from_world = sun_space_from_world;
		globals.old_clip_space_from_world = frame_idx == 0 ? camera.clip_from_world : old_clip_space_from_world;
		globals.sun_direction.XYZ = sun_dir;
		globals.camera_pos = camera.lazy_pos;
		globals.frame_idx_mod_59 = (float)(frame_idx % 59);
		globals.lightgrid_scale = 1.f / LIGHTGRID_EXTENT_WS;
		globals.shift_is_held_down = (float)Input_IsDown(&input_frame, Input_Key_Alt);
		memcpy(s->globals_buffer->data, &globals, sizeof(globals));
		
		graph_idx = (graph_idx + 1) % 2;
		GPU_Graph* graph = graphs[graph_idx];
		GPU_GraphWait(graph);

		GPU_Texture* backbuffer = GPU_GetBackbuffer(graph);
		if (backbuffer) {
			
			// GPU_OpClearColorF(graph, gbuffer_base_color, 0.1f, 0.2f, 0.5f, 1.f);
			GPU_OpClearDepthStencil(graph, s->gbuffer_depth[frame_idx % 2], GPU_MIP_LEVEL_ALL);

			//  -- Sun depth renderpass ------------------

			GPU_OpClearDepthStencil(graph, s->sun_depth_rt, GPU_MIP_LEVEL_ALL);

			GPU_OpPrepareRenderPass(graph, s->sun_depth_render_pass);

			DS_DynArray(uint32_t) sun_depth_pass_part_draw_params = {TEMP};
			for (int i = 0; i < s->ro_world.parts.count; i++) {
				RenderObjectPart* part = &s->ro_world.parts[i];
				
				uint32_t draw_params = GPU_OpPrepareDrawParams(graph, s->sun_depth_pipeline, part->descriptor_set);
				DS_ArrPush(&sun_depth_pass_part_draw_params, draw_params);
			}

			GPU_OpBeginRenderPass(graph);

			GPU_OpBindVertexBuffer(graph, s->ro_world.vertex_buffer);
			GPU_OpBindIndexBuffer(graph, s->ro_world.index_buffer);
			// GPU_OpPushGraphicsConstants(graph, &time, sizeof(time));

			for (int i = 0; i < s->ro_world.parts.count; i++) {
				RenderObjectPart* part = &s->ro_world.parts[i];
				GPU_OpBindDrawParams(graph, sun_depth_pass_part_draw_params[i]);
				GPU_OpDrawIndexed(graph, part->index_count, 1, part->first_index, 0, 0);
			}

			GPU_OpEndRenderPass(graph);

			// -- Voxelize pass -------------------------
			
			if (revoxelize) {
				revoxelize = false;

				if (frame_idx == 0) {
					GPU_OpClearColorF(graph, s->lightgrid, GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);

					// clear the initial feedback framebuffers
					GPU_OpClearDepthStencil(graph, s->gbuffer_depth[0], GPU_MIP_LEVEL_ALL);
					GPU_OpClearDepthStencil(graph, s->gbuffer_depth[1], GPU_MIP_LEVEL_ALL);
					GPU_OpClearColorF(graph, s->gbuffer_velocity[0], GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
					GPU_OpClearColorF(graph, s->gbuffer_velocity[1], GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
					GPU_OpClearColorF(graph, s->taa_output_rt[0], GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
					GPU_OpClearColorF(graph, s->taa_output_rt[1], GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
				}

				GPU_OpPrepareRenderPass(graph, s->lightgrid_voxelize_render_pass);
				
				DS_DynArray(uint32_t) voxelize_pass_part_draw_paramss = {TEMP};
				for (int i = 0; i < s->ro_world.parts.count; i++) {
					RenderObjectPart* part = &s->ro_world.parts[i];
					uint32_t draw_params = GPU_OpPrepareDrawParams(graph, s->lightgrid_voxelize_pipeline, part->descriptor_set);
					DS_ArrPush(&voxelize_pass_part_draw_paramss, draw_params);
				}

				GPU_OpBeginRenderPass(graph);
			
				for (int i = 0; i < s->ro_world.parts.count; i++) {
					RenderObjectPart* part = &s->ro_world.parts[i];
					GPU_OpBindDrawParams(graph, voxelize_pass_part_draw_paramss[i]);
					GPU_OpDraw(graph, part->index_count, 1, part->first_index, 0);
				}

				GPU_OpEndRenderPass(graph);
			}

			//  -- Light grid sweep pass -----------------
			
			// So, let's spread the illumination. We need a cubemap for the sky, but for now lets say its all blue.
			// Next it'd be interesting to benchmark the timing of this dispatch, and compare the speeds when doing it in X vs Y vs Z.
			
			sweep_direction++;
			if (sweep_direction == 3) sweep_direction = 0;
			
			//FOS_Print("sweep_direction ~u32\n", sweep_direction);
			GPU_OpBindComputePipeline(graph, s->lightgrid_sweep_pipeline);
			GPU_OpBindComputeDescriptorSet(graph, s->lightgrid_sweep_desc_set);
			GPU_OpPushComputeConstants(graph, s->main_pass_layout.pipeline_layout, &sweep_direction, sizeof(sweep_direction));
			GPU_OpDispatch(graph, 1, 16, 16); // 1*1, 16*8, 16*8 = (1, 128, 128)

			// -- Geometry pass -------------------------
			
			GPU_OpPrepareRenderPass(graph, s->geometry_render_pass[frame_idx % 2]);
			
			DS_DynArray(uint32_t) geometry_pass_part_draw_params = {TEMP};
			for (int i = 0; i < s->ro_world.parts.count; i++) {
				RenderObjectPart* part = &s->ro_world.parts[i];
				uint32_t draw_params = GPU_OpPrepareDrawParams(graph, s->geometry_pass_pipeline[frame_idx % 2], part->descriptor_set);
				DS_ArrPush(&geometry_pass_part_draw_params, draw_params);
			}

			GPU_OpBeginRenderPass(graph);

			{
				GPU_OpBindVertexBuffer(graph, s->ro_world.vertex_buffer);
				GPU_OpBindIndexBuffer(graph, s->ro_world.index_buffer);
				
				struct {
					HMM_Vec2 taa_jitter;
					HMM_Vec2 taa_jitter_prev;
				} pc = {
					taa_jitter,
					taa_jitter_prev,
				};
				GPU_OpPushGraphicsConstants(graph, s->main_pass_layout.pipeline_layout, &pc, sizeof(pc));

				for (int i = 0; i < s->ro_world.parts.count; i++) {
					RenderObjectPart* part = &s->ro_world.parts[i];
					GPU_OpBindDrawParams(graph, geometry_pass_part_draw_params[i]);
					GPU_OpDrawIndexed(graph, part->index_count, 1, part->first_index, 0, 0);
				}
			}
			
			// Draw skybox
			{
				GPU_OpBindVertexBuffer(graph, s->ro_skybox.vertex_buffer);
				GPU_OpBindIndexBuffer(graph, s->ro_skybox.index_buffer);
				
				for (int i = 0; i < s->ro_skybox.parts.count; i++) {
					RenderObjectPart* part = &s->ro_skybox.parts[i];
					GPU_OpBindDrawParams(graph, geometry_pass_part_draw_params[i]);
					GPU_OpDrawIndexed(graph, part->index_count, 1, part->first_index, 0, 0);
				}
			}

			GPU_OpEndRenderPass(graph);

			// -- Lighting pass -------------------------

			GPU_OpPrepareRenderPass(graph, s->lighting_render_pass);
			uint32_t lighting_pass_draw_params = GPU_OpPrepareDrawParams(graph, s->lighting_pass_pipeline, s->lighting_pass_descriptor_set[frame_idx % 2]);
			GPU_OpBeginRenderPass(graph);
			
			GPU_OpBindDrawParams(graph, lighting_pass_draw_params);
			// GPU_OpPushGraphicsConstants(graph, s->lighting_pass_layout.pipeline_layout, &sun_jitter, sizeof(sun_jitter));
			GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

			GPU_OpEndRenderPass(graph);
			
			// -- TAA resolve pass -------------------------

			GPU_OpPrepareRenderPass(graph, s->taa_resolve_render_pass[frame_idx % 2]);
			uint32_t taa_resolve_pass_draw_params = GPU_OpPrepareDrawParams(graph, s->taa_resolve_pipeline[frame_idx % 2], s->taa_resolve_descriptor_set[frame_idx % 2]);
			GPU_OpBeginRenderPass(graph);

			GPU_OpBindDrawParams(graph, taa_resolve_pass_draw_params);
			GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

			GPU_OpEndRenderPass(graph);

			// -- bloom downsample -------------------------
			
			for (uint32_t step = 0; step < BLOOM_PASS_COUNT; step++) {
				GPU_OpPrepareRenderPass(graph, s->bloom_downsamples[step].render_pass[frame_idx % 2]);
				uint32_t draw_params = GPU_OpPrepareDrawParams(graph, s->bloom_downsamples[step].pipeline[frame_idx % 2], s->bloom_downsamples[step].desc_set[frame_idx % 2]);
				GPU_OpBeginRenderPass(graph);
				
				uint32_t dst_mip_level = step + 1;
				GPU_OpPushGraphicsConstants(graph, s->main_pass_layout.pipeline_layout, &dst_mip_level, sizeof(dst_mip_level));
				GPU_OpBindDrawParams(graph, draw_params);
				GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle
				
				GPU_OpEndRenderPass(graph);
			}

			// -- bloom upsample ---------------------------
			
			// this is pretty dumb...
			GPU_OpClearColorF(graph, s->bloom_upscale_rt, GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
			
			GPU_OpBlitInfo blit = {};
			blit.src_area[1] = {window_w, window_h, 1};
			blit.dst_area[1] = {window_w, window_h, 1};
			blit.src_texture = s->taa_output_rt[frame_idx % 2];
			blit.dst_texture = s->bloom_upscale_rt;
			GPU_OpBlit(graph, &blit);

			for (uint32_t step = 0; step < BLOOM_PASS_COUNT; step++) {
				GPU_OpPrepareRenderPass(graph, s->bloom_upsamples[step].render_pass[frame_idx % 2]);
				uint32_t draw_params = GPU_OpPrepareDrawParams(graph, s->bloom_upsamples[step].pipeline[frame_idx % 2], s->bloom_upsamples[step].desc_set[frame_idx % 2]);
				GPU_OpBeginRenderPass(graph);

				uint32_t dst_mip_level = BLOOM_PASS_COUNT - step - 1;
				GPU_OpPushGraphicsConstants(graph, s->main_pass_layout.pipeline_layout, &dst_mip_level, sizeof(dst_mip_level));
				GPU_OpBindDrawParams(graph, draw_params);
				GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

				GPU_OpEndRenderPass(graph);
			}

			// -- Final post process ---------------------
			
			GPU_OpPrepareRenderPass(graph, s->final_post_process_render_pass);
			uint32_t final_pp_draw_params = GPU_OpPrepareDrawParams(graph, s->final_post_process_pipeline, s->final_post_process_desc_set[frame_idx % 2]);
			GPU_OpBeginRenderPass(graph);

			GPU_OpBindDrawParams(graph, final_pp_draw_params);
			GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

			GPU_OpEndRenderPass(graph);

			// ---------------------------------------------

			GPU_GraphSubmit(graph);
		}
		
		taa_jitter_prev = taa_jitter;
		frame_idx++;
		time += 1.f/60.f;
	}

	return 0;
}

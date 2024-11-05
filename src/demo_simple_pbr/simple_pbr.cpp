
// Change this to "1" if you want to load a gun model instead of PBR spheres
#if 0
#define MESH_WORLD_PATH                 "../resources/Cerberus.glb"
#define TEX_BASE_COLOR_PATH             "../resources/Cerberus_Albedo.tga"
#define TEX_AO_ROUGHNESS_METALLIC_PATH  "../resources/Cerberus_ORM.tga"
#define TEX_NORMAL_PATH                 "../resources/Cerberus_Normal.tga"
#else
#define MESH_WORLD_PATH                 "../resources/MetalRoughSpheres.glb"
#define TEX_BASE_COLOR_PATH             "../resources/MetalRoughSpheres_Albedo.tga"
#define TEX_AO_ROUGHNESS_METALLIC_PATH  "../resources/MetalRoughSpheres_ORM.tga"
#define TEX_NORMAL_PATH                 "../resources/MetalRoughSpheres_Normal.tga"
#endif

// -------------------------------------------------------------------------------------------------

#define _CRT_SECURE_NO_WARNINGS

#include "src/fire/fire_ds.h"

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

#include "third_party/HandmadeMath.h"

#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

#include "src/utils/key_input/key_input.h"
#include "src/utils/key_input/key_input_fire_os.h"
#include "src/utils/camera.h"

#include <stdio.h>

// Define all your assets here!
// To add a new automatically hotreloaded asset, add it to this list. You can then access it through ASSETS.[name], e.g. ASSETS.TEX_BASE_COLOR
#define ASSETS_LIST\
	ASSET(TEX_ENV_CUBE,                    "../resources/shipyard_cranes_track_cube.hdr")\
	ASSET(MESH_SKYBOX,                     "../resources/UnitCube.glb")\
	ASSET(SHADER_MAIN,                     "../src/demo_simple_pbr/shader.glsl")\
	ASSET(SHADER_SKYBOX,                   "../src/demo_simple_pbr/skybox_shader.glsl")\
	ASSET(SHADER_GEN_IRRADIANCE_MAP,       "../src/demo_simple_pbr/gen_irradiance_map.glsl")\
	ASSET(SHADER_GEN_PREFILTERED_ENV_MAP,  "../src/demo_simple_pbr/gen_prefiltered_env_map.glsl")\
	ASSET(SHADER_GEN_BRDF_INTEGRATION_MAP, "../src/demo_simple_pbr/gen_brdf_integration_map.glsl")\
	ASSET(MESH_WORLD,                      MESH_WORLD_PATH)\
	ASSET(TEX_BASE_COLOR,                  TEX_BASE_COLOR_PATH)\
	ASSET(TEX_AO_ROUGHNESS_METALLIC,       TEX_AO_ROUGHNESS_METALLIC_PATH)\
	ASSET(TEX_NORMAL,                      TEX_NORMAL_PATH)\
// --

typedef int AssetIndex;
	
// This struct will be populated with an array of integers going from 0 to the last asset idx. It's a bit hacky.
static struct {
	#define ASSET(NAME, PATH) AssetIndex NAME;
	ASSETS_LIST
	#undef ASSET
} ASSETS;

#define ASSET(NAME, PATH) PATH,
static const char* ASSET_PATHS[] = {ASSETS_LIST};
#undef ASSET

struct Vertex {
	HMM_Vec3 position;
	HMM_Vec3 normal; // this could be packed better!
	HMM_Vec2 tex_coord;
	int material_id;
};

struct Mesh {
	uint32_t num_indices;
	GPU_Buffer* vertex_buffer;
	GPU_Buffer* index_buffer;
};

struct GenIrradianceMapCtx {
	GPU_PipelineLayout* pipeline_layout;
	uint32_t sampler_binding;
	uint32_t tex_env_cube_binding;
	uint32_t output_binding;
	GPU_ComputePipeline* pipeline;
};

struct GenPrefilteredEnvMapCtx {
	GPU_PipelineLayout* pipeline_layout;
	uint32_t sampler_binding;
	uint32_t tex_env_cube_binding;
	uint32_t output_binding;
	GPU_ComputePipeline* pipeline;
};

struct GenBRDFIntegrationMapCtx {
	GPU_PipelineLayout* pipeline_layout;
	uint32_t output_binding;
	GPU_ComputePipeline* pipeline;
};

struct MainPassLayout {
	GPU_PipelineLayout* pipeline_layout;
	uint32_t globals_binding;
	uint32_t tex_base_color_binding;
	uint32_t tex_x_rough_metal_binding;
	uint32_t tex_normal_binding;
	uint32_t tex_env_cube_binding;
	uint32_t tex_irradiance_map_binding;
	uint32_t sampler_linear_clamp_binding;
	uint32_t sampler_linear_wrap_binding;
	uint32_t prefiltered_env_map_binding;
	uint32_t brdf_integration_map_binding;
};

struct Hotreloader {
	bool is_outdated[DS_ArrayCount(ASSET_PATHS)];
	
	// Getting the file modtime is relatively slow, so we only call it on one asset each frame.
	AssetIndex next_check_asset_idx;
	uint64_t file_modtimes[DS_ArrayCount(ASSET_PATHS)];
};

struct Scene {
	GPU_RenderPass* renderpass;
	GPU_Buffer* globals_buffer;

	MainPassLayout main_pass_layout;
	
	Mesh mesh_world;
	Mesh skybox_mesh;

	GPU_Texture* irradiance_map;
	GPU_Texture* brdf_lut;
	GPU_Texture* tex_specular_env_map;

	GPU_Texture* textures[DS_ArrayCount(ASSET_PATHS)];
	GPU_DescriptorSet* desc_set;
	GPU_GraphicsPipeline* main_pipeline;
	GPU_GraphicsPipeline* skybox_pipeline;
};

// ---------------------------------------------------------------------

static const uint32_t window_w = 1400;
static const uint32_t window_h = 1000;

static const float Z_NEAR = 0.02f;
static const float Z_FAR = 10000.f;

// -- OS-specific ------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static bool OS_FileLastModificationTime(const char* filepath, uint64_t* out_modtime) {
	wchar_t filepath_wide[MAX_PATH];
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filepath, -1, filepath_wide, MAX_PATH);

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

static bool OS_ReadEntireFile(DS_Arena* arena, const char* file, char** out_data, int* out_size) {
	FILE* f = fopen(file, "rb");
	if (f == NULL) return false;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char* data = DS_ArenaPush(arena, size);
	fread(data, size, 1, f);

	fclose(f);
	*out_data = data;
	*out_size = size;
	return true;
}

// ---------------------------------------------------------------------

static void MUST(bool x, const char* error) {
	if (!x) {
		printf("ERROR: %s\n", error);
		assert(0);
	}
}

static GPU_String GPU_StringInit(const char* str) {
	return {(char*)str, (int)strlen(str)};
}

static bool ReloadMesh(Mesh* result, DS_Arena* temp_arena, const char* filepath, uint32_t material_id) {
	cgltf_options options = {};
	cgltf_data* data = {};

	MUST(cgltf_parse_file(&options, filepath, &data) == cgltf_result_success, "failed reading mesh file");

	MUST(cgltf_load_buffers(&options, data, filepath) == cgltf_result_success, "`cgltf_load_buffers` failed");

	MUST(cgltf_validate(data) == cgltf_result_success, "`cgltf_validate` failed");

	MUST(data->meshes_count == 1, "GLTF file must contain a single mesh"); // TODO: treat separate meshes as separate primitives
	cgltf_mesh* mesh = &data->meshes[0];

	// Let's use a traditional AOS vertex buffer layout for now.
	// TODO: we could pre-reserve the size and waste less memory

	DS_DynArray(uint32_t) index_buffer = {temp_arena};
	DS_DynArray(Vertex) vertex_buffer = {temp_arena};

	for (cgltf_size primitive_i = 0; primitive_i < mesh->primitives_count; primitive_i++) {
		cgltf_primitive* primitive = &mesh->primitives[primitive_i];
		cgltf_accessor* indices = primitive->indices;

		// Fill index buffer

		uint32_t first_index = (uint32_t)index_buffer.count;
		uint32_t first_index_value = (uint32_t)vertex_buffer.count;
		DS_ArrResizeUndef(&index_buffer, index_buffer.count + (int)indices->count);

		{
			void* primitive_indices = (char*)indices->buffer_view->buffer->data + indices->buffer_view->offset;
			if (indices->component_type == cgltf_component_type_r_16u) {
				for (cgltf_size i = 0; i < indices->count; i++) {
					DS_ArrSet(index_buffer, first_index + i, first_index_value + ((uint16_t*)primitive_indices)[i]);
				}
			}
			else if (indices->component_type == cgltf_component_type_r_32u) {
				for (cgltf_size i = 0; i < indices->count; i++) {
					DS_ArrSet(index_buffer, first_index + i, first_index_value + ((uint32_t*)primitive_indices)[i]);
				}
			}
			else assert(0);
		}

		// Fill vertex buffer

		{
			HMM_Vec3* positions_data = NULL;
			HMM_Vec3* normals_data = NULL;
			HMM_Vec2* texcoords_data = NULL;

			MUST(primitive->attributes_count > 0, "Primitive must have vertex attributes");
			uint32_t num_vertices = (uint32_t)primitive->attributes[0].data->count;

			for (cgltf_size i = 0; i < primitive->attributes_count; i++) {
				cgltf_attribute* attribute = &primitive->attributes[i];
				void* attribute_data = (char*)attribute->data->buffer_view->buffer->data + attribute->data->buffer_view->offset;

				MUST(num_vertices == attribute->data->count, "All vertex attributes must have the same vertex count");

				if (attribute->type == cgltf_attribute_type_position) {
					positions_data = (HMM_Vec3*)attribute_data;
				} else if (attribute->type == cgltf_attribute_type_normal) {
					normals_data = (HMM_Vec3*)attribute_data;
				} else if (attribute->type == cgltf_attribute_type_texcoord) {
					texcoords_data = (HMM_Vec2*)attribute_data;
				}
			}

			MUST(positions_data && normals_data && texcoords_data, "Primitive must have position, normal and texture coordinate attributes");

			DS_ArrReserve(&vertex_buffer, num_vertices);

			for (uint32_t i = 0; i < num_vertices; i++) {
				Vertex vertex;
				vertex.position = positions_data[i];
				vertex.normal = normals_data[i];
				vertex.tex_coord = texcoords_data[i];
				
				// In GLTF, Y is up, but we want Z up.
				vertex.position = {vertex.position.X, -1.f * vertex.position.Z, vertex.position.Y};
				vertex.normal = {vertex.normal.X, -1.f * vertex.normal.Z, vertex.normal.Y};

				vertex.material_id = material_id;
				DS_ArrPush(&vertex_buffer, vertex);
			}
		}
	}

	*result = Mesh{};
	result->num_indices = (uint32_t)index_buffer.count;
	
	GPU_DestroyBuffer(result->vertex_buffer);
	result->vertex_buffer = GPU_MakeBuffer((uint32_t)vertex_buffer.count * sizeof(*vertex_buffer.data), GPU_BufferFlag_GPU, vertex_buffer.data);
	
	GPU_DestroyBuffer(result->index_buffer);
	result->index_buffer = GPU_MakeBuffer((uint32_t)index_buffer.count * sizeof(*index_buffer.data), GPU_BufferFlag_GPU, index_buffer.data);
	
	cgltf_free(data);
	
	return true;
}

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
		hotreloader->is_outdated[check_idx] = true;
		hotreloader->file_modtimes[check_idx] = modtime;
	}
}

static GPU_Texture* RefreshTexture(Hotreloader* r, Scene* s, AssetIndex asset, bool* reloaded) {
	if (r->is_outdated[asset]) {
		int x, y, comp;
		uint8_t* data = stbi_load(ASSET_PATHS[asset], &x, &y, &comp, 4);
		
		GPU_DestroyTexture(s->textures[asset]);
		s->textures[asset] = GPU_MakeTexture(GPU_Format_RGBA8UN, (uint32_t)x, (uint32_t)y, 1, GPU_TextureFlag_HasMipmaps, data);
		stbi_image_free(data);
		
		*reloaded = true;
	}
	return s->textures[asset];
}

static GPU_ComputePipeline* LoadComputeShader(DS_Arena* arena, AssetIndex asset, GPU_PipelineLayout* pipeline_layout, GPU_ShaderDesc* cs_desc) {
	for (;;) {
		GPU_String shader_src;
		while (!OS_ReadEntireFile(arena, ASSET_PATHS[asset], (char**)&shader_src.data, &shader_src.length)) {}

		cs_desc->glsl = shader_src;
		cs_desc->glsl_debug_filepath = GPU_StringInit(ASSET_PATHS[asset]);

		GPU_GLSLErrorArray errors = {};
		cs_desc->spirv = GPU_SPIRVFromGLSL(arena, GPU_ShaderStage_Compute, pipeline_layout, cs_desc, &errors);
		if (cs_desc->spirv.length == 0) {
			GPU_String error_str = GPU_JoinGLSLErrorString(arena, errors);
			printf("Compute shader error in %s: \"%.*s\"\n", ASSET_PATHS[asset], error_str.length, error_str.data);
			OS_SleepMilliseconds(100);
			continue;
		}
		break;
	}

	GPU_ComputePipeline* result = GPU_MakeComputePipeline(pipeline_layout, cs_desc);
	return result;
}

static void LoadVertexAndFragmentShader(DS_Arena* arena, AssetIndex asset, GPU_PipelineLayout* pipeline_layout, GPU_ShaderDesc* vs_desc, GPU_ShaderDesc* fs_desc) {
	for (;;) {
		GPU_String shader_src;
		while (!OS_ReadEntireFile(arena, ASSET_PATHS[asset], (char**)&shader_src.data, &shader_src.length)) {
			printf("Trying to load %s...\n", ASSET_PATHS[asset]);
			OS_SleepMilliseconds(100);
		}

		fs_desc->glsl = shader_src;
		vs_desc->glsl = shader_src;
		fs_desc->glsl_debug_filepath = GPU_StringInit(ASSET_PATHS[asset]);
		vs_desc->glsl_debug_filepath = GPU_StringInit(ASSET_PATHS[asset]);

		GPU_GLSLErrorArray errors = {};
		vs_desc->spirv = GPU_SPIRVFromGLSL(arena, GPU_ShaderStage_Vertex, pipeline_layout, vs_desc, &errors);
		if (vs_desc->spirv.length == 0) {
			GPU_String error_str = GPU_JoinGLSLErrorString(arena, errors);
			printf("Vertex shader error in %s: \"%.*s\"\n", ASSET_PATHS[asset], error_str.length, error_str.data);
			OS_SleepMilliseconds(100);
			continue;
		}
		fs_desc->spirv = GPU_SPIRVFromGLSL(arena, GPU_ShaderStage_Fragment, pipeline_layout, fs_desc, &errors);
		if (fs_desc->spirv.length == 0) {
			GPU_String error_str = GPU_JoinGLSLErrorString(arena, errors);
			printf("Fragment shader error in %s: \"%.*s\"\n", ASSET_PATHS[asset], error_str.length, error_str.data);
			OS_SleepMilliseconds(100);
			continue;
		}
		break;
	}	
}

static void UpdateScene(DS_Arena* temp_arena, Hotreloader* r, Scene* s) {
	bool update_desc_set = false;
	GPU_Texture* tex_base_color = RefreshTexture(r, s, ASSETS.TEX_BASE_COLOR, &update_desc_set);
	GPU_Texture* tex_x_rough_metal = RefreshTexture(r, s, ASSETS.TEX_AO_ROUGHNESS_METALLIC, &update_desc_set);
	GPU_Texture* tex_normal = RefreshTexture(r, s, ASSETS.TEX_NORMAL, &update_desc_set);

	if (r->is_outdated[ASSETS.TEX_ENV_CUBE]) {
		int x, y, comp;
		void* data = stbi_loadf(ASSET_PATHS[ASSETS.TEX_ENV_CUBE], &x, &y, &comp, 4);
		assert(y == x*6);
	
		GPU_DestroyTexture(s->textures[ASSETS.TEX_ENV_CUBE]);
		s->textures[ASSETS.TEX_ENV_CUBE] = GPU_MakeTexture(GPU_Format_RGBA32F, (uint32_t)x, (uint32_t)x, 1, GPU_TextureFlag_Cubemap|GPU_TextureFlag_HasMipmaps, data);
		
		stbi_image_free(data);
		update_desc_set = true;
	}
	GPU_Texture* tex_env_cube = s->textures[ASSETS.TEX_ENV_CUBE];

	if (r->is_outdated[ASSETS.SHADER_MAIN]) {
		MainPassLayout* pass = &s->main_pass_layout;

		GPU_ShaderDesc vs_desc{};
		GPU_ShaderDesc fs_desc{};

		GPU_Access vs_accesses[] = { GPU_Read(pass->globals_binding) };
		GPU_Access fs_accesses[] = {
			GPU_Read(pass->globals_binding),
			GPU_Read(pass->tex_base_color_binding),
			GPU_Read(pass->tex_x_rough_metal_binding),
			GPU_Read(pass->tex_normal_binding),
			GPU_Read(pass->tex_irradiance_map_binding),
			GPU_Read(pass->sampler_linear_clamp_binding),
			GPU_Read(pass->sampler_linear_wrap_binding),
			GPU_Read(pass->prefiltered_env_map_binding),
			GPU_Read(pass->brdf_integration_map_binding)
		};

		vs_desc.accesses = vs_accesses; vs_desc.accesses_count = DS_ArrayCount(vs_accesses);
		fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
		LoadVertexAndFragmentShader(temp_arena, ASSETS.SHADER_MAIN, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_Format vertex_input_formats[] = {GPU_Format_RGB32F, GPU_Format_RGB32F, GPU_Format_RG32F, GPU_Format_R32I};

		GPU_GraphicsPipelineDesc pipeline_desc{};
		pipeline_desc.layout = pass->pipeline_layout;
		pipeline_desc.render_pass = s->renderpass;
		pipeline_desc.vs = vs_desc;
		pipeline_desc.fs = fs_desc;
		pipeline_desc.vertex_input_formats = vertex_input_formats;
		pipeline_desc.vertex_input_formats_count = DS_ArrayCount(vertex_input_formats);
		pipeline_desc.enable_depth_test = true;
		pipeline_desc.enable_depth_write = true;
		s->main_pipeline = GPU_MakeGraphicsPipeline(&pipeline_desc);
	}

	if (r->is_outdated[ASSETS.SHADER_SKYBOX]) {
		MainPassLayout* pass = &s->main_pass_layout;

		GPU_ShaderDesc vs_desc{};
		GPU_ShaderDesc fs_desc{};

		GPU_Access vs_accesses[] = { GPU_Read(pass->globals_binding) };
		GPU_Access fs_accesses[] = {
			GPU_Read(pass->prefiltered_env_map_binding),
			GPU_Read(pass->tex_env_cube_binding),
			GPU_Read(pass->sampler_linear_clamp_binding),
		};

		vs_desc.accesses = vs_accesses; vs_desc.accesses_count = DS_ArrayCount(vs_accesses);
		fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
		LoadVertexAndFragmentShader(temp_arena, ASSETS.SHADER_SKYBOX, pass->pipeline_layout, &vs_desc, &fs_desc);
		
		GPU_Format vertex_input_formats[] = {GPU_Format_RGB32F, GPU_Format_RGB32F, GPU_Format_RG32F, GPU_Format_R32I};
		
		GPU_GraphicsPipelineDesc pipeline_desc{};
		pipeline_desc.layout = pass->pipeline_layout;
		pipeline_desc.render_pass = s->renderpass;
		pipeline_desc.vs = vs_desc;
		pipeline_desc.fs = fs_desc;
		pipeline_desc.vertex_input_formats = vertex_input_formats;
		pipeline_desc.vertex_input_formats_count = DS_ArrayCount(vertex_input_formats);
		pipeline_desc.enable_depth_test = true;
		pipeline_desc.enable_depth_write = true;
		s->skybox_pipeline = GPU_MakeGraphicsPipeline(&pipeline_desc);
	}

	if (r->is_outdated[ASSETS.SHADER_GEN_IRRADIANCE_MAP]) {
		GenIrradianceMapCtx ctx{};
		ctx.pipeline_layout = GPU_InitPipelineLayout();
		ctx.sampler_binding = GPU_SamplerBinding(ctx.pipeline_layout, "SAMPLER_LINEAR_CLAMP");
		ctx.tex_env_cube_binding = GPU_TextureBinding(ctx.pipeline_layout, "TEX_ENV_CUBE");
		ctx.output_binding = GPU_StorageImageBinding(ctx.pipeline_layout, "OUTPUT", GPU_Format_RGBA32F);
		GPU_FinalizePipelineLayout(ctx.pipeline_layout);
		
		GPU_Access cs_accesses[] = { GPU_Read(ctx.sampler_binding), GPU_Read(ctx.tex_env_cube_binding), GPU_Write(ctx.output_binding) };
		GPU_ShaderDesc cs_desc{};
		cs_desc.accesses = cs_accesses; cs_desc.accesses_count = DS_ArrayCount(cs_accesses);

		ctx.pipeline = LoadComputeShader(temp_arena, ASSETS.SHADER_GEN_IRRADIANCE_MAP, ctx.pipeline_layout, &cs_desc);
		
		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, ctx.pipeline_layout);
		GPU_SetSamplerBinding(desc_set, ctx.sampler_binding, GPU_SamplerLinearClamp());
		GPU_SetTextureBinding(desc_set, ctx.tex_env_cube_binding, tex_env_cube);
		GPU_SetStorageImageBinding(desc_set, ctx.output_binding, s->irradiance_map, 0);
		GPU_FinalizeDescriptorSet(desc_set);
		
		GPU_Graph* graph = GPU_MakeGraph();
		GPU_OpBindComputePipeline(graph, ctx.pipeline);
		GPU_OpBindComputeDescriptorSet(graph, desc_set);

		GPU_OpDispatch(graph, s->irradiance_map->width / 8, s->irradiance_map->height / 8, 1);
		GPU_GraphSubmit(graph);
		GPU_GraphWait(graph);
		GPU_DestroyGraph(graph);
		GPU_DestroyDescriptorSet(desc_set);
	}

	if (r->is_outdated[ASSETS.SHADER_GEN_PREFILTERED_ENV_MAP]) {
		GenPrefilteredEnvMapCtx ctx{};
		
		ctx.pipeline_layout = GPU_InitPipelineLayout(); // TODO: fix leak
		ctx.sampler_binding = GPU_SamplerBinding(ctx.pipeline_layout, "SAMPLER_LINEAR_CLAMP");
		ctx.tex_env_cube_binding = GPU_TextureBinding(ctx.pipeline_layout, "TEX_ENV_CUBE");
		ctx.output_binding = GPU_StorageImageBinding(ctx.pipeline_layout, "OUTPUT", GPU_Format_RGBA32F);
		GPU_FinalizePipelineLayout(ctx.pipeline_layout);
		
		GPU_Access cs_accesses[] = { GPU_Read(ctx.sampler_binding), GPU_Read(ctx.tex_env_cube_binding), GPU_Write(ctx.output_binding) };
		GPU_ShaderDesc cs_desc{};
		cs_desc.accesses = cs_accesses; cs_desc.accesses_count = DS_ArrayCount(cs_accesses);

		ctx.pipeline = LoadComputeShader(temp_arena, ASSETS.SHADER_GEN_PREFILTERED_ENV_MAP, ctx.pipeline_layout, &cs_desc);

		GPU_Graph* graph = GPU_MakeGraph();
		GPU_OpBindComputePipeline(graph, ctx.pipeline);

		GPU_DescriptorArena* descriptor_arena = GPU_MakeDescriptorArena();

		uint32_t size = s->tex_specular_env_map->width;
		for (uint32_t i = 0; i < s->tex_specular_env_map->mip_level_count; i++) {
			if (size < 16) break; // stop at 16x16

			GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(descriptor_arena, ctx.pipeline_layout);
			GPU_SetSamplerBinding(desc_set, ctx.sampler_binding, GPU_SamplerLinearClamp());
			GPU_SetTextureBinding(desc_set, ctx.tex_env_cube_binding, tex_env_cube);
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
		GenBRDFIntegrationMapCtx ctx{};
		
		ctx.pipeline_layout = GPU_InitPipelineLayout();  // TODO: fix leak
		ctx.output_binding = GPU_StorageImageBinding(ctx.pipeline_layout, "OUTPUT", GPU_Format_RG16F);
		GPU_FinalizePipelineLayout(ctx.pipeline_layout);
		
		GPU_Access cs_accesses[] = { GPU_Write(ctx.output_binding) };
		GPU_ShaderDesc cs_desc{};
		cs_desc.accesses = cs_accesses; cs_desc.accesses_count = DS_ArrayCount(cs_accesses);
		ctx.pipeline = LoadComputeShader(temp_arena, ASSETS.SHADER_GEN_BRDF_INTEGRATION_MAP, ctx.pipeline_layout, &cs_desc);

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

	if (update_desc_set) {
		MainPassLayout* pass = &s->main_pass_layout;
		GPU_DestroyDescriptorSet(s->desc_set);
		s->desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
		GPU_SetBufferBinding(s->desc_set, pass->globals_binding, s->globals_buffer);
		GPU_SetSamplerBinding(s->desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
		GPU_SetSamplerBinding(s->desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
		GPU_SetTextureBinding(s->desc_set, pass->tex_base_color_binding, tex_base_color);
		GPU_SetTextureBinding(s->desc_set, pass->tex_x_rough_metal_binding, tex_x_rough_metal);
		GPU_SetTextureBinding(s->desc_set, pass->tex_normal_binding, tex_normal);
		GPU_SetTextureBinding(s->desc_set, pass->tex_env_cube_binding, tex_env_cube);
		GPU_SetTextureBinding(s->desc_set, pass->tex_irradiance_map_binding, s->irradiance_map);
		GPU_SetTextureBinding(s->desc_set, pass->prefiltered_env_map_binding, s->tex_specular_env_map);
		GPU_SetTextureBinding(s->desc_set, pass->brdf_integration_map_binding, s->brdf_lut);
		GPU_FinalizeDescriptorSet(s->desc_set);
	}

	if (r->is_outdated[ASSETS.MESH_WORLD]) {
		bool ok = ReloadMesh(&s->mesh_world, temp_arena, ASSET_PATHS[ASSETS.MESH_WORLD], 0);
		assert(ok);
	}

	if (r->is_outdated[ASSETS.MESH_SKYBOX]) {
		bool ok = ReloadMesh(&s->skybox_mesh, temp_arena, ASSET_PATHS[ASSETS.MESH_SKYBOX], 0);
		assert(ok);
	}
	
	// Finally, tell the hotreloader that we have checked everything
	for (int i = 0; i < DS_ArrayCount(r->is_outdated); i++) r->is_outdated[i] = false;
}

int main() {
	DS_Arena temp_arena;
	DS_ArenaInit(&temp_arena, 4096*16, DS_HEAP);

	OS_WINDOW window = OS_WINDOW_Create(window_w, window_h, "PBR rendering");
	GPU_Init(window.handle);

	// Init asset index table
	for (int i = 0; i < DS_ArrayCount(ASSET_PATHS); i++) ((AssetIndex*)&ASSETS)[i] = i;
	
	Scene scene{};
	
	Hotreloader hotreloader{};
	for (int i = 0; i < DS_ArrayCount(hotreloader.is_outdated); i++) {
		hotreloader.is_outdated[i] = true;
	}

	GPU_Texture* depth_rt = GPU_MakeTexture(GPU_Format_D32F_Or_X8D24UN, (uint32_t)window_w, (uint32_t)window_h, 1, GPU_TextureFlag_RenderTarget, NULL);

	Camera camera{};
	camera.pos = {0.f, -10.f, 0.f};

	struct {
		HMM_Mat4 clip_space_from_world;
		HMM_Vec3 camera_pos;
	} globals;

	// Init scene
	{
		scene.globals_buffer = GPU_MakeBuffer(sizeof(globals), GPU_BufferFlag_CPU|GPU_BufferFlag_GPU|GPU_BufferFlag_StorageBuffer, NULL);
		
		GPU_RenderPassDesc renderpass_desc{};
		renderpass_desc.color_targets = GPU_SWAPCHAIN_COLOR_TARGET;
		renderpass_desc.color_targets_count = 1;
		renderpass_desc.depth_stencil_target = depth_rt;
		scene.renderpass = GPU_MakeRenderPass(&renderpass_desc);

		scene.irradiance_map = GPU_MakeTexture(GPU_Format_RGBA32F, 32, 32, 1, GPU_TextureFlag_Cubemap|GPU_TextureFlag_StorageImage, NULL);
		scene.brdf_lut = GPU_MakeTexture(GPU_Format_RG16F, 256, 256, 1, GPU_TextureFlag_StorageImage, NULL);
		scene.tex_specular_env_map = GPU_MakeTexture(GPU_Format_RGBA32F, 256, 256, 1, GPU_TextureFlag_Cubemap|GPU_TextureFlag_HasMipmaps|GPU_TextureFlag_StorageImage, NULL);

		MainPassLayout* lo = &scene.main_pass_layout;
		lo->pipeline_layout = GPU_InitPipelineLayout();
		lo->globals_binding = GPU_BufferBinding(lo->pipeline_layout, "GLOBALS");
		lo->tex_base_color_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX_BASE_COLOR");
		lo->tex_x_rough_metal_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX_X_ROUGH_METAL");
		lo->tex_normal_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX_NORMAL");
		lo->tex_env_cube_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX_ENV_CUBE");
		
		lo->tex_irradiance_map_binding = GPU_TextureBinding(lo->pipeline_layout, "TEX_IRRADIANCE_MAP");
		lo->sampler_linear_clamp_binding = GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_LINEAR_CLAMP");
		lo->sampler_linear_wrap_binding = GPU_SamplerBinding(lo->pipeline_layout, "SAMPLER_LINEAR_WRAP");
		lo->prefiltered_env_map_binding = GPU_TextureBinding(lo->pipeline_layout, "PREFILTERED_ENV_MAP");
		lo->brdf_integration_map_binding = GPU_TextureBinding(lo->pipeline_layout, "BRDF_INTEGRATION_MAP");
		GPU_FinalizePipelineLayout(lo->pipeline_layout);
	}

	float time = 0.f;

	GPU_Graph* graphs[2];
	GPU_MakeSwapchainGraphs(2, &graphs[0]);
	int graph_idx = 0;
	
	for (;;) {
		DS_ArenaReset(&temp_arena);

		Input_Frame input_frame;
		
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

		time += 1.f/60.f;

		UpdateHotreloader(&hotreloader);
		UpdateScene(&temp_arena, &hotreloader, &scene);

		float movement_speed = 0.05f;
		float mouse_speed = 0.001f;
		float FOV = 70.f;
		Camera_Update(&camera, &input_frame, movement_speed, mouse_speed, FOV, (float)window_w / (float)window_h, Z_NEAR, Z_FAR);

		globals.clip_space_from_world = camera.clip_from_world;
		globals.camera_pos = camera.pos;
		memcpy(scene.globals_buffer->data, &globals, sizeof(globals));

		graph_idx = (graph_idx + 1) % 2;
		GPU_Graph* graph = graphs[graph_idx];
		GPU_GraphWait(graph);

		GPU_Texture* backbuffer = GPU_GetBackbuffer(graph);
		if (backbuffer) {
			GPU_OpClearColorF(graph, backbuffer, 0, 0.1f, 0.2f, 0.5f, 1.f);
			GPU_OpClearDepthStencil(graph, depth_rt, 0);
			
			GPU_OpPrepareRenderPass(graph, scene.renderpass);
			uint32_t main_draw_params = GPU_OpPrepareDrawParams(graph, scene.main_pipeline, scene.desc_set);
			uint32_t skybox_draw_params = GPU_OpPrepareDrawParams(graph, scene.skybox_pipeline, scene.desc_set);
			GPU_OpBeginRenderPass(graph);
			
			GPU_OpBindDrawParams(graph, main_draw_params);
			GPU_OpPushGraphicsConstants(graph, scene.main_pass_layout.pipeline_layout, &time, sizeof(time));
			GPU_OpBindVertexBuffer(graph, scene.mesh_world.vertex_buffer);
			GPU_OpBindIndexBuffer(graph, scene.mesh_world.index_buffer);
			GPU_OpDrawIndexed(graph, scene.mesh_world.num_indices, 1, 0, 0, 0);

			GPU_OpBindDrawParams(graph, skybox_draw_params);
			GPU_OpBindVertexBuffer(graph, scene.skybox_mesh.vertex_buffer);
			GPU_OpBindIndexBuffer(graph, scene.skybox_mesh.index_buffer);
			GPU_OpDrawIndexed(graph, scene.skybox_mesh.num_indices, 1, 0, 0, 0);
			
			GPU_OpEndRenderPass(graph);
			GPU_GraphSubmit(graph);
		}
	}

	return 0;
}
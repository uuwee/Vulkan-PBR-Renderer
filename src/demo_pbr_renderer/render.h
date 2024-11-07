
#define BLOOM_PASS_COUNT 6

#define SHADER_ASSETS \
	X(LightgridVoxelize,       "../src/demo_pbr_renderer/shaders/lightgrid_voxelize.glsl")\
	X(LightgridSweep,          "../src/demo_pbr_renderer/shaders/lightgrid_sweep.glsl")\
	X(GeometryPass,            "../src/demo_pbr_renderer/shaders/geometry_pass.glsl")\
	X(LightingPass,            "../src/demo_pbr_renderer/shaders/lighting_pass.glsl")\
	X(TAAResolve,              "../src/demo_pbr_renderer/shaders/taa_resolve.glsl")\
	X(BloomDownsample,         "../src/demo_pbr_renderer/shaders/bloom_downsample.glsl")\
	X(BloomUpsample,           "../src/demo_pbr_renderer/shaders/bloom_upsample.glsl")\
	X(FinalPostProcess,        "../src/demo_pbr_renderer/shaders/final_post_process.glsl")\
	X(SunDepthPass,            "../src/demo_pbr_renderer/shaders/sun_depth_pass.glsl")\
	X(GenIrradianceMap,        "../src/demo_pbr_renderer/shaders/gen_irradiance_map.glsl")\
	X(GenPrefilteredEnvMap,    "../src/demo_pbr_renderer/shaders/gen_prefiltered_env_map.glsl")\
	X(GenBRDFIntegrationMap,   "../src/demo_pbr_renderer/shaders/gen_brdf_integration_map.glsl")

enum class ShaderAsset {
#define X(TAG, PATH) TAG,
	SHADER_ASSETS
#undef X
	COUNT,
};

static const char* ShaderAssetPaths[] = {
#define X(TAG, PATH) PATH,
	SHADER_ASSETS
#undef X
};

struct Vertex {
	HMM_Vec3 position;
	HMM_Vec3 normal; // this could be packed better!
	HMM_Vec3 tangent; // this could be packed better!
	HMM_Vec2 tex_coord;
};

struct MainPassLayout {
	GPU_PipelineLayout* pipeline_layout;
	
	uint32_t globals_binding;
	uint32_t tex0_binding; // base color
	uint32_t tex1_binding; // normal
	uint32_t tex2_binding; // occlusion-roughness-metallic
	uint32_t tex3_binding; // emissive
	uint32_t sun_depth_map_binding_;

	uint32_t sampler_linear_clamp_binding;
	uint32_t sampler_linear_wrap_binding;
	uint32_t sampler_percentage_closer;
	
	uint32_t ssbo0_binding; // for vertex buffer in lightgrid voxelize
	uint32_t ssbo1_binding; // for index buffer in lightgrid voxelize
	uint32_t img0_binding; // for lightmap image in lightgrid voxelize
	
	// TAA resolve stuff
	uint32_t prev_frame_result_binding;
	uint32_t lighting_result_rt;
	uint32_t gbuffer_depth_binding;
	uint32_t gbuffer_depth_prev_binding;
	uint32_t gbuffer_velocity_binding;
	uint32_t gbuffer_velocity_prev_binding;
};

struct LightingPassLayout {
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
};

typedef uint64_t StringHash;

struct ShaderHotreloader {
	bool shader_is_outdated[(int)ShaderAsset::COUNT];
	uint64_t file_modtimes[(int)ShaderAsset::COUNT];
	int next_check_shader_idx; // Getting the file modtime is relatively slow so call it on one asset each frame.
};

struct RenderObjectPart {
	GPU_Texture* tex_base_color;
	GPU_Texture* tex_normal;
	GPU_Texture* tex_orm;
	GPU_Texture* tex_emissive;

	uint32_t first_index;
	uint32_t index_count;
	GPU_DescriptorSet* descriptor_set;
};

struct RenderObject {
	GPU_Buffer* vertex_buffer;
	GPU_Buffer* index_buffer;
	DS_DynArray<RenderObjectPart> parts;
};

struct BloomDownsamplePass {
	GPU_RenderPass* render_pass[2];
	GPU_GraphicsPipeline* pipeline[2];
	GPU_DescriptorSet* desc_set[2];
};

struct BloomUpsamplePass {
	GPU_RenderPass* render_pass[2];
	GPU_GraphicsPipeline* pipeline[2];
	GPU_DescriptorSet* desc_set[2];
};

struct RendererGlobalsBuffer {
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
	uint32_t visualize_lightgrid;
};

struct Renderer {
	ShaderHotreloader shader_hotreloader;

	uint32_t window_width, window_height;
	GPU_Texture* tex_env_cube;

	GPU_RenderPass* lightgrid_voxelize_render_pass;
	GPU_RenderPass* geometry_render_pass[2];
	GPU_RenderPass* lighting_render_pass;
	GPU_RenderPass* taa_resolve_render_pass[2];
	GPU_RenderPass* sun_depth_render_pass;
	GPU_RenderPass* final_post_process_render_pass;
	GPU_Buffer* globals_buffer;

	GPU_Sampler* sampler_percentage_closer;

	MainPassLayout main_pass_layout;
	LightingPassLayout lighting_pass_layout;
	
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

	GPU_Texture* bloom_downscale_rt;
	GPU_Texture* bloom_upscale_rt;
	
	GPU_Texture* dummy_normal_map;
	GPU_Texture* dummy_black;
	GPU_Texture* dummy_white;
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

	GPU_GraphicsPipeline* sun_depth_pipeline;
	GPU_GraphicsPipeline* geometry_pass_pipeline[2];

	HMM_Vec2 taa_jitter_prev_frame;
	HMM_Mat4 clip_space_from_world_prev_frame;
	HMM_Vec2 sun_angle_prev_frame;
	uint32_t frame_idx;
	uint32_t sweep_direction;
};

struct RenderParameters {
	HMM_Vec2 sun_angle;
	bool visualize_lightgrid;
};

// -------------------------------------------------------------

void InitRenderer(Renderer* r, uint32_t window_width, uint32_t window_height);

void DeinitRenderer(Renderer* r);

void HotreloadShaders(Renderer* r, GPU_Texture* tex_env_cube);

void BuildRenderCommands(Renderer* rs, GPU_Graph* graph, GPU_Texture* backbuffer, RenderObject* world, RenderObject* skybox, const Camera& camera, const RenderParameters& params);

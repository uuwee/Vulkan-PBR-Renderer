#include "common.h"
#include "render.h"
#include "os_utils.h"

extern DS_Arena* TEMP; // Arena for per-frame, temporary allocations

// Should we move the hotreloader into Utils? The same way camera is an util.
static void UpdateShaderHotreloader(ShaderHotreloader* hotreloader) {
	int check_idx = hotreloader->next_check_shader_idx;
	hotreloader->next_check_shader_idx = (hotreloader->next_check_shader_idx + 1) % ShaderAsset_COUNT;

	uint64_t modtime;
	bool ok = OS_FileLastModificationTime(ShaderAssetPaths[check_idx], &modtime);
	assert(ok);

	if (hotreloader->file_modtimes[check_idx] == 0) { // Initialize modtime for this file
		hotreloader->file_modtimes[check_idx] = modtime;
	}

	if (modtime != hotreloader->file_modtimes[check_idx]) {
		hotreloader->shader_is_outdated[check_idx] = true;
		hotreloader->file_modtimes[check_idx] = modtime;
	}
}

static GPU_ComputePipeline* LoadComputeShader(ShaderAsset shader_asset, GPU_PipelineLayout* pipeline_layout, GPU_ShaderDesc* cs_desc) {
	STR_View shader_path = ShaderAssetPaths[shader_asset];
	for (;;) {
		STR_View shader_src;
		while (!OS_ReadEntireFile(TEMP, shader_path, &shader_src)) {}

		cs_desc->glsl = {shader_src.data, shader_src.size};
		cs_desc->glsl_debug_filepath = {shader_path.data, shader_path.size};

		GPU_GLSLErrorArray errors = {};
		cs_desc->spirv = GPU_SPIRVFromGLSL(TEMP, GPU_ShaderStage_Compute, pipeline_layout, cs_desc, &errors);
		if (cs_desc->spirv.length == 0) {
			STR_View err = STR_Form(TEMP, "Error in \"%v\": %v", shader_path, GPU_JoinGLSLErrorString(TEMP, errors));
			OS_MessageBox(err);
			continue;
		}
		break;
	}

	GPU_ComputePipeline* result = GPU_MakeComputePipeline(pipeline_layout, cs_desc);
	return result;
}

static void LoadVertexAndFragmentShader(DS_Arena* arena, ShaderAsset shader_asset,
	GPU_PipelineLayout* pipeline_layout, GPU_ShaderDesc* vs_desc, GPU_ShaderDesc* fs_desc)
{
	STR_View shader_path = ShaderAssetPaths[shader_asset];
	for (;;) {
		STR_View shader_src;
		while (!OS_ReadEntireFile(TEMP, shader_path, &shader_src)) {}

		fs_desc->glsl = {shader_src.data, shader_src.size};
		vs_desc->glsl = {shader_src.data, shader_src.size};
		fs_desc->glsl_debug_filepath = {shader_path.data, shader_path.size};
		vs_desc->glsl_debug_filepath = {shader_path.data, shader_path.size};

		GPU_GLSLErrorArray errors = {};
		vs_desc->spirv = GPU_SPIRVFromGLSL(arena, GPU_ShaderStage_Vertex, pipeline_layout, vs_desc, &errors);
		if (vs_desc->spirv.length == 0) {
			STR_View err = STR_Form(TEMP, "Error in \"%v\": %v", shader_path, GPU_JoinGLSLErrorString(TEMP, errors));
			OS_MessageBox(err);
			continue;
		}
		fs_desc->spirv = GPU_SPIRVFromGLSL(arena, GPU_ShaderStage_Fragment, pipeline_layout, fs_desc, &errors);
		if (fs_desc->spirv.length == 0) {
			STR_View err = STR_Form(TEMP, "Error in \"%v\": %v", shader_path, GPU_JoinGLSLErrorString(TEMP, errors));
			OS_MessageBox(err);
			continue;
		}
		break;
	}
}

void HotreloadShaders(Renderer* renderer) {
	DS_ArenaMark T = DS_ArenaGetMark(TEMP);
	
	ShaderHotreloader* reloader = &renderer->shader_hotreloader;
	if (reloader->shader_is_outdated[ShaderAsset_SunDepthPass]) {
		MainPassLayout* pass = &renderer->main_pass_layout;
		GPU_WaitUntilIdle();
		GPU_DestroyGraphicsPipeline(renderer->sun_depth_pipeline);

		GPU_Access accesses[] = {GPU_Read(pass->globals_binding)};

		GPU_ShaderDesc vs_desc = {};
		vs_desc.accesses = accesses; vs_desc.accesses_count = DS_ArrayCount(accesses);

		GPU_ShaderDesc fs_desc = {};
		LoadVertexAndFragmentShader(TEMP, ShaderAsset_SunDepthPass, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_Format vertex_formats[] = { GPU_Format_RGB32F, GPU_Format_RGB32F, GPU_Format_RGB32F, GPU_Format_RG32F };

		GPU_GraphicsPipelineDesc desc = {};
		desc.layout = pass->pipeline_layout;
		desc.render_pass = renderer->sun_depth_render_pass;
		desc.vs = vs_desc;
		desc.fs = fs_desc;
		desc.vertex_input_formats = vertex_formats;
		desc.vertex_input_formats_count = DS_ArrayCount(vertex_formats);
		desc.enable_depth_test = true;
		desc.enable_depth_write = true;
		// desc.cull_mode = GPU_CullMode_DrawCCW,
		renderer->sun_depth_pipeline = GPU_MakeGraphicsPipeline(&desc);
	}

	if (reloader->shader_is_outdated[ShaderAsset_LightgridVoxelize]) {
		MainPassLayout* pass = &renderer->main_pass_layout;
		GPU_WaitUntilIdle();
		GPU_DestroyGraphicsPipeline(renderer->lightgrid_voxelize_pipeline);

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

		LoadVertexAndFragmentShader(TEMP, ShaderAsset_LightgridVoxelize, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_GraphicsPipelineDesc desc = {};
		desc.layout = pass->pipeline_layout;
		desc.render_pass = renderer->lightgrid_voxelize_render_pass;
		desc.vs = vs_desc;
		desc.fs = fs_desc;
		desc.enable_conservative_rasterization = true;
		renderer->lightgrid_voxelize_pipeline = GPU_MakeGraphicsPipeline(&desc);
	}

	if (reloader->shader_is_outdated[ShaderAsset_LightgridSweep]) {
		MainPassLayout* pass = &renderer->main_pass_layout;
		GPU_WaitUntilIdle();
		GPU_DestroyComputePipeline(renderer->lightgrid_sweep_pipeline);

		GPU_Access cs_accesses[] = {
			GPU_ReadWrite(pass->img0_binding),
		};

		GPU_ShaderDesc cs_desc = {};
		cs_desc.accesses = cs_accesses; cs_desc.accesses_count = DS_ArrayCount(cs_accesses);
		LoadComputeShader(ShaderAsset_LightgridSweep, pass->pipeline_layout, &cs_desc);
		renderer->lightgrid_sweep_pipeline = GPU_MakeComputePipeline(pass->pipeline_layout, &cs_desc);

		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
		GPU_SetStorageImageBinding(desc_set, pass->img0_binding, renderer->lightgrid, 0);

		// ... unused descriptors. This is stupid.
		GPU_SetBufferBinding(desc_set, pass->globals_binding, renderer->globals_buffer);
		GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, renderer->globals_buffer);
		GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, renderer->globals_buffer);
		GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
		GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
		GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, renderer->sampler_percentage_closer);
		GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex0_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex1_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex2_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->tex3_binding, renderer->dummy_black);
		GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, renderer->dummy_black);

		GPU_FinalizeDescriptorSet(desc_set);
		renderer->lightgrid_sweep_desc_set = desc_set;
	}

	if (reloader->shader_is_outdated[ShaderAsset_GeometryPass]) {
		MainPassLayout* pass = &renderer->main_pass_layout;
		GPU_WaitUntilIdle();
		for (int i = 0; i < 2; i++) {
			GPU_DestroyGraphicsPipeline(renderer->geometry_pass_pipeline[i]);

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
			LoadVertexAndFragmentShader(TEMP, ShaderAsset_GeometryPass, pass->pipeline_layout, &vs_desc, &fs_desc);

			GPU_Format vertex_inputs[] = {
				GPU_Format_RGB32F,
				GPU_Format_RGB32F,
				GPU_Format_RGB32F,
				GPU_Format_RG32F,
			};

			GPU_GraphicsPipelineDesc desc = {};
			desc.layout = pass->pipeline_layout;
			desc.render_pass = renderer->geometry_render_pass[i];
			desc.vs = vs_desc;
			desc.fs = fs_desc;
			desc.vertex_input_formats = vertex_inputs;
			desc.vertex_input_formats_count = DS_ArrayCount(vertex_inputs);
			desc.enable_depth_test = true;
			desc.enable_depth_write = true;
			desc.cull_mode = GPU_CullMode_DrawCCW;
			renderer->geometry_pass_pipeline[i] = GPU_MakeGraphicsPipeline(&desc);
		}
	}

	if (reloader->shader_is_outdated[ShaderAsset_LightingPass]) {
		LightingPassLayout* pass = &renderer->lighting_pass_layout;
		GPU_WaitUntilIdle();
		GPU_DestroyGraphicsPipeline(renderer->lighting_pass_pipeline);

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

		LoadVertexAndFragmentShader(TEMP, ShaderAsset_LightingPass, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_GraphicsPipelineDesc desc = {};
		desc.layout = pass->pipeline_layout;
		desc.render_pass = renderer->lighting_render_pass;
		desc.vs = vs_desc;
		desc.fs = fs_desc;
		renderer->lighting_pass_pipeline = GPU_MakeGraphicsPipeline(&desc);
	}

	if (reloader->shader_is_outdated[ShaderAsset_TAAResolve]) {
		MainPassLayout* pass = &renderer->main_pass_layout; // let's use the main pass layout for now.
		GPU_WaitUntilIdle();

		for (int i = 0; i < 2; i++) {
			GPU_DestroyGraphicsPipeline(renderer->taa_resolve_pipeline[i]);
			GPU_DestroyDescriptorSet(renderer->taa_resolve_descriptor_set[i]);

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
			LoadVertexAndFragmentShader(TEMP, ShaderAsset_TAAResolve, pass->pipeline_layout, &vs_desc, &fs_desc);

			GPU_GraphicsPipelineDesc desc = {};
			desc.layout = pass->pipeline_layout;
			desc.render_pass = renderer->taa_resolve_render_pass[i];
			desc.vs = vs_desc;
			desc.fs = fs_desc;
			renderer->taa_resolve_pipeline[i] = GPU_MakeGraphicsPipeline(&desc);

			GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
			GPU_SetBufferBinding(desc_set, pass->globals_binding, renderer->globals_buffer);
			GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
			GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
			GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, renderer->sampler_percentage_closer);
			GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, renderer->taa_output_rt[1 - i]);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, renderer->gbuffer_depth[i]);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, renderer->gbuffer_depth[1 - i]);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, renderer->gbuffer_velocity[i]);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, renderer->gbuffer_velocity[1 - i]);
			GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, renderer->lighting_result_rt);

			// ... unused descriptors. This is stupid.
			GPU_SetStorageImageBinding(desc_set, pass->img0_binding, renderer->lightgrid, 0);
			//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, renderer->lightgrid, 0);
			GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, renderer->globals_buffer);
			GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, renderer->globals_buffer);
			GPU_SetTextureBinding(desc_set, pass->tex0_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex1_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex2_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex3_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, renderer->dummy_black);

			GPU_FinalizeDescriptorSet(desc_set);
			renderer->taa_resolve_descriptor_set[i] = desc_set;
		}
	}

	if (reloader->shader_is_outdated[ShaderAsset_BloomDownsample]) {
		MainPassLayout* pass = &renderer->main_pass_layout; // let's use the main pass layout for now.
		GPU_WaitUntilIdle();

		for (uint32_t step = 0; step < BLOOM_PASS_COUNT; step++) {

			GPU_Access fs_accesses[] = {GPU_Read(pass->tex0_binding), GPU_Read(pass->sampler_linear_clamp_binding)};
			GPU_ShaderDesc vs_desc = {};
			GPU_ShaderDesc fs_desc = {};
			fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
			LoadVertexAndFragmentShader(TEMP, ShaderAsset_BloomDownsample, pass->pipeline_layout, &vs_desc, &fs_desc);

			for (int i = 0; i < 2; i++) {
				GPU_DestroyGraphicsPipeline(renderer->bloom_downsamples[step].pipeline[i]);

				// In pure vulkan since we have the concept of framebuffers, we would only need one pipeline here...
				GPU_GraphicsPipelineDesc desc = {};
				desc.layout = pass->pipeline_layout;
				desc.render_pass = renderer->bloom_downsamples[step].render_pass[i];
				desc.vs = vs_desc;
				desc.fs = fs_desc;
				renderer->bloom_downsamples[step].pipeline[i] = GPU_MakeGraphicsPipeline(&desc);

				GPU_DestroyDescriptorSet(renderer->bloom_downsamples[step].desc_set[i]);
				GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);

				if (step == 0) {
					GPU_SetTextureBinding(desc_set, pass->tex0_binding, renderer->taa_output_rt[i]);
				} else {
					GPU_SetTextureMipBinding(desc_set, pass->tex0_binding, renderer->bloom_downscale_rt, step-1);
				}

				GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());

				// ... unused descriptors. This is stupid.
				GPU_SetStorageImageBinding(desc_set, pass->img0_binding, renderer->lightgrid, 0);
				//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, renderer->lightgrid, 0);
				GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, renderer->globals_buffer);
				GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, renderer->globals_buffer);
				GPU_SetBufferBinding(desc_set, pass->globals_binding, renderer->globals_buffer);
				GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
				GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, renderer->sampler_percentage_closer);
				GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex1_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex2_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex3_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, renderer->dummy_black);
				GPU_FinalizeDescriptorSet(desc_set);
				renderer->bloom_downsamples[step].desc_set[i] = desc_set;
			}
		}
	}

	if (reloader->shader_is_outdated[ShaderAsset_BloomUpsample]) {
		MainPassLayout* pass = &renderer->main_pass_layout; // let's use the main pass layout for now.
		GPU_WaitUntilIdle();

		GPU_Access fs_accesses[] = {GPU_Read(pass->tex0_binding), GPU_Read(pass->sampler_linear_clamp_binding)};
		GPU_ShaderDesc vs_desc = {};
		GPU_ShaderDesc fs_desc = {};
		fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
		LoadVertexAndFragmentShader(TEMP, ShaderAsset_BloomUpsample, pass->pipeline_layout, &vs_desc, &fs_desc);

		for (uint32_t step = 0; step < BLOOM_PASS_COUNT; step++) {
			for (int i = 0; i < 2; i++) {
				GPU_DestroyGraphicsPipeline(renderer->bloom_upsamples[step].pipeline[i]);
				GPU_GraphicsPipelineDesc desc = {};
				desc.layout = pass->pipeline_layout;
				desc.render_pass = renderer->bloom_upsamples[step].render_pass[i];
				desc.vs = vs_desc;
				desc.fs = fs_desc;
				desc.enable_blending = true;
				desc.blending_mode_additive = true;
				renderer->bloom_upsamples[step].pipeline[i] = GPU_MakeGraphicsPipeline(&desc);

				GPU_DestroyDescriptorSet(renderer->bloom_upsamples[step].desc_set[i]);
				GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);

				if (step == 0) {
					GPU_SetTextureMipBinding(desc_set, pass->tex0_binding, renderer->bloom_downscale_rt, BLOOM_PASS_COUNT-1);
				}
				else {
					GPU_SetTextureMipBinding(desc_set, pass->tex0_binding, renderer->bloom_upscale_rt, BLOOM_PASS_COUNT-step);
				}
				GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());

				// ... unused descriptors. This is stupid.
				GPU_SetStorageImageBinding(desc_set, pass->img0_binding, renderer->lightgrid, 0);
				//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, renderer->lightgrid, 0);
				GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, renderer->globals_buffer);
				GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, renderer->globals_buffer);
				GPU_SetBufferBinding(desc_set, pass->globals_binding, renderer->globals_buffer);
				GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
				GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, renderer->sampler_percentage_closer);
				GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex1_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex2_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->tex3_binding, renderer->dummy_black);
				GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, renderer->dummy_black);
				GPU_FinalizeDescriptorSet(desc_set);
				renderer->bloom_upsamples[step].desc_set[i] = desc_set;
			}
		}
	}

	if (reloader->shader_is_outdated[ShaderAsset_FinalPostProcess]) {
		MainPassLayout* pass = &renderer->main_pass_layout; // let's use the main pass layout for now.
		GPU_WaitUntilIdle();

		GPU_DestroyGraphicsPipeline(renderer->final_post_process_pipeline);

		GPU_Access fs_accesses[] = {GPU_Read(pass->tex0_binding), GPU_Read(pass->sampler_linear_clamp_binding)};
		GPU_ShaderDesc vs_desc = {};
		GPU_ShaderDesc fs_desc = {};
		fs_desc.accesses = fs_accesses; fs_desc.accesses_count = DS_ArrayCount(fs_accesses);
		LoadVertexAndFragmentShader(TEMP, ShaderAsset_FinalPostProcess, pass->pipeline_layout, &vs_desc, &fs_desc);

		GPU_GraphicsPipelineDesc desc = {};
		desc.layout = pass->pipeline_layout;
		desc.render_pass = renderer->final_post_process_render_pass;
		desc.vs = vs_desc;
		desc.fs = fs_desc;
		renderer->final_post_process_pipeline = GPU_MakeGraphicsPipeline(&desc);

		for (int i = 0; i < 2; i++) {
			GPU_DestroyDescriptorSet(renderer->final_post_process_desc_set[i]);
			GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pass->pipeline_layout);
			GPU_SetTextureBinding(desc_set, pass->tex0_binding, renderer->bloom_upscale_rt);
			GPU_SetSamplerBinding(desc_set, pass->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());

			// ... unused descriptors. This is stupid.
			GPU_SetStorageImageBinding(desc_set, pass->img0_binding, renderer->lightgrid, 0);
			//GPU_SetStorageImageBinding(desc_set, pass->img0_binding_uint, renderer->lightgrid, 0);
			GPU_SetBufferBinding(desc_set, pass->ssbo0_binding, renderer->globals_buffer);
			GPU_SetBufferBinding(desc_set, pass->ssbo1_binding, renderer->globals_buffer);
			GPU_SetBufferBinding(desc_set, pass->globals_binding, renderer->globals_buffer);
			GPU_SetSamplerBinding(desc_set, pass->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
			GPU_SetSamplerBinding(desc_set, pass->sampler_percentage_closer, renderer->sampler_percentage_closer);
			GPU_SetTextureBinding(desc_set, pass->prev_frame_result_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_depth_prev_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->gbuffer_velocity_prev_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->lighting_result_rt, renderer->lighting_result_rt);
			GPU_SetTextureBinding(desc_set, pass->tex1_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex2_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->tex3_binding, renderer->dummy_black);
			GPU_SetTextureBinding(desc_set, pass->sun_depth_map_binding_, renderer->dummy_black);

			GPU_FinalizeDescriptorSet(desc_set);
			renderer->final_post_process_desc_set[i] = desc_set;
		}
	}

	if (reloader->shader_is_outdated[ShaderAsset_GenIrradianceMap]) {
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
		ctx.pipeline = LoadComputeShader(ShaderAsset_GenIrradianceMap, ctx.pipeline_layout, &cs_desc);

		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, ctx.pipeline_layout);
		GPU_SetSamplerBinding(desc_set, ctx.sampler_binding, GPU_SamplerLinearClamp());
		GPU_SetTextureBinding(desc_set, ctx.tex_env_cube_binding, renderer->tex_env_cube);
		GPU_SetStorageImageBinding(desc_set, ctx.output_binding, renderer->irradiance_map, 0);
		GPU_FinalizeDescriptorSet(desc_set);

		GPU_Graph* graph = GPU_MakeGraph(); // TODO: merge all graphs that should execute within this UpdateScene function into one.
		GPU_OpBindComputePipeline(graph, ctx.pipeline);
		GPU_OpBindComputeDescriptorSet(graph, desc_set);

		GPU_OpDispatch(graph, renderer->irradiance_map->width / 8, renderer->irradiance_map->height / 8, 1);
		GPU_GraphSubmit(graph);
		GPU_GraphWait(graph);
		GPU_DestroyGraph(graph);
		GPU_DestroyDescriptorSet(desc_set);
	}

	if (reloader->shader_is_outdated[ShaderAsset_GenPrefilteredEnvMap]) {
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
		ctx.pipeline = LoadComputeShader(ShaderAsset_GenPrefilteredEnvMap, ctx.pipeline_layout, &cs_desc);

		GPU_Graph* graph = GPU_MakeGraph();
		GPU_OpBindComputePipeline(graph, ctx.pipeline);

		GPU_DescriptorArena* descriptor_arena = GPU_MakeDescriptorArena();

		uint32_t size = renderer->tex_specular_env_map->width;
		for (uint32_t i = 0; i < renderer->tex_specular_env_map->mip_level_count; i++) {
			if (size < 16) break; // stop at 16x16

			GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(descriptor_arena, ctx.pipeline_layout);
			GPU_SetSamplerBinding(desc_set, ctx.sampler_binding, GPU_SamplerLinearClamp());
			GPU_SetTextureBinding(desc_set, ctx.tex_env_cube_binding, renderer->tex_env_cube);
			GPU_SetStorageImageBinding(desc_set, ctx.output_binding, renderer->tex_specular_env_map, i);
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

	if (reloader->shader_is_outdated[ShaderAsset_GenBRDFIntegrationMap]) {
		GenBRDFintegrationMapCtx ctx = {};

		ctx.pipeline_layout = GPU_InitPipelineLayout();
		ctx.output_binding = GPU_StorageImageBinding(ctx.pipeline_layout, "OUTPUT", GPU_Format_RG16F);
		GPU_FinalizePipelineLayout(ctx.pipeline_layout);

		GPU_Access cs_accesses[] = {GPU_Write(ctx.output_binding)};
		GPU_ShaderDesc cs_desc = {};
		cs_desc.accesses = cs_accesses; cs_desc.accesses_count = DS_ArrayCount(cs_accesses);
		ctx.pipeline = LoadComputeShader(ShaderAsset_GenBRDFIntegrationMap, ctx.pipeline_layout, &cs_desc);

		GPU_Graph* graph = GPU_MakeGraph();

		GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, ctx.pipeline_layout);
		GPU_SetStorageImageBinding(desc_set, ctx.output_binding, renderer->brdf_lut, 0);
		GPU_FinalizeDescriptorSet(desc_set);

		GPU_OpBindComputePipeline(graph, ctx.pipeline);
		GPU_OpBindComputeDescriptorSet(graph, desc_set);

		GPU_OpDispatch(graph, 256 / 8, 256 / 8, 1);

		GPU_GraphSubmit(graph);
		GPU_GraphWait(graph);
		GPU_DestroyGraph(graph);
		GPU_DestroyDescriptorSet(desc_set);
	}

	// Finally, tell the hotreloader that we have checked everything
	for (int i = 0; i < ShaderAsset_COUNT; i++) {
		reloader->shader_is_outdated[i] = false;
	}

	DS_ArenaSetMark(TEMP, T);
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

void InitRenderer(Renderer* renderer, uint32_t window_width, uint32_t window_height, GPU_Texture* tex_env_cube) {
	renderer->window_width = window_width;
	renderer->window_height = window_height;
	renderer->tex_env_cube = tex_env_cube;
	
	for (int i = 0; i < ShaderAsset_COUNT; i++) {
		renderer->shader_hotreloader.shader_is_outdated[i] = true;
	}
	
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
		renderer->sampler_percentage_closer = GPU_MakeSampler(&sampler_pcf_desc);

		renderer->globals_buffer = GPU_MakeBuffer(sizeof(RendererGlobalsBuffer), GPU_BufferFlag_CPU|GPU_BufferFlag_GPU|GPU_BufferFlag_StorageBuffer, NULL);
		
		renderer->sun_depth_rt       = GPU_MakeTexture(GPU_Format_D32F_Or_X8D24UN, 2048, 2048, 1, GPU_TextureFlag_RenderTarget, NULL);
		renderer->lightgrid          = GPU_MakeTexture(GPU_Format_RGBA16F, LIGHTGRID_SIZE, LIGHTGRID_SIZE, LIGHTGRID_SIZE, GPU_TextureFlag_StorageImage, NULL);

		renderer->gbuffer_base_color = GPU_MakeTexture(GPU_Format_RGBA8UN, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);
		renderer->gbuffer_normal     = GPU_MakeTexture(GPU_Format_RGBA8UN, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);
		renderer->gbuffer_orm        = GPU_MakeTexture(GPU_Format_RGBA8UN, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);
		renderer->gbuffer_emissive   = GPU_MakeTexture(GPU_Format_RGBA8UN, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);

		// We keep track of the previous frame's velocity buffer for depth/location based rejection
		renderer->gbuffer_depth[0]   = GPU_MakeTexture(GPU_Format_D32F_Or_X8D24UN, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);
		renderer->gbuffer_depth[1]   = renderer->gbuffer_depth[0]; // for now, lets not do depth based rejection. // GPU_MakeTexture(GPU_Format_D32F_Or_X8D24UN, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);
		
		// We keep track of the previous frame's velocity buffer for velocity-based rejection
		renderer->gbuffer_velocity[0] = GPU_MakeTexture(GPU_Format_RG16F, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);
		renderer->gbuffer_velocity[1] = GPU_MakeTexture(GPU_Format_RG16F, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);

		renderer->lighting_result_rt = GPU_MakeTexture(GPU_Format_RGBA16F, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);

		// in the TAA resolve, we need to be writing to one resulting RT and read from the previous frame's result, so we need two RTs.
		renderer->taa_output_rt[0] = GPU_MakeTexture(GPU_Format_RGBA16F, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);
		renderer->taa_output_rt[1] = GPU_MakeTexture(GPU_Format_RGBA16F, window_width, window_height, 1, GPU_TextureFlag_RenderTarget, NULL);

		for (int i = 0; i < 2; i++) {
			GPU_TextureView geometry_color_targets[] = {{renderer->gbuffer_base_color}, {renderer->gbuffer_normal}, {renderer->gbuffer_orm}, {renderer->gbuffer_emissive}, {renderer->gbuffer_velocity[i]}};
			
			GPU_RenderPassDesc desc = {};
			desc.width = window_width;
			desc.height = window_height;
			desc.color_targets = geometry_color_targets;
			desc.color_targets_count = DS_ArrayCount(geometry_color_targets);
			desc.depth_stencil_target = renderer->gbuffer_depth[i];
			renderer->geometry_render_pass[i] = GPU_MakeRenderPass(&desc);
		}
		
		GPU_RenderPassDesc lightgrid_voxelize_pass_desc = {};
		lightgrid_voxelize_pass_desc.width = LIGHTGRID_SIZE;
		lightgrid_voxelize_pass_desc.height = LIGHTGRID_SIZE;
		renderer->lightgrid_voxelize_render_pass = GPU_MakeRenderPass(&lightgrid_voxelize_pass_desc);

		GPU_TextureView lighting_color_targets[] = {{renderer->lighting_result_rt}};
		
		GPU_RenderPassDesc lighting_pass_desc = {};
		lighting_pass_desc.width = window_width;
		lighting_pass_desc.height = window_height;
		lighting_pass_desc.color_targets = lighting_color_targets;
		lighting_pass_desc.color_targets_count = DS_ArrayCount(lighting_color_targets);
		renderer->lighting_render_pass = GPU_MakeRenderPass(&lighting_pass_desc);
		
		GPU_RenderPassDesc sun_render_pass_desc = {};
		sun_render_pass_desc.width = renderer->sun_depth_rt->width;
		sun_render_pass_desc.height = renderer->sun_depth_rt->height;
		sun_render_pass_desc.depth_stencil_target = renderer->sun_depth_rt;
		renderer->sun_depth_render_pass = GPU_MakeRenderPass(&sun_render_pass_desc);

		for (int i = 0; i < 2; i++) {
			GPU_TextureView taa_resolve_color_targets[] = {renderer->taa_output_rt[i]};
			
			GPU_RenderPassDesc resolve_pass_desc = {};
			resolve_pass_desc.width = window_width;
			resolve_pass_desc.height = window_height;
			resolve_pass_desc.color_targets = taa_resolve_color_targets;
			resolve_pass_desc.color_targets_count = DS_ArrayCount(taa_resolve_color_targets);
			renderer->taa_resolve_render_pass[i] = GPU_MakeRenderPass(&resolve_pass_desc);
		}

		renderer->bloom_downscale_rt = GPU_MakeTexture(GPU_Format_RGBA16F, window_width/2, window_height/2, 1,
			GPU_TextureFlag_RenderTarget|GPU_TextureFlag_HasMipmaps|GPU_TextureFlag_PerMipBinding, NULL);

		renderer->bloom_upscale_rt = GPU_MakeTexture(GPU_Format_RGBA16F, window_width, window_height, 1,
			GPU_TextureFlag_RenderTarget|GPU_TextureFlag_HasMipmaps|GPU_TextureFlag_PerMipBinding, NULL);
		
		for (int i = 0; i < 2; i++) {
			uint32_t width = window_width, height = window_height;

			for (int step = 0; step < BLOOM_PASS_COUNT; step++) {
				GPU_TextureView bloom_downsample_color_targets[] = {{renderer->bloom_downscale_rt, (uint32_t)step}};
				
				width /= 2;
				height /= 2;

				GPU_RenderPassDesc pass_desc = {};
				pass_desc.width = width;
				pass_desc.height = height;
				pass_desc.color_targets = bloom_downsample_color_targets;
				pass_desc.color_targets_count = DS_ArrayCount(bloom_downsample_color_targets);
				renderer->bloom_downsamples[step].render_pass[i] = GPU_MakeRenderPass(&pass_desc);
			}

			width = window_width, height = window_height;
			for (int step = BLOOM_PASS_COUNT - 1; step >= 0; step--) {
				uint32_t dst_level = BLOOM_PASS_COUNT - 1 - (uint32_t)step;
				GPU_TextureView bloom_upsample_color_targets[] = {{renderer->bloom_upscale_rt, dst_level}};

				GPU_RenderPassDesc pass_desc = {};
				pass_desc.width = width;
				pass_desc.height = height;
				pass_desc.color_targets = bloom_upsample_color_targets;
				pass_desc.color_targets_count = DS_ArrayCount(bloom_upsample_color_targets);
				renderer->bloom_upsamples[step].render_pass[i] = GPU_MakeRenderPass(&pass_desc);
				
				width /= 2;
				height /= 2;
			}
		}

		GPU_RenderPassDesc final_pp_pass_desc = {};
		final_pp_pass_desc.color_targets = GPU_SWAPCHAIN_COLOR_TARGET;
		final_pp_pass_desc.color_targets_count = 1;
		renderer->final_post_process_render_pass = GPU_MakeRenderPass(&final_pp_pass_desc);

		uint32_t normal_up = 0xFFFF7F7F;
		uint32_t black = 0x00000000;
		uint32_t white = 0xFFFFFFFF;

		renderer->dummy_normal_map = GPU_MakeTexture(GPU_Format_RGBA8UN, 1, 1, 1, 0, &normal_up);
		renderer->dummy_black = GPU_MakeTexture(GPU_Format_RGBA8UN, 1, 1, 1, 0, &black);
		renderer->dummy_white = GPU_MakeTexture(GPU_Format_RGBA8UN, 1, 1, 1, 0, &white);
		renderer->irradiance_map = GPU_MakeTexture(GPU_Format_RGBA32F, 32, 32, 1, GPU_TextureFlag_Cubemap|GPU_TextureFlag_StorageImage, NULL);
		renderer->brdf_lut = GPU_MakeTexture(GPU_Format_RG16F, 256, 256, 1, GPU_TextureFlag_StorageImage, NULL);
		renderer->tex_specular_env_map = GPU_MakeTexture(GPU_Format_RGBA32F, 256, 256, 1, GPU_TextureFlag_Cubemap|GPU_TextureFlag_HasMipmaps|GPU_TextureFlag_StorageImage, NULL);

		{
			MainPassLayout* lo = &renderer->main_pass_layout;
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
			lo->img0_binding = GPU_StorageImageBinding(lo->pipeline_layout, "IMG0", renderer->lightgrid->format);
			
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
			LightingPassLayout* lo = &renderer->lighting_pass_layout;
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
				GPU_SetBufferBinding(desc_set, lo->globals_binding, renderer->globals_buffer);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_base_color_binding, renderer->gbuffer_base_color);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_normal_binding, renderer->gbuffer_normal);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_orm_binding, renderer->gbuffer_orm);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_emissive_binding, renderer->gbuffer_emissive);
				GPU_SetTextureBinding(desc_set, lo->gbuffer_depth_binding, renderer->gbuffer_depth[i]);
				GPU_SetTextureBinding(desc_set, lo->tex_irradiance_map_binding, renderer->irradiance_map);
				GPU_SetTextureBinding(desc_set, lo->prefiltered_env_map_binding, renderer->tex_specular_env_map);
				GPU_SetTextureBinding(desc_set, lo->brdf_integration_map_binding, renderer->brdf_lut);
				GPU_SetTextureBinding(desc_set, lo->lightgrid_binding, renderer->lightgrid);
				GPU_SetTextureBinding(desc_set, lo->prev_frame_result_binding, renderer->bloom_downscale_rt);
				GPU_SetTextureBinding(desc_set, lo->sun_depth_map_binding, renderer->sun_depth_rt);
				GPU_SetSamplerBinding(desc_set, lo->sampler_linear_clamp_binding, GPU_SamplerLinearClamp());
				GPU_SetSamplerBinding(desc_set, lo->sampler_linear_wrap_binding, GPU_SamplerLinearWrap());
				GPU_SetSamplerBinding(desc_set, lo->sampler_nearest_clamp_binding, GPU_SamplerNearestClamp());
				GPU_SetSamplerBinding(desc_set, lo->sampler_percentage_closer, renderer->sampler_percentage_closer);
				GPU_FinalizeDescriptorSet(desc_set);
				renderer->lighting_pass_descriptor_set[i] = desc_set;
			}
		}
	}
}

void BuildRenderCommands(Renderer* renderer, GPU_Graph* graph, GPU_Texture* backbuffer, RenderObject* ro_world, RenderObject* ro_skybox, GPU_Texture* tex_env_cube, const Camera& camera, HMM_Vec2 sun_angle)
{
	uint32_t frame_idx = renderer->frame_idx;
	uint32_t frame_idx_mod2 = frame_idx % 2;

	const float sun_half_size = 40.f;

	HMM_Mat4 sun_space_from_world;
	HMM_Vec3 sun_dir;
	{
		HMM_Mat4 sun_ori = HMM_Rotate_RH(HMM_AngleDeg(sun_angle.X), HMM_V3(cosf(HMM_AngleDeg(sun_angle.Y)), sinf(HMM_AngleDeg(sun_angle.Y)), 0.f));

		sun_space_from_world = HMM_InvGeneralM4(sun_ori);
		sun_space_from_world = HMM_MulM4(HMM_Orthographic_RH_ZO(-sun_half_size, sun_half_size, -sun_half_size, sun_half_size, -sun_half_size, sun_half_size), sun_space_from_world);

		sun_dir = HMM_MulM4V4(sun_ori, HMM_V4(0, 0, -1, 0)).XYZ;
	}
	
	HMM_Vec2 taa_jitter = r2_sequence((float)frame_idx);
	taa_jitter.X = (taa_jitter.X * 2.f - 1.f) / (float)renderer->window_width;
	taa_jitter.Y = (taa_jitter.Y * 2.f - 1.f) / (float)renderer->window_height;
	
	const float LIGHTGRID_EXTENT_WS = 40.f;

	RendererGlobalsBuffer globals;
	globals.clip_space_from_world = camera.clip_from_world;
	globals.clip_space_from_view = camera.clip_from_view;
	globals.world_space_from_clip = camera.world_from_clip;
	globals.view_space_from_clip = camera.view_from_clip;
	globals.view_space_from_world = camera.view_from_world;
	globals.world_space_from_view = camera.world_from_view;
	globals.sun_space_from_world = sun_space_from_world;
	globals.old_clip_space_from_world = frame_idx == 0 ? camera.clip_from_world : renderer->clip_space_from_world_prev_frame;
	globals.sun_direction.XYZ = sun_dir;
	globals.camera_pos = camera.lazy_pos;
	globals.frame_idx_mod_59 = (float)(frame_idx % 59);
	globals.lightgrid_scale = 1.f / LIGHTGRID_EXTENT_WS;
	globals.shift_is_held_down = false; // (float)Input_IsDown(&inputs, Input_Key_Alt);
	memcpy(renderer->globals_buffer->data, &globals, sizeof(globals));

	GPU_OpClearDepthStencil(graph, renderer->gbuffer_depth[frame_idx_mod2], GPU_MIP_LEVEL_ALL);

	//  -- Sun depth renderpass ------------------

	GPU_OpClearDepthStencil(graph, renderer->sun_depth_rt, GPU_MIP_LEVEL_ALL);

	GPU_OpPrepareRenderPass(graph, renderer->sun_depth_render_pass);

	DS_DynArray(uint32_t) sun_depth_pass_part_draw_params = {TEMP};
	for (int i = 0; i < ro_world->parts.count; i++) {
		RenderObjectPart* part = &ro_world->parts[i];

		uint32_t draw_params = GPU_OpPrepareDrawParams(graph, renderer->sun_depth_pipeline, part->descriptor_set);
		DS_ArrPush(&sun_depth_pass_part_draw_params, draw_params);
	}

	GPU_OpBeginRenderPass(graph);

	GPU_OpBindVertexBuffer(graph, ro_world->vertex_buffer);
	GPU_OpBindIndexBuffer(graph, ro_world->index_buffer);
	// GPU_OpPushGraphicsConstants(graph, &time, sizeof(time));

	for (int i = 0; i < ro_world->parts.count; i++) {
		RenderObjectPart* part = &ro_world->parts[i];
		GPU_OpBindDrawParams(graph, sun_depth_pass_part_draw_params[i]);
		GPU_OpDrawIndexed(graph, part->index_count, 1, part->first_index, 0, 0);
	}

	GPU_OpEndRenderPass(graph);

	// -- Voxelize pass -------------------------

	bool revoxelize = frame_idx == 0 || sun_angle != renderer->sun_angle_prev_frame;
	if (revoxelize) {

		if (frame_idx == 0) {
			GPU_OpClearColorF(graph, renderer->lightgrid, GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);

			// clear the initial feedback framebuffers
			GPU_OpClearDepthStencil(graph, renderer->gbuffer_depth[0], GPU_MIP_LEVEL_ALL);
			GPU_OpClearDepthStencil(graph, renderer->gbuffer_depth[1], GPU_MIP_LEVEL_ALL);
			GPU_OpClearColorF(graph, renderer->gbuffer_velocity[0], GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
			GPU_OpClearColorF(graph, renderer->gbuffer_velocity[1], GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
			GPU_OpClearColorF(graph, renderer->taa_output_rt[0], GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
			GPU_OpClearColorF(graph, renderer->taa_output_rt[1], GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);
		}

		GPU_OpPrepareRenderPass(graph, renderer->lightgrid_voxelize_render_pass);

		DS_DynArray(uint32_t) voxelize_pass_part_draw_paramss = {TEMP};
		for (int i = 0; i < ro_world->parts.count; i++) {
			RenderObjectPart* part = &ro_world->parts[i];
			uint32_t draw_params = GPU_OpPrepareDrawParams(graph, renderer->lightgrid_voxelize_pipeline, part->descriptor_set);
			DS_ArrPush(&voxelize_pass_part_draw_paramss, draw_params);
		}

		GPU_OpBeginRenderPass(graph);

		for (int i = 0; i < ro_world->parts.count; i++) {
			RenderObjectPart* part = &ro_world->parts[i];
			GPU_OpBindDrawParams(graph, voxelize_pass_part_draw_paramss[i]);
			GPU_OpDraw(graph, part->index_count, 1, part->first_index, 0);
		}

		GPU_OpEndRenderPass(graph);
	}

	//  -- Light grid sweep pass -----------------

	// So, let's spread the illumination. We need a cubemap for the sky, but for now lets say its all blue.
	// Next it'd be interesting to benchmark the timing of this dispatch and compare the speeds when doing it in X vs Y vs Z.

	renderer->sweep_direction++;
	if (renderer->sweep_direction == 3) renderer->sweep_direction = 0;

	GPU_OpBindComputePipeline(graph, renderer->lightgrid_sweep_pipeline);
	GPU_OpBindComputeDescriptorSet(graph, renderer->lightgrid_sweep_desc_set);
	GPU_OpPushComputeConstants(graph, renderer->main_pass_layout.pipeline_layout, &renderer->sweep_direction, sizeof(renderer->sweep_direction));
	GPU_OpDispatch(graph, 1, 16, 16); // 1*1, 16*8, 16*8 = (1, 128, 128)

	// -- Geometry pass -------------------------

	GPU_OpPrepareRenderPass(graph, renderer->geometry_render_pass[frame_idx_mod2]);
	
	DS_DynArray(uint32_t) geometry_pass_part_draw_params = {TEMP};
	for (int i = 0; i < ro_world->parts.count; i++) {
		RenderObjectPart* part = &ro_world->parts[i];
		uint32_t draw_params = GPU_OpPrepareDrawParams(graph, renderer->geometry_pass_pipeline[frame_idx_mod2], part->descriptor_set);
		DS_ArrPush(&geometry_pass_part_draw_params, draw_params);
	}

	GPU_OpBeginRenderPass(graph);

	{
		GPU_OpBindVertexBuffer(graph, ro_world->vertex_buffer);
		GPU_OpBindIndexBuffer(graph, ro_world->index_buffer);

		HMM_Vec2 constants[2];
		constants[0] = taa_jitter;
		constants[1] = renderer->taa_jitter_prev_frame;
		GPU_OpPushGraphicsConstants(graph, renderer->main_pass_layout.pipeline_layout, constants, sizeof(constants));

		for (int i = 0; i < ro_world->parts.count; i++) {
			RenderObjectPart* part = &ro_world->parts[i];
			GPU_OpBindDrawParams(graph, geometry_pass_part_draw_params[i]);
			GPU_OpDrawIndexed(graph, part->index_count, 1, part->first_index, 0, 0);
		}
	}

	// Draw skybox
	{
		GPU_OpBindVertexBuffer(graph, ro_skybox->vertex_buffer);
		GPU_OpBindIndexBuffer(graph, ro_skybox->index_buffer);

		for (int i = 0; i < ro_skybox->parts.count; i++) {
			RenderObjectPart* part = &ro_skybox->parts[i];
			GPU_OpBindDrawParams(graph, geometry_pass_part_draw_params[i]);
			GPU_OpDrawIndexed(graph, part->index_count, 1, part->first_index, 0, 0);
		}
	}

	GPU_OpEndRenderPass(graph);

	// -- Lighting pass -------------------------

	GPU_OpPrepareRenderPass(graph, renderer->lighting_render_pass);
	uint32_t lighting_pass_draw_params = GPU_OpPrepareDrawParams(graph, renderer->lighting_pass_pipeline, renderer->lighting_pass_descriptor_set[frame_idx_mod2]);
	GPU_OpBeginRenderPass(graph);

	GPU_OpBindDrawParams(graph, lighting_pass_draw_params);
	// GPU_OpPushGraphicsConstants(graph, renderer->lighting_pass_layout.pipeline_layout, &sun_jitter, sizeof(sun_jitter));
	GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

	GPU_OpEndRenderPass(graph);

	// -- TAA resolve pass -------------------------

	GPU_OpPrepareRenderPass(graph, renderer->taa_resolve_render_pass[frame_idx_mod2]);
	uint32_t taa_resolve_pass_draw_params = GPU_OpPrepareDrawParams(graph, renderer->taa_resolve_pipeline[frame_idx_mod2], renderer->taa_resolve_descriptor_set[frame_idx_mod2]);
	GPU_OpBeginRenderPass(graph);

	GPU_OpBindDrawParams(graph, taa_resolve_pass_draw_params);
	GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

	GPU_OpEndRenderPass(graph);

	// -- bloom downsample -------------------------

	for (uint32_t step = 0; step < BLOOM_PASS_COUNT; step++) {
		GPU_OpPrepareRenderPass(graph, renderer->bloom_downsamples[step].render_pass[frame_idx_mod2]);
		uint32_t draw_params = GPU_OpPrepareDrawParams(graph, renderer->bloom_downsamples[step].pipeline[frame_idx_mod2], renderer->bloom_downsamples[step].desc_set[frame_idx_mod2]);
		GPU_OpBeginRenderPass(graph);

		uint32_t dst_mip_level = step + 1;
		GPU_OpPushGraphicsConstants(graph, renderer->main_pass_layout.pipeline_layout, &dst_mip_level, sizeof(dst_mip_level));
		GPU_OpBindDrawParams(graph, draw_params);
		GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

		GPU_OpEndRenderPass(graph);
	}

	// -- bloom upsample ---------------------------

	GPU_OpClearColorF(graph, renderer->bloom_upscale_rt, GPU_MIP_LEVEL_ALL, 0.f, 0.f, 0.f, 0.f);

	GPU_OpBlitInfo blit = {};
	blit.src_area[1] = {(int)renderer->window_width, (int)renderer->window_height, 1};
	blit.dst_area[1] = {(int)renderer->window_width, (int)renderer->window_height, 1};
	blit.src_texture = renderer->taa_output_rt[frame_idx_mod2];
	blit.dst_texture = renderer->bloom_upscale_rt;
	GPU_OpBlit(graph, &blit);

	for (uint32_t step = 0; step < BLOOM_PASS_COUNT; step++) {
		GPU_OpPrepareRenderPass(graph, renderer->bloom_upsamples[step].render_pass[frame_idx_mod2]);
		uint32_t draw_params = GPU_OpPrepareDrawParams(graph, renderer->bloom_upsamples[step].pipeline[frame_idx_mod2], renderer->bloom_upsamples[step].desc_set[frame_idx_mod2]);
		GPU_OpBeginRenderPass(graph);

		uint32_t dst_mip_level = BLOOM_PASS_COUNT - step - 1;
		GPU_OpPushGraphicsConstants(graph, renderer->main_pass_layout.pipeline_layout, &dst_mip_level, sizeof(dst_mip_level));
		GPU_OpBindDrawParams(graph, draw_params);
		GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

		GPU_OpEndRenderPass(graph);
	}

	// -- Final post process ---------------------

	GPU_OpPrepareRenderPass(graph, renderer->final_post_process_render_pass);
	uint32_t final_pp_draw_params = GPU_OpPrepareDrawParams(graph, renderer->final_post_process_pipeline, renderer->final_post_process_desc_set[frame_idx_mod2]);
	GPU_OpBeginRenderPass(graph);

	GPU_OpBindDrawParams(graph, final_pp_draw_params);
	GPU_OpDraw(graph, 3, 1, 0, 0); // fullscreen triangle

	GPU_OpEndRenderPass(graph);

	// ---------------------------------------------

	renderer->taa_jitter_prev_frame = taa_jitter;
	renderer->frame_idx++;
	renderer->clip_space_from_world_prev_frame = camera.clip_from_world;
	renderer->sun_angle_prev_frame = sun_angle;
}
#define _CRT_SECURE_NO_WARNINGS

#include "src/fire/fire_ds.h"

#define FIRE_OS_WINDOW_IMPLEMENTATION
#include "src/fire/fire_os_window.h"

#include "src/gpu/gpu.h"

#define GPU_VALIDATION_ENABLED false

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include "src/gpu/gpu_vulkan.h"

#include <stdio.h>

#define GPU_STRING(x) GPU_String{(char*)x, sizeof(x)-1}

static const uint32_t window_w = 1200;
static const uint32_t window_h = 900;

struct Vertex {
	float x, y;
	float r, g, b;
};

static void ReadEntireFile(DS_Arena* arena, const char* file, char** out_data, uint32_t* out_size) {
	FILE* f = fopen(file, "rb");
	assert(f);

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char* data = DS_ArenaPush(arena, size);
	fread(data, size, 1, f);

	fclose(f);
	*out_data = data;
	*out_size = size;
}

int main() {
	DS_Arena persist;
	DS_ArenaInit(&persist, 4096*16, DS_HEAP);

	OS_WINDOW window = OS_WINDOW_Create(window_w, window_h, "Triangle");
	GPU_Init(window.handle);

	GPU_RenderPassDesc renderpass_desc{};
	renderpass_desc.color_targets = GPU_SWAPCHAIN_COLOR_TARGET;
	renderpass_desc.color_targets_count = 1;
	GPU_RenderPass* renderpass = GPU_MakeRenderPass(&renderpass_desc);

	GPU_String shader_path = GPU_STRING("../src/demo_triangle/triangle_shader.glsl");
	GPU_String shader_src;
	ReadEntireFile(&persist, shader_path.data, &shader_src.data, &shader_src.length);
	
	GPU_PipelineLayout* pipeline_layout = GPU_InitPipelineLayout();
	GPU_FinalizePipelineLayout(pipeline_layout);
	
	GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pipeline_layout);
	GPU_FinalizeDescriptorSet(desc_set);

	GPU_Format vertex_input_formats[] = {GPU_Format_RG32F, GPU_Format_RGB32F};
	GPU_GraphicsPipelineDesc pipeline_desc{};
	pipeline_desc.layout = pipeline_layout;
	pipeline_desc.render_pass = renderpass;
	pipeline_desc.vs.glsl = shader_src;
	pipeline_desc.vs.glsl_debug_filepath = shader_path;
	pipeline_desc.fs.glsl = shader_src;
	pipeline_desc.fs.glsl_debug_filepath = shader_path;
	pipeline_desc.vertex_input_formats = vertex_input_formats;
	pipeline_desc.vertex_input_formats_count = DS_ArrayCount(vertex_input_formats);
	GPU_GraphicsPipeline* pipeline = GPU_MakeGraphicsPipeline(&pipeline_desc);

	Vertex vertices[] = {
		{-0.5f, -0.5f,  1.f, 0.f, 0.f},
		{0.5f, -0.5f,   0.f, 1.f, 0.f},
		{0.f, 0.5f,     0.f, 0.f, 1.f}};
	uint32_t indices[] = {0, 1, 2};

	GPU_Buffer* vertex_buffer = GPU_MakeBuffer(sizeof(vertices), GPU_BufferFlag_GPU, vertices);
	GPU_Buffer* index_buffer = GPU_MakeBuffer(sizeof(indices), GPU_BufferFlag_GPU, indices);

	GPU_Graph* graphs[2];
	GPU_MakeSwapchainGraphs(2, &graphs[0]);
	int graph_idx = 0;

	for (;;) {
		OS_WINDOW_Event event;
		while (OS_WINDOW_PollEvent(&window, &event, NULL, NULL)) {}

		if (OS_WINDOW_ShouldClose(&window)) break;

		graph_idx = (graph_idx + 1) % 2;
		GPU_Graph* graph = graphs[graph_idx];
		GPU_GraphWait(graph);

		GPU_Texture* backbuffer = GPU_GetBackbuffer(graph);
		if (backbuffer) {
			GPU_OpClearColorF(graph, backbuffer, 0, 0.1f, 0.2f, 0.5f, 1.f);

			GPU_OpPrepareRenderPass(graph, renderpass);
			
			uint32_t draw_params = GPU_OpPrepareDrawParams(graph, pipeline, desc_set);
			
			GPU_OpBeginRenderPass(graph);
			
			GPU_OpBindDrawParams(graph, draw_params);
			GPU_OpBindIndexBuffer(graph, index_buffer);
			GPU_OpBindVertexBuffer(graph, vertex_buffer);
			GPU_OpDraw(graph, 3, 1, 0, 0);
			
			GPU_OpEndRenderPass(graph);
		
			GPU_GraphSubmit(graph);
		}
	}

	return 0;
}
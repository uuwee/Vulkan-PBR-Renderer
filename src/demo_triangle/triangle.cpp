#include <stdio.h>
#include "../fire/fire_ds.h"

#define FIRE_OS_WINDOW_IMPLEMENTATION
#include "../fire/fire_os_window.h"

#include "../gpu/gpu.h"

#define SHADER_PATH "../src/demo_triangle/triangle_shader.glsl"

struct Vertex {
	float x, y;
	float r, g, b;
};

static GPU_String ReadEntireFile(DS_Arena* arena, const char* file) {
	FILE* f = NULL;
	fopen_s(&f, file, "rb");
	assert(f != NULL);

	fseek(f, 0, SEEK_END);
	size_t size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);

	char* data = DS_ArenaPush(arena, size);
	fread(data, size, 1, f);

	fclose(f);

	GPU_String result = {data, size};
	return result;
}

int main() {
	DS_Arena persist;
	DS_ArenaInit(&persist, 4096*16, DS_HEAP);

	OS_Window window = OS_CreateWindow(800, 800, "Triangle");
	GPU_Init(window.handle);

	GPU_RenderPassDesc renderpass_desc{};
	renderpass_desc.color_targets = GPU_SWAPCHAIN_COLOR_TARGET;
	renderpass_desc.color_targets_count = 1;
	GPU_RenderPass* renderpass = GPU_MakeRenderPass(&renderpass_desc);

	GPU_String shader_source = ReadEntireFile(&persist, SHADER_PATH);
	
	GPU_PipelineLayout* pipeline_layout = GPU_InitPipelineLayout();
	GPU_FinalizePipelineLayout(pipeline_layout);
	
	GPU_DescriptorSet* desc_set = GPU_InitDescriptorSet(NULL, pipeline_layout);
	GPU_FinalizeDescriptorSet(desc_set);

	GPU_Format vertex_input_formats[] = {GPU_Format_RG32F, GPU_Format_RGB32F};
	GPU_GraphicsPipelineDesc pipeline_desc{};
	pipeline_desc.layout = pipeline_layout;
	pipeline_desc.render_pass = renderpass;
	pipeline_desc.vs.glsl = shader_source;
	pipeline_desc.vs.glsl_debug_filepath = GPU_STR(SHADER_PATH);
	pipeline_desc.fs.glsl = shader_source;
	pipeline_desc.fs.glsl_debug_filepath = GPU_STR(SHADER_PATH);
	pipeline_desc.vertex_input_formats = vertex_input_formats;
	pipeline_desc.vertex_input_formats_count = DS_ArrayCount(vertex_input_formats);
	GPU_GraphicsPipeline* pipeline = GPU_MakeGraphicsPipeline(&pipeline_desc);

	Vertex vertices[] = {
		{-0.5f, -0.5f,   1.f, 0.f, 0.f},
		{ 0.5f, -0.5f,   0.f, 1.f, 0.f},
		{ 0.0f,  0.5f,   0.f, 0.f, 1.f},
	};
	
	uint32_t indices[] = {0, 1, 2};

	GPU_Buffer* vertex_buffer = GPU_MakeBuffer(sizeof(vertices), GPU_BufferFlag_GPU, vertices);
	GPU_Buffer* index_buffer = GPU_MakeBuffer(sizeof(indices), GPU_BufferFlag_GPU, indices);

	GPU_Graph* graphs[2];
	GPU_MakeSwapchainGraphs(2, &graphs[0]);
	int graph_idx = 0;

	while (!OS_WindowShouldClose(&window)) {
		OS_Event event;
		while (OS_PollEvent(&window, &event, NULL, NULL)) {}

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

	GPU_WaitUntilIdle();
	GPU_DestroyGraph(graphs[0]);
	GPU_DestroyGraph(graphs[1]);

	GPU_DestroyBuffer(vertex_buffer);
	GPU_DestroyBuffer(index_buffer);

	GPU_DestroyGraphicsPipeline(pipeline);
	GPU_DestroyDescriptorSet(desc_set);
	GPU_DestroyPipelineLayout(pipeline_layout);
	GPU_DestroyRenderPass(renderpass);
	GPU_Deinit();

	DS_ArenaDeinit(&persist);
	return 0;
}
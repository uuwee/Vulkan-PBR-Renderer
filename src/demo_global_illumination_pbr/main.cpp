#include "common.h"
#include "render.h"
#include "asset_import.h"

#define FIRE_OS_WINDOW_IMPLEMENTATION
#include "src/fire/fire_os_window.h"
#include "src/utils/key_input/key_input_fire_os.h"

DS_Arena* TEMP; // Arena for per-frame, temporary allocations

int main() {
	DS_Arena frame_temp_arena;
	DS_ArenaInit(&frame_temp_arena, 4096, DS_HEAP);
	TEMP = &frame_temp_arena;

	const uint32_t window_width = 1920;
	const uint32_t window_height = 1080;

	OS_WINDOW window = OS_WINDOW_Create(window_width, window_height, "PBR Renderer");
	OS_WINDOW_SetFullscreen(&window, true);

	GPU_Init(window.handle);
	
	GPU_Texture* tex_env_cube = MakeTextureFromHDRIFile("../resources/shipyard_cranes_track_cube.hdr");

	Renderer renderer = {};
	InitRenderer(&renderer, window_width, window_height, tex_env_cube);
	
	Camera camera = {};
	camera.pos = {0.f, 5.f, 5.f};
	
	HMM_Vec2 sun_angle = {56.5f, 97.f};
	
	GPU_Graph* graphs[2];
	GPU_MakeSwapchainGraphs(2, &graphs[0]);
	int graph_idx = 0;

	RenderObject ro_world = LoadMesh(&renderer, "../resources/SunTemple/SunTemple.fbx");
	RenderObject ro_skybox = LoadMesh(&renderer, "../resources/Skybox_200x200x200.fbx");
	
	Input_Frame inputs = {};
	
	while (!OS_WINDOW_ShouldClose(&window)) {
		DS_ArenaReset(&frame_temp_arena);

		// process inputs
		{
			Input_OS_State input_os_state;
			Input_OS_BeginEvents(&input_os_state, &inputs, &frame_temp_arena);
			
			OS_WINDOW_Event event;
			while (OS_WINDOW_PollEvent(&window, &event, NULL, NULL)) {
				Input_OS_AddEvent(&input_os_state, &event);
			}
			
			Input_OS_EndEvents(&input_os_state);
		}
		
		bool revoxelize = true;
		if (Input_IsDown(&inputs, Input_Key_9)) { sun_angle.X -= 0.5f; revoxelize = true; }
		if (Input_IsDown(&inputs, Input_Key_0)) { sun_angle.X += 0.5f; revoxelize = true; }
		if (Input_IsDown(&inputs, Input_Key_8)) { sun_angle.Y -= 0.5f; revoxelize = true; }
		if (Input_IsDown(&inputs, Input_Key_7)) { sun_angle.Y += 0.5f; revoxelize = true; }
		
		HotreloadShaders(&renderer);

		float movement_speed = 0.05f;
		float mouse_speed = 0.001f;
		float FOV = 75.f;
		float z_near = 0.02f;
		float z_far = 10000.f;
		Camera_Update(&camera, &inputs, movement_speed, mouse_speed, FOV, (float)window_width / (float)window_height, z_near, z_far);
		
		graph_idx = (graph_idx + 1) % 2;
		GPU_Graph* graph = graphs[graph_idx];
		GPU_GraphWait(graph);

		GPU_Texture* backbuffer = GPU_GetBackbuffer(graph);

		if (backbuffer) {
			BuildRenderCommands(&renderer, graph, backbuffer, &ro_world, &ro_skybox, tex_env_cube, camera, sun_angle);
			GPU_GraphSubmit(graph);
		}
	}

	UnloadMesh(&ro_world);
	UnloadMesh(&ro_skybox);
	GPU_DestroyTexture(tex_env_cube);

	return 0;
}

#include "src/Fire/fire_build.h"
#define ArrCount(X) sizeof(X)/sizeof(X[0])

static void AddVulkanSDK(BUILD_Project* p, const char* vk_sdk) {
	BUILD_AddIncludeDir(p, BUILD_Concat2(p, vk_sdk, "/Include"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/vulkan-1.lib"));
	
	// If compiling with the GLSLang shader compiler, the following are required
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/GenericCodeGen.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/glslang.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/MachineIndependent.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/OGLCompiler.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/OSDependent.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/SPIRV.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/SPIRV-Tools.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/SPIRV-Tools-diff.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/SPIRV-Tools-link.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/SPIRV-Tools-lint.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/SPIRV-Tools-opt.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/SPIRV-Tools-reduce.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/SPIRV-Tools-shared.lib"));
	BUILD_AddLinkerInput(p, BUILD_Concat2(p, vk_sdk, "/Lib/glslang-default-resource-limits.lib"));
}

int main() {
	const char* vk_sdk = getenv("VULKAN_SDK");
	if (vk_sdk == NULL) {
		printf("ERROR: Vulkan SDK not found (\"VULKAN_SDK\" environment variable is missing).\n"
			"    Example projects which use Fire GPU won't be generated.");
		return 0;
	}
	
	BUILD_ProjectOptions opts = {
		.debug_info = true,
		.enable_optimizations = false,
		.c_runtime_library_dll = true, // glslang.lib uses /MD
	};
	
	BUILD_Project triangle;
	BUILD_InitProject(&triangle, "triangle", &opts);
	BUILD_AddIncludeDir(&triangle, "../"); // repository root dir
	BUILD_AddSourceFile(&triangle, "../src/demo_triangle/triangle.cpp");
	AddVulkanSDK(&triangle, vk_sdk);
	
	BUILD_Project pbr_rendering;
	BUILD_InitProject(&pbr_rendering, "pbr_rendering", &opts);
	BUILD_AddIncludeDir(&pbr_rendering, "../"); // repository root dir
	BUILD_AddSourceFile(&pbr_rendering, "../src/demo_pbr_rendering/pbr_rendering.cpp");
	AddVulkanSDK(&pbr_rendering, vk_sdk);

	BUILD_Project* projects[] = {&triangle, &pbr_rendering};
	BUILD_CreateDirectory("build");
	
	if (BUILD_CreateVisualStudioSolution("build", ".", "demos.sln", projects, ArrCount(projects), BUILD_GetConsole())) {
		printf("Project files were generated successfully! See the \"build\" folder.\n");
	}
	else {
		printf("ERROR: failed to generate project files.\n");
	}
	
	return 0;
}

workspace "VulkanRenderer"
	architecture "x64"
	configurations { "Debug", "Release" }
	location "build_projects"

project "PBR-Renderer"
	kind "ConsoleApp"
	language "C++"
	targetdir "build"
	
	-- /MD
	staticruntime "off"
	runtime "Release"

	includedirs { "src", "third_party" }
	
	files {
		"src/demo_pbr_renderer/**",
		"src/fire/**",
		"src/gpu/**",
		"src/utils/**",
		"third_party/**",
	}
	
	-- To enable vulkan validation layers, uncomment the following:
	-- defines "GPU_ENABLE_VALIDATION"
	
	-- Add vulkan SDK
	includedirs "%VULKAN_SDK%/Include"
	links "%VULKAN_SDK%/Lib/*"
	
	-- Add Assimp
	links "third_party/assimp/lib/assimp-vc143-mt.lib"
	postbuildcommands "copy \"..\\third_party\\assimp\\lib\\assimp-vc143-mt.dll\" ..\\build\\assimp-vc143-mt.dll"
	
	filter "configurations:Debug"
		symbols "On"

	filter "configurations:Release"
		optimize "On"

project "Triangle"
	kind "ConsoleApp"
	language "C++"
	targetdir "build"
	
	includedirs { "src", "third_party" }

	files {
		"src/demo_triangle/**",
		"src/fire/**",
		"src/gpu/**",
		"src/utils/**",
		"third_party/**",
	}
	
	-- To enable vulkan validation layers, uncomment the following:
	-- defines "GPU_ENABLE_VALIDATION"
	
	-- Add vulkan SDK
	includedirs "%VULKAN_SDK%/Include"
	links "%VULKAN_SDK%/Lib/*"
	
	filter "configurations:Debug"
		symbols "On"

	filter "configurations:Release"
		optimize "On"



function SpecifyWarnings()
	flags "FatalWarnings" -- treat all warnings as errors
	buildoptions "/w14062" -- error on unhandled enum members in switch cases
	buildoptions "/w14456" -- error on shadowed locals
	buildoptions "/wd4101" -- allow unused locals
	linkoptions "-IGNORE:4099" -- disable linker warning: "PDB was not found ...; linking object as if no debug info"
end

workspace "VulkanRenderer"
	architecture "x64"
	configurations { "Debug", "Release" }
	location "build_projects"

project "PBR-Renderer"
	kind "ConsoleApp"
	language "C++"
	targetdir "build"
	
	SpecifyWarnings()
	
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
	
	-- To enable vulkan validation, uncomment the following:
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
	
	SpecifyWarnings()
	
	-- /MD
	staticruntime "off"
	runtime "Release"
	
	includedirs { "src", "third_party" }

	files {
		"src/demo_triangle/**",
		"src/fire/**",
		"src/gpu/**",
		"src/utils/**",
		"third_party/**",
	}
	
	-- To enable vulkan validation, uncomment the following:
	-- defines "GPU_ENABLE_VALIDATION"
	
	-- Add vulkan SDK
	includedirs "%VULKAN_SDK%/Include"
	links "%VULKAN_SDK%/Lib/*"
	
	filter "configurations:Debug"
		symbols "On"

	filter "configurations:Release"
		optimize "On"


# Place executables and shared libs under "build-dir/",
# instead of under "build-dir/rts/"
# This way, we have the build-dir structure more like the install-dir one,
# which makes testing spring in the builddir easier, eg. like this:
# cd build-dir
# SPRING_DATADIR=$(pwd) ./spring
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

add_library(Game STATIC)
target_sources(Game PRIVATE ${sources_engine_Game})
add_dependencies(Game generateVersionFiles)
target_include_directories(Game PRIVATE "${CMAKE_BINARY_DIR}/src-generated/engine")
target_link_libraries(Game PRIVATE
	 Tracy::TracyClient
	 SDL2::SDL2
	 RmlUi::RmlUi
	 ${sound-impl}
	 prd::jsoncpp
	 streflop
	 fmt::fmt
)

### Assemble libraries
find_package(SDL2 MODULE REQUIRED)





set(engineLibraries SDL2::SDL2)

set(OpenGL_GL_PREFERENCE LEGACY)
find_package_static(OpenGL 3.0 REQUIRED)
list(APPEND engineLibraries OpenGL::GL)

if(ENABLE_VULKAN)
	find_package(Vulkan 1.1 REQUIRED)
	find_program(VULKAN_GLSLANG_VALIDATOR
		NAMES glslangValidator
		HINTS "$ENV{VULKAN_SDK}/Bin"
		REQUIRED
	)
	list(APPEND engineLibraries Vulkan::Vulkan)
endif()

find_fontconfig_hack()
find_package_static(Fontconfig 2.11 REQUIRED)
list(APPEND engineLibraries Fontconfig::Fontconfig)
add_definitions(-DUSE_FONTCONFIG)

if (NOT WIN32)
	find_package_static(EXPAT 2.2.5 REQUIRED)
	list(APPEND engineLibraries EXPAT::EXPAT)
endif()

# !!! If you change freetype resolution here, change it also in rts/lib
find_freetype_hack() # hack to find different named freetype.dll
find_package_static(Freetype 2.8.1 REQUIRED)
list(APPEND engineLibraries Freetype::Freetype)

if    (UNIX AND NOT APPLE)
	find_package(X11 REQUIRED)
	target_link_libraries(Game PRIVATE X11::Xcursor)
	list(APPEND engineLibraries ${X11_Xcursor_LIB} ${X11_X11_LIB})
endif (UNIX)

if (APPLE)
	find_library(COREFOUNDATION_LIBRARY Foundation)
	list(APPEND engineLibraries ${COREFOUNDATION_LIBRARY})
endif (APPLE)

list(APPEND engineLibraries squish rgetc1)
list(APPEND engineLibraries ${sound-impl})
list(APPEND engineLibraries engineSystemNet)
list(APPEND engineLibraries ${engineCommonLibraries})
#list(APPEND engineLibraries engineaGui)
list(APPEND engineLibraries ${SPRING_SIM_LIBRARIES})
list(APPEND engineLibraries engineSim)
list(APPEND engineLibraries pr-downloader)
list(APPEND engineLibraries RmlUi::Core)
list(APPEND engineLibraries RmlUi::Debugger)
list(APPEND engineLibraries lunasvg)
list(APPEND engineLibraries glad)

### Assemble external include dirs
list(APPEND engineIncludes ${ENGINE_SRC_ROOT_DIR}/lib/asio/include)
list(APPEND engineIncludes ${ENGINE_SRC_ROOT_DIR}/lib/slimsig/include)
list(APPEND engineIncludes ${ENGINE_SRC_ROOT_DIR}/lib/cereal/include)

include_directories(${engineIncludes})

### Build the executable
add_executable(engine-legacy ${EXE_FLAGS} ${engineSources} ${ENGINE_ICON} ${engineHeaders})
target_link_libraries(engine-legacy ${engineLibraries} Game)

if(ENABLE_VULKAN)
	set(vulkanShaderSourceDir "${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/Shaders")
	set(vulkanShaderBuildDir "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders")
	set(vulkanVertexShaderHeader "${vulkanShaderBuildDir}/Texture.vert.spv.h")
	set(vulkanFragmentShaderHeader "${vulkanShaderBuildDir}/Texture.frag.spv.h")
	set(vulkanTerrainVertexShaderHeader "${vulkanShaderBuildDir}/Terrain.vert.spv.h")
	set(vulkanTerrainFragmentShaderHeader "${vulkanShaderBuildDir}/Terrain.frag.spv.h")

	add_custom_command(
		OUTPUT "${vulkanVertexShaderHeader}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${vulkanShaderBuildDir}"
		COMMAND "${VULKAN_GLSLANG_VALIDATOR}"
			-V
			--target-env vulkan1.1
			-x
			--vn RECOIL_VULKAN_TEXTURE_VERT_SPV
			-o "${vulkanVertexShaderHeader}"
			"${vulkanShaderSourceDir}/Texture.vert"
		DEPENDS "${vulkanShaderSourceDir}/Texture.vert"
		VERBATIM
	)
	add_custom_command(
		OUTPUT "${vulkanFragmentShaderHeader}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${vulkanShaderBuildDir}"
		COMMAND "${VULKAN_GLSLANG_VALIDATOR}"
			-V
			--target-env vulkan1.1
			-x
			--vn RECOIL_VULKAN_TEXTURE_FRAG_SPV
			-o "${vulkanFragmentShaderHeader}"
			"${vulkanShaderSourceDir}/Texture.frag"
		DEPENDS "${vulkanShaderSourceDir}/Texture.frag"
		VERBATIM
	)
	add_custom_command(
		OUTPUT "${vulkanTerrainVertexShaderHeader}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${vulkanShaderBuildDir}"
		COMMAND "${VULKAN_GLSLANG_VALIDATOR}"
			-V
			--target-env vulkan1.1
			-x
			--vn RECOIL_VULKAN_TERRAIN_VERT_SPV
			-o "${vulkanTerrainVertexShaderHeader}"
			"${vulkanShaderSourceDir}/Terrain.vert"
		DEPENDS "${vulkanShaderSourceDir}/Terrain.vert"
		VERBATIM
	)
	add_custom_command(
		OUTPUT "${vulkanTerrainFragmentShaderHeader}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${vulkanShaderBuildDir}"
		COMMAND "${VULKAN_GLSLANG_VALIDATOR}"
			-V
			--target-env vulkan1.1
			-x
			--vn RECOIL_VULKAN_TERRAIN_FRAG_SPV
			-o "${vulkanTerrainFragmentShaderHeader}"
			"${vulkanShaderSourceDir}/Terrain.frag"
		DEPENDS "${vulkanShaderSourceDir}/Terrain.frag"
		VERBATIM
	)

	target_sources(engine-legacy PRIVATE
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanBuffer.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanContext.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanDevice.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanDrawBatch.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanDdsTexture.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanFontRenderer.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanGuiRenderer.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanLoadingScreen.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanMapShading.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanMapTexture.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanPipeline.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanSkyTexture.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanStartupScreen.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanSwapchain.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanSmfTerrainTexture.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanTerrain.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanTerrainRenderer.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanTextRenderer.cpp"
		"${ENGINE_SRC_ROOT_DIR}/Rendering/Vulkan/VulkanTexture.cpp"
		"${vulkanVertexShaderHeader}"
		"${vulkanFragmentShaderHeader}"
		"${vulkanTerrainVertexShaderHeader}"
		"${vulkanTerrainFragmentShaderHeader}"
	)
	target_include_directories(engine-legacy PRIVATE "${vulkanShaderBuildDir}")
	target_compile_definitions(engine-legacy PRIVATE RECOIL_VULKAN)
endif()

# Export symbols for plugin access (replaces CMP0065 OLD behavior)
set_target_properties(engine-legacy PROPERTIES ENABLE_EXPORTS TRUE)

if(MSVC)
    target_link_libraries(engine-legacy engine-natvis)
endif()


### Install the executable
install(TARGETS engine-legacy DESTINATION ${BINDIR})

create_engine_build_and_install_target(legacy)

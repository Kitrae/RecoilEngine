cmake_minimum_required(VERSION 3.25)

project(recoil-vulkan-smoke LANGUAGES CXX)

find_package(SDL2 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(recoil-vulkan-smoke
	"${CMAKE_CURRENT_LIST_DIR}/main.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/../../rts/Rendering/Vulkan/VulkanContext.cpp"
)

target_compile_features(recoil-vulkan-smoke PRIVATE cxx_std_23)
target_compile_definitions(recoil-vulkan-smoke PRIVATE SDL_MAIN_HANDLED)
target_include_directories(recoil-vulkan-smoke PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}/../../rts"
)
target_link_libraries(recoil-vulkan-smoke PRIVATE
	SDL2::SDL2
	Vulkan::Vulkan
)

if(MSVC)
	target_compile_options(recoil-vulkan-smoke PRIVATE /W4 /permissive-)
else()
	target_compile_options(recoil-vulkan-smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
	add_custom_command(TARGET recoil-vulkan-smoke POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different
			"$<TARGET_FILE:SDL2::SDL2>"
			"$<TARGET_FILE_DIR:recoil-vulkan-smoke>"
	)
endif()

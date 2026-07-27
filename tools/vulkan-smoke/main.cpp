/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "Rendering/Vulkan/VulkanContext.h"

namespace
{
	int ParseFrameLimit(int argc, char** argv)
	{
		for (int index = 1; index + 1 < argc; ++index) {
			if (std::string_view(argv[index]) == "--frames")
				return std::max(std::atoi(argv[index + 1]), 0);
		}

		return 0;
	}

	void LogMessage(const char* message)
	{
		std::cerr << message << '\n';
	}
}

int main(int argc, char** argv)
{
	SDL_SetMainReady();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}

	if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
		std::cerr << "SDL could not load Vulkan: " << SDL_GetError() << '\n';
		SDL_Quit();
		return 1;
	}

	SDL_Window* window = SDL_CreateWindow(
		"Recoil Vulkan",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		1280,
		720,
		SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
	);
	if (window == nullptr) {
		std::cerr << "SDL window creation failed: " << SDL_GetError() << '\n';
		SDL_Vulkan_UnloadLibrary();
		SDL_Quit();
		return 1;
	}

	int result = 0;
	{
		Vulkan::Context context;
		if (!context.Initialize(window, true, LogMessage)) {
			std::cerr << context.GetLastError() << '\n';
			result = 1;
		} else {
			const int frameLimit = ParseFrameLimit(argc, argv);
			int frame = 0;
			bool running = true;

			while (running && (frameLimit == 0 || frame < frameLimit)) {
				SDL_Event event;
				while (SDL_PollEvent(&event)) {
					if (event.type == SDL_QUIT)
						running = false;
				}

				const float phase = static_cast<float>(frame) * 0.02f;
				const std::array clearColor = {
					0.08f + 0.04f * std::sin(phase),
					0.12f + 0.04f * std::sin(phase + 2.0f),
					0.18f + 0.04f * std::sin(phase + 4.0f),
					1.0f,
				};

				if (!context.DrawFrame(clearColor)) {
					std::cerr << context.GetLastError() << '\n';
					result = 1;
					break;
				}

				++frame;
				SDL_Delay(16);
			}
		}
	}

	SDL_DestroyWindow(window);
	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
	return result;
}

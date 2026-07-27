/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanStartupScreen.h"

#include <string>

#include "VulkanDrawBatch.h"
#include "Rendering/Fonts/glFont.h"
#include "Rendering/GlobalRendering.h"
#include "System/Color.h"

namespace Vulkan
{
	bool ConfigureStartupScreen(
		CGlobalRendering& rendering,
		CglFont& startupFont,
		const std::string& springVersion
	) {
		const std::string status = "[Initializing Virtual File System]";
		const std::string version = "Recoil " + springVersion;

		DrawBatch batch;
		if (!batch.AddQuad(
			rendering.GetVulkanStartupTexture(),
			0.0f,
			1.0f,
			1.0f,
			0.0f,
			0.0f,
			0.0f,
			1.0f,
			1.0f,
			{255, 255, 255, 255}
		)) {
			return false;
		}
		if (!batch.Submit(rendering))
			return false;

		constexpr int textOptions = FONT_CENTER | FONT_SCALE | FONT_NORM;
		startupFont.Begin();
		startupFont.SetTextColor(SColor(255, 255, 255, 242));
		startupFont.glPrint(0.5f, 0.175f, 0.8f, textOptions, status);
		startupFont.SetTextColor(SColor(220, 220, 220, 230));
		startupFont.glPrint(0.5f, 0.08f, 0.7f, textOptions, version);
		startupFont.End();
		startupFont.SetTextColor();
		return true;
	}
}

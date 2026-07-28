/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanLoadingScreen.h"

#include <algorithm>

#include "VulkanGuiRenderer.h"
#include "Game/GameVersion.h"
#include "Rendering/Fonts/glFont.h"
#include "Rendering/GlobalRendering.h"
#include "System/Color.h"

namespace Vulkan
{
	bool DrawLoadingScreen(
		CGlobalRendering& rendering,
		CglFont& font,
		std::span<const std::string> messages,
		std::string_view mapName,
		std::string_view gameName
	) {
		if (!DrawGuiQuad(rendering, 0.0f, 0.0f, 1.0f, 1.0f, SColor(12, 16, 23, 255)))
			return false;
		if (!DrawGuiQuad(rendering, 0.12f, 0.12f, 0.88f, 0.88f, SColor(25, 32, 43, 238)))
			return false;
		if (!DrawGuiOutline(rendering, 0.12f, 0.12f, 0.88f, 0.88f, SColor(105, 132, 163, 220)))
			return false;

		constexpr int centered = FONT_CENTER | FONT_SCALE | FONT_NORM;
		constexpr int left = FONT_SCALE | FONT_NORM;

		font.Begin();
		font.SetTextColor(SColor(235, 240, 247, 255));
		font.glPrint(0.5f, 0.81f, 1.45f, centered, "Loading game");

		font.SetTextColor(SColor(170, 190, 212, 255));
		font.glPrint(0.16f, 0.75f, 0.8f, left, "Game: " + std::string(gameName));
		font.glPrint(0.16f, 0.71f, 0.8f, left, "Map: " + std::string(mapName));

		const std::size_t firstMessage = messages.size() > 10 ? messages.size() - 10 : 0;
		float y = 0.62f;
		for (std::size_t i = firstMessage; i < messages.size(); ++i) {
			const bool latest = (i + 1 == messages.size());
			font.SetTextColor(latest ? SColor(238, 244, 251, 255) : SColor(150, 164, 181, 230));
			font.glPrint(0.16f, y, latest ? 0.9f : 0.72f, left, messages[i]);
			y -= 0.045f;
		}

		font.SetTextColor(SColor(125, 140, 158, 220));
		font.glPrint(
			0.5f,
			0.16f,
			0.65f,
			centered,
			"Recoil " + SpringVersion::GetFull()
		);
		font.End();
		font.SetTextColor();
		return true;
	}
}

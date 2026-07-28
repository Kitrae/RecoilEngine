/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <span>
#include <string>
#include <string_view>

class CglFont;
class CGlobalRendering;

namespace Vulkan
{
	bool DrawLoadingScreen(
		CGlobalRendering& rendering,
		CglFont& font,
		std::span<const std::string> messages,
		std::string_view mapName,
		std::string_view gameName
	);
}

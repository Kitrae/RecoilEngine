/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <string>

class CGlobalRendering;
class CglFont;

namespace Vulkan
{
	bool ConfigureStartupScreen(
		CGlobalRendering& rendering,
		CglFont& startupFont,
		const std::string& springVersion
	);
}

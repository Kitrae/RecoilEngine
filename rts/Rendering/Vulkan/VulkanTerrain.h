/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

class CCamera;
class CGlobalRendering;
class CReadMap;

namespace Vulkan
{
	bool InitializeTerrain(CGlobalRendering& rendering, CReadMap& map);
	void DrawTerrain(CGlobalRendering& rendering, const CCamera& activeCamera);
	void KillTerrain(CGlobalRendering& rendering);
}

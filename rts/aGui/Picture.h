/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <string>

#include "GuiElement.h"

namespace agui
{

class Picture : public GuiElement
{
public:
	Picture(GuiElement* parent = NULL);
	~Picture();

	void Load(const std::string& file);

private:
	virtual void DrawSelf();
	
	uint32_t texture;
	bool vulkanTexture;
	std::string file;
};

}

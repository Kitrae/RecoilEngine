/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "Picture.h"

#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Textures/Bitmap.h"
#if defined(RECOIL_VULKAN)
#include "Rendering/Vulkan/VulkanGuiRenderer.h"
#endif
#include "System/Log/ILog.h"

namespace agui
{

	Picture::Picture(GuiElement* parent)
		: GuiElement(parent)
		, texture(0)
		, vulkanTexture(false)
	{
	}

	Picture::~Picture()
	{
		if (texture == 0)
			return;

#if defined(RECOIL_VULKAN)
		if (vulkanTexture) {
			if (globalRendering != nullptr)
				globalRendering->DestroyVulkanTexture(texture);
		} else
#endif
		{
			glDeleteTextures(1, &texture);
		}
	}

	void Picture::Load(const std::string& _file)
	{
		file = _file;

		CBitmap bmp;
		if (bmp.Load(file)) {
#if defined(RECOIL_VULKAN)
			if (globalRendering->IsVulkan()) {
				const auto handle = Vulkan::CreateGuiTexture(*globalRendering, bmp);
				if (!handle.has_value()) {
					LOG_L(L_WARNING, "Failed to upload Vulkan GUI texture: %s", file.c_str());
					return;
				}

				texture = handle.value();
				vulkanTexture = true;
				return;
			}
#endif
			texture = bmp.CreateTexture();
		}
		else {
			LOG_L(L_WARNING, "Failed to load: %s", file.c_str());
			texture = 0;
		}
	}

#ifdef HEADLESS
	void Picture::DrawSelf() {}
#else
	void Picture::DrawSelf()
	{
		if (texture) {
#if defined(RECOIL_VULKAN)
			if (vulkanTexture) {
				Vulkan::DrawGuiTexturedQuad(
					*globalRendering,
					texture,
					pos[0],
					pos[1],
					pos[0] + size[0],
					pos[1] + size[1]
				);
				return;
			}
#endif
			auto& rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_2DTC>();
			auto& sh = rb.GetShader();
			const SColor color = { 1.0f, 1.0f, 1.0f, 1.0f };

			rb.AddQuadTriangles(
				{ pos[0]          , pos[1]          , 0.0f, 1.0f, color },
				{ pos[0] + size[0], pos[1]          , 1.0f, 1.0f, color },
				{ pos[0] + size[0], pos[1] + size[1], 1.0f, 0.0f, color },
				{ pos[0]          , pos[1] + size[1], 0.0f, 0.0f, color }
			);

			glBindTexture(GL_TEXTURE_2D, texture);
			sh.Enable();
			rb.DrawElements(GL_TRIANGLES);
			sh.Disable();
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	}
#endif

} // namespace agui

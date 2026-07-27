/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanTextRenderer.h"

#include <algorithm>
#include <string>

#include "VulkanDrawBatch.h"
#include "Rendering/Fonts/glFont.h"
#include "Rendering/Fonts/ustring.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Textures/Bitmap.h"
#include "System/StringUtil.h"

namespace
{
	bool ConvertAtlasToRGBA8(const CBitmap& atlas, std::vector<uint8_t>& rgba)
	{
		if (atlas.Empty() || atlas.xsize <= 0 || atlas.ysize <= 0)
			return false;
		if (atlas.channels != 1 && atlas.channels != 4)
			return false;

		const auto pixelCount = static_cast<std::size_t>(atlas.xsize) * atlas.ysize;
		rgba.resize(pixelCount * 4);
		const uint8_t* source = atlas.GetRawMem();

		for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
			if (atlas.channels == 1) {
				rgba[pixel * 4 + 0] = 255;
				rgba[pixel * 4 + 1] = 255;
				rgba[pixel * 4 + 2] = 255;
				rgba[pixel * 4 + 3] = source[pixel];
			} else {
				rgba[pixel * 4 + 0] = source[pixel * 4 + 2];
				rgba[pixel * 4 + 1] = source[pixel * 4 + 1];
				rgba[pixel * 4 + 2] = source[pixel * 4 + 0];
				rgba[pixel * 4 + 3] = source[pixel * 4 + 3];
			}
		}

		return true;
	}

	bool ReadColorCode(
		const std::string& text,
		int& position,
		const std::array<uint8_t, 4>& baseColor,
		std::array<uint8_t, 4>& color
	) {
		const auto marker = static_cast<uint8_t>(text[position]);
		if (marker == CTextWrap::ColorResetIndicator) {
			color = baseColor;
			++position;
			return true;
		}

		if (marker == CTextWrap::ColorCodeIndicator || marker == CTextWrap::OldColorCodeIndicator) {
			if (position + 4 > static_cast<int>(text.size()))
				return false;

			color = {
				static_cast<uint8_t>(text[position + 1]),
				static_cast<uint8_t>(text[position + 2]),
				static_cast<uint8_t>(text[position + 3]),
				baseColor[3],
			};
			position += 4;
			return true;
		}

		if (marker == CTextWrap::ColorCodeIndicatorEx || marker == CTextWrap::OldColorCodeIndicatorEx) {
			if (position + 9 > static_cast<int>(text.size()))
				return false;

			color = {
				static_cast<uint8_t>(text[position + 1]),
				static_cast<uint8_t>(text[position + 2]),
				static_cast<uint8_t>(text[position + 3]),
				static_cast<uint8_t>(text[position + 4]),
			};
			position += 9;
			return true;
		}

		return false;
	}
}

namespace Vulkan
{
	TextRenderer::TextRenderer(CGlobalRendering& rendering, CglFont& font)
		: rendering(rendering)
		, font(font)
	{
	}

	bool TextRenderer::Prepare(std::span<const std::string_view> text)
	{
		for (const auto value : text) {
			const std::string textCopy(value);
			font.ScanForWantedGlyphs(toustring(textCopy));
		}

		const CBitmap& atlas = font.PrepareGlyphAtlas();
		const uint64_t revision = font.GetGlyphAtlasRevision();
		if (texture != INVALID_TEXTURE_HANDLE && revision == uploadedRevision)
			return true;
		if (!UploadAtlas(atlas))
			return false;

		uploadedRevision = revision;
		atlasWidth = atlas.xsize;
		atlasHeight = atlas.ysize;
		return true;
	}

	bool TextRenderer::UploadAtlas(const CBitmap& atlas)
	{
		if (!ConvertAtlasToRGBA8(atlas, atlasRGBA))
			return false;

		if (texture == INVALID_TEXTURE_HANDLE) {
			const auto newTexture = rendering.CreateVulkanTextureRGBA8(
				atlasRGBA.data(),
				static_cast<uint32_t>(atlas.xsize),
				static_cast<uint32_t>(atlas.ysize)
			);
			if (!newTexture.has_value())
				return false;

			texture = newTexture.value();
			return true;
		}

		return rendering.UpdateVulkanTextureRGBA8(
			texture,
			atlasRGBA.data(),
			static_cast<uint32_t>(atlas.xsize),
			static_cast<uint32_t>(atlas.ysize)
		);
	}

	bool TextRenderer::AddText(
		DrawBatch& batch,
		std::string_view text,
		float x,
		float baselineY,
		float scale,
		const std::array<uint8_t, 4>& color,
		TextAlignment alignment
	) const {
		if (texture == INVALID_TEXTURE_HANDLE || atlasWidth <= 0 || atlasHeight <= 0)
			return false;

		const float scaleX = scale * font.GetSize() * rendering.pixelX;
		const float scaleY = scale * font.GetSize() * rendering.pixelY;
		const float inverseAtlasWidth = 1.0f / atlasWidth;
		const float inverseAtlasHeight = 1.0f / atlasHeight;
		const auto lines = CglFont::SplitIntoLines(toustring(std::string(text)));

		float lineBaseline = baselineY;
		for (const auto& line : lines) {
			float lineX = x;
			const float lineWidth = scaleX * font.GetTextWidth(line);
			if (alignment == TextAlignment::Center)
				lineX -= 0.5f * lineWidth;
			else if (alignment == TextAlignment::Right)
				lineX -= lineWidth;

			const float lineY = lineBaseline + scaleY * font.GetDescender();
			std::array<uint8_t, 4> currentColor = color;
			char32_t previousCharacter = 0;

			for (int position = 0; position < static_cast<int>(line.size()); ) {
				if (ReadColorCode(line, position, color, currentColor))
					continue;

				const char32_t character = utf8::GetNextChar(line, position);
				if (previousCharacter != 0)
					lineX += scaleX * font.GetGlyphAdvance(previousCharacter, character);

				const auto& glyph = font.GetGlyph(character);
				if (glyph.size.w != 0.0f && glyph.size.h != 0.0f) {
					const float x0 = lineX + scaleX * glyph.size.x0();
					const float y0 = lineY + scaleY * glyph.size.y0();
					const float x1 = lineX + scaleX * glyph.size.x1();
					const float y1 = lineY + scaleY * glyph.size.y1();
					const float u0 = glyph.texCord.x0() * inverseAtlasWidth;
					const float v0 = glyph.texCord.y0() * inverseAtlasHeight;
					const float u1 = glyph.texCord.x1() * inverseAtlasWidth;
					const float v1 = glyph.texCord.y1() * inverseAtlasHeight;

					if (!batch.AddQuad(texture, x0, y0, x1, y1, u0, v0, u1, v1, currentColor))
						return false;
				}

				previousCharacter = character;
			}

			lineBaseline += scaleY * font.GetLineHeight();
		}

		return true;
	}
}

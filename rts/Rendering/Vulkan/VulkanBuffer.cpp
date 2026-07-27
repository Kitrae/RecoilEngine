/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanContext.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace Vulkan
{
	uint32_t Context::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
	{
		VkPhysicalDeviceMemoryProperties memoryProperties{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

		for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
			const bool typeMatches = (typeFilter & (1u << index)) != 0;
			const bool propertiesMatch = (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;
			if (typeMatches && propertiesMatch)
				return index;
		}

		return std::numeric_limits<uint32_t>::max();
	}

	bool Context::CreateBuffer(
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties,
		VkBuffer& buffer,
		VkDeviceMemory& memory
	) {
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
			return Fail("Failed creating a Vulkan buffer");

		VkMemoryRequirements memoryRequirements{};
		vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

		const uint32_t memoryType = FindMemoryType(memoryRequirements.memoryTypeBits, properties);
		if (memoryType == std::numeric_limits<uint32_t>::max()) {
			vkDestroyBuffer(device, buffer, nullptr);
			buffer = VK_NULL_HANDLE;
			return Fail("No compatible Vulkan buffer memory type is available");
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryRequirements.size;
		allocateInfo.memoryTypeIndex = memoryType;

		if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
			vkDestroyBuffer(device, buffer, nullptr);
			buffer = VK_NULL_HANDLE;
			return Fail("Failed allocating Vulkan buffer memory");
		}

		if (vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
			vkDestroyBuffer(device, buffer, nullptr);
			vkFreeMemory(device, memory, nullptr);
			memory = VK_NULL_HANDLE;
			buffer = VK_NULL_HANDLE;
			return Fail("Failed binding Vulkan buffer memory");
		}

		return true;
	}

	bool Context::ValidateDrawBatch(
		std::span<const Vertex2D> vertices,
		std::span<const uint32_t> indices,
		std::span<const DrawRange> ranges
	) {
		if (vertices.empty() != indices.empty())
			return Fail("Vulkan draw vertices and indices must both be empty or both contain data");
		if (vertices.empty() != ranges.empty())
			return Fail("A Vulkan draw batch must provide ranges for its geometry");
		if (vertices.size() > std::numeric_limits<uint32_t>::max() || indices.size() > std::numeric_limits<uint32_t>::max())
			return Fail("The Vulkan draw batch exceeds 32-bit geometry limits");

		const auto invalidIndex = std::find_if(indices.begin(), indices.end(), [vertexCount = vertices.size()](uint32_t index) {
			return index >= vertexCount;
		});
		if (invalidIndex != indices.end())
			return Fail("A Vulkan draw index is outside the vertex batch");

		for (const auto& range : ranges) {
			if (range.firstIndex > indices.size() || range.indexCount > indices.size() - range.firstIndex)
				return Fail("A Vulkan draw range is outside the index batch");
			if (range.texture >= textures.size() || textures[range.texture].descriptorSet == VK_NULL_HANDLE)
				return Fail("A Vulkan draw range refers to an invalid texture");
		}

		return true;
	}

	bool Context::SetDrawBatch(
		std::span<const Vertex2D> vertices,
		std::span<const uint32_t> indices,
		std::span<const DrawRange> ranges
	) {
		if (!ValidateDrawBatch(vertices, indices, ranges))
			return false;

		drawVertices.assign(vertices.begin(), vertices.end());
		drawIndices.assign(indices.begin(), indices.end());
		drawRanges.assign(ranges.begin(), ranges.end());
		return true;
	}

	bool Context::AppendDrawBatch(
		std::span<const Vertex2D> vertices,
		std::span<const uint32_t> indices,
		std::span<const DrawRange> ranges
	) {
		if (!ValidateDrawBatch(vertices, indices, ranges))
			return false;
		if (
			vertices.size() > std::numeric_limits<uint32_t>::max() - drawVertices.size() ||
			indices.size() > std::numeric_limits<uint32_t>::max() - drawIndices.size()
		) {
			return Fail("The combined Vulkan draw batch exceeds 32-bit geometry limits");
		}

		const uint32_t vertexOffset = static_cast<uint32_t>(drawVertices.size());
		const uint32_t indexOffset = static_cast<uint32_t>(drawIndices.size());
		drawVertices.insert(drawVertices.end(), vertices.begin(), vertices.end());
		drawIndices.reserve(drawIndices.size() + indices.size());
		for (const uint32_t index : indices)
			drawIndices.push_back(vertexOffset + index);

		drawRanges.reserve(drawRanges.size() + ranges.size());
		for (const auto& range : ranges)
			drawRanges.push_back({indexOffset + range.firstIndex, range.indexCount, range.texture});

		return true;
	}

	bool Context::CreateGeometryBuffers()
	{
		constexpr std::array<Vertex2D, 4> vertices = {{
			{{0.0f, 1.0f}, {0.0f, 0.0f}, {255, 255, 255, 255}},
			{{0.0f, 0.0f}, {0.0f, 1.0f}, {255, 255, 255, 255}},
			{{1.0f, 0.0f}, {1.0f, 1.0f}, {255, 255, 255, 255}},
			{{1.0f, 1.0f}, {1.0f, 0.0f}, {255, 255, 255, 255}},
		}};
		constexpr std::array<uint32_t, 6> indices = {
			0, 1, 2,
			0, 2, 3,
		};
		const std::array<DrawRange, 1> ranges = {{
			{0, static_cast<uint32_t>(indices.size()), startupTexture},
		}};

		if (!SetDrawBatch(vertices, indices, ranges))
			return false;

		for (std::size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex) {
			if (!UpdateGeometryBuffer(frameIndex)) {
				DestroyGeometryBuffers();
				return false;
			}
		}

		return true;
	}

	bool Context::ResizeGeometryBuffer(FrameGeometry& geometry, VkDeviceSize requiredSize)
	{
		DestroyGeometryBuffer(geometry);

		constexpr VkDeviceSize MINIMUM_CAPACITY = 64 * 1024;
		VkDeviceSize newCapacity = MINIMUM_CAPACITY;
		while (newCapacity < requiredSize) {
			if (newCapacity > std::numeric_limits<VkDeviceSize>::max() / 2)
				return Fail("The Vulkan 2D geometry batch is too large");
			newCapacity *= 2;
		}

		if (!CreateBuffer(
			newCapacity,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			geometry.buffer,
			geometry.memory
		)) {
			return false;
		}

		if (vkMapMemory(device, geometry.memory, 0, newCapacity, 0, &geometry.mappedMemory) != VK_SUCCESS) {
			DestroyGeometryBuffer(geometry);
			return Fail("Failed mapping a Vulkan 2D geometry buffer");
		}

		geometry.capacity = newCapacity;
		return true;
	}

	bool Context::UpdateGeometryBuffer(std::size_t frameIndex)
	{
		auto& geometry = frameGeometry[frameIndex];
		if (drawVertices.empty()) {
			geometry.indexCount = 0;
			return true;
		}

		const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(drawVertices.size()) * sizeof(Vertex2D);
		const VkDeviceSize indexOffset = (vertexBytes + sizeof(uint32_t) - 1) & ~(sizeof(uint32_t) - 1);
		const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(drawIndices.size()) * sizeof(uint32_t);
		if (indexOffset > std::numeric_limits<VkDeviceSize>::max() - indexBytes)
			return Fail("The Vulkan 2D geometry batch is too large");
		const VkDeviceSize requiredSize = indexOffset + indexBytes;

		if (geometry.capacity < requiredSize && !ResizeGeometryBuffer(geometry, requiredSize))
			return false;

		std::memcpy(geometry.mappedMemory, drawVertices.data(), static_cast<std::size_t>(vertexBytes));
		std::memcpy(
			static_cast<uint8_t*>(geometry.mappedMemory) + indexOffset,
			drawIndices.data(),
			static_cast<std::size_t>(indexBytes)
		);

		geometry.indexOffset = indexOffset;
		geometry.indexCount = static_cast<uint32_t>(drawIndices.size());
		return true;
	}

	void Context::DestroyGeometryBuffer(FrameGeometry& geometry)
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (geometry.mappedMemory != nullptr)
			vkUnmapMemory(device, geometry.memory);
		if (geometry.buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(device, geometry.buffer, nullptr);
		if (geometry.memory != VK_NULL_HANDLE)
			vkFreeMemory(device, geometry.memory, nullptr);

		geometry = {};
	}

	void Context::DestroyGeometryBuffers()
	{
		for (auto& geometry : frameGeometry)
			DestroyGeometryBuffer(geometry);

		drawVertices.clear();
		drawIndices.clear();
		drawRanges.clear();
	}
}

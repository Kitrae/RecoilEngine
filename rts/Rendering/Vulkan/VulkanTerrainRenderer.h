/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

namespace Vulkan
{
	struct TerrainVertex
	{
		std::array<float, 3> position;
		std::array<float, 2> textureCoordinates;
	};

	class TerrainRenderer
	{
	public:
		bool Initialize(
			VkDevice device,
			VkPhysicalDevice physicalDevice,
			VkRenderPass renderPass
		);
		void Shutdown();

		bool RecreatePipeline(VkRenderPass renderPass);
		void DestroyPipeline();
		bool SetMesh(std::span<const TerrainVertex> vertices, std::span<const uint32_t> indices);
		void SetTextures(const std::array<VkDescriptorImageInfo, 3>& textures);
		void SetDrawInfo(uint32_t instanceCount, const std::array<uint32_t, 4>& terrainInfo);
		void ClearMesh();
		void SetTransform(const std::array<float, 16>& transform) { viewProjection = transform; }
		void Record(VkCommandBuffer commandBuffer) const;
		bool HasMesh() const { return indexCount != 0; }

	private:
		bool CreateDescriptors();
		bool CreatePipeline(VkRenderPass renderPass);
		bool CreateShaderModule(const uint32_t* code, std::size_t size, VkShaderModule& shaderModule) const;
		bool CreateBuffer(
			VkDeviceSize size,
			VkBufferUsageFlags usage,
			VkBuffer& buffer,
			VkDeviceMemory& memory
		) const;
		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

	private:
		VkDevice device = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDescriptorSetLayout textureDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool textureDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet textureDescriptorSet = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkBuffer vertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
		VkBuffer indexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory indexMemory = VK_NULL_HANDLE;
		uint32_t indexCount = 0;
		uint32_t instanceCount = 0;
		std::array<uint32_t, 4> terrainInfo{};
		std::array<float, 16> viewProjection{};
	};
}

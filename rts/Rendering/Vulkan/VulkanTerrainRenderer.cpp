/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanTerrainRenderer.h"

#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>

#include "Terrain.frag.spv.h"
#include "Terrain.vert.spv.h"

namespace Vulkan
{
	bool TerrainRenderer::Initialize(
		VkDevice device_,
		VkPhysicalDevice physicalDevice_,
		VkRenderPass renderPass
	) {
		device = device_;
		physicalDevice = physicalDevice_;
		viewProjection = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};
		if (!CreateDescriptors())
			return false;
		return CreatePipeline(renderPass);
	}

	void TerrainRenderer::Shutdown()
	{
		if (device == VK_NULL_HANDLE)
			return;

		ClearMesh();
		DestroyPipeline();
		if (textureDescriptorPool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(device, textureDescriptorPool, nullptr);
		if (textureDescriptorSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout, nullptr);

		device = VK_NULL_HANDLE;
		physicalDevice = VK_NULL_HANDLE;
		textureDescriptorSetLayout = VK_NULL_HANDLE;
		textureDescriptorPool = VK_NULL_HANDLE;
		textureDescriptorSet = VK_NULL_HANDLE;
	}

	bool TerrainRenderer::CreateDescriptors()
	{
		std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
		for (uint32_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex) {
			auto& binding = bindings[bindingIndex];
			binding.binding = bindingIndex;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			binding.descriptorCount = 1;
			binding.stageFlags = bindingIndex == 0
				? VK_SHADER_STAGE_FRAGMENT_BIT
				: VK_SHADER_STAGE_VERTEX_BIT;
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &textureDescriptorSetLayout) != VK_SUCCESS)
			return false;

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = static_cast<uint32_t>(bindings.size());

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &textureDescriptorPool) != VK_SUCCESS)
			return false;

		VkDescriptorSetAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = textureDescriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &textureDescriptorSetLayout;
		return vkAllocateDescriptorSets(device, &allocateInfo, &textureDescriptorSet) == VK_SUCCESS;
	}

	bool TerrainRenderer::RecreatePipeline(VkRenderPass renderPass)
	{
		DestroyPipeline();
		return CreatePipeline(renderPass);
	}

	void TerrainRenderer::DestroyPipeline()
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(device, pipeline, nullptr);
		if (pipelineLayout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

		pipeline = VK_NULL_HANDLE;
		pipelineLayout = VK_NULL_HANDLE;
	}

	bool TerrainRenderer::CreateShaderModule(
		const uint32_t* code,
		std::size_t size,
		VkShaderModule& shaderModule
	) const {
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = size;
		createInfo.pCode = code;
		return vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS;
	}

	bool TerrainRenderer::CreatePipeline(VkRenderPass renderPass)
	{
		VkShaderModule vertexShader = VK_NULL_HANDLE;
		VkShaderModule fragmentShader = VK_NULL_HANDLE;
		if (!CreateShaderModule(RECOIL_VULKAN_TERRAIN_VERT_SPV, sizeof(RECOIL_VULKAN_TERRAIN_VERT_SPV), vertexShader))
			return false;
		if (!CreateShaderModule(RECOIL_VULKAN_TERRAIN_FRAG_SPV, sizeof(RECOIL_VULKAN_TERRAIN_FRAG_SPV), fragmentShader)) {
			vkDestroyShaderModule(device, vertexShader, nullptr);
			return false;
		}

		VkPipelineShaderStageCreateInfo shaderStages[2]{};
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vertexShader;
		shaderStages[0].pName = "main";
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = fragmentShader;
		shaderStages[1].pName = "main";

		VkVertexInputBindingDescription vertexBinding{};
		vertexBinding.binding = 0;
		vertexBinding.stride = sizeof(TerrainVertex);
		vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttributes[2]{};
		vertexAttributes[0].location = 0;
		vertexAttributes[0].binding = 0;
		vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttributes[0].offset = offsetof(TerrainVertex, position);
		vertexAttributes[1].location = 1;
		vertexAttributes[1].binding = 0;
		vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[1].offset = offsetof(TerrainVertex, textureCoordinates);

		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &vertexBinding;
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(vertexAttributes));
		vertexInput.pVertexAttributeDescriptions = vertexAttributes;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterization{};
		rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization.cullMode = VK_CULL_MODE_NONE;
		rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterization.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		const VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
		dynamicState.pDynamicStates = dynamicStates;

		VkPushConstantRange pushConstant{};
		pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstant.size = sizeof(viewProjection) + sizeof(terrainInfo);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &textureDescriptorSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushConstant;
		const auto layoutResult = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = static_cast<uint32_t>(std::size(shaderStages));
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		const auto pipelineResult = layoutResult == VK_SUCCESS
			? vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)
			: VK_ERROR_INITIALIZATION_FAILED;

		vkDestroyShaderModule(device, fragmentShader, nullptr);
		vkDestroyShaderModule(device, vertexShader, nullptr);
		if (pipelineResult == VK_SUCCESS)
			return true;

		DestroyPipeline();
		return false;
	}

	uint32_t TerrainRenderer::FindMemoryType(
		uint32_t typeFilter,
		VkMemoryPropertyFlags properties
	) const {
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

	bool TerrainRenderer::CreateBuffer(
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkBuffer& buffer,
		VkDeviceMemory& memory
	) const {
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
			return false;

		VkMemoryRequirements memoryRequirements{};
		vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);
		const auto memoryType = FindMemoryType(
			memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);
		if (memoryType == std::numeric_limits<uint32_t>::max()) {
			vkDestroyBuffer(device, buffer, nullptr);
			buffer = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryRequirements.size;
		allocateInfo.memoryTypeIndex = memoryType;
		if (
			vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS ||
			vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS
		) {
			if (memory != VK_NULL_HANDLE)
				vkFreeMemory(device, memory, nullptr);
			vkDestroyBuffer(device, buffer, nullptr);
			memory = VK_NULL_HANDLE;
			buffer = VK_NULL_HANDLE;
			return false;
		}

		return true;
	}

	bool TerrainRenderer::SetMesh(
		std::span<const TerrainVertex> vertices,
		std::span<const uint32_t> indices
	) {
		if (vertices.empty() || indices.empty())
			return false;

		ClearMesh();
		const VkDeviceSize vertexSize = vertices.size_bytes();
		const VkDeviceSize indexSize = indices.size_bytes();
		if (!CreateBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexMemory))
			return false;
		if (!CreateBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer, indexMemory)) {
			ClearMesh();
			return false;
		}

		void* mappedMemory = nullptr;
		if (vkMapMemory(device, vertexMemory, 0, vertexSize, 0, &mappedMemory) != VK_SUCCESS) {
			ClearMesh();
			return false;
		}
		std::memcpy(mappedMemory, vertices.data(), vertexSize);
		vkUnmapMemory(device, vertexMemory);

		if (vkMapMemory(device, indexMemory, 0, indexSize, 0, &mappedMemory) != VK_SUCCESS) {
			ClearMesh();
			return false;
		}
		std::memcpy(mappedMemory, indices.data(), indexSize);
		vkUnmapMemory(device, indexMemory);

		indexCount = static_cast<uint32_t>(indices.size());
		return true;
	}

	void TerrainRenderer::SetTextures(const std::array<VkDescriptorImageInfo, 3>& textures)
	{
		std::array<VkWriteDescriptorSet, 3> writes{};
		for (uint32_t bindingIndex = 0; bindingIndex < writes.size(); ++bindingIndex) {
			auto& write = writes[bindingIndex];
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = textureDescriptorSet;
			write.dstBinding = bindingIndex;
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.pImageInfo = &textures[bindingIndex];
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

	void TerrainRenderer::SetDrawInfo(
		uint32_t instanceCount_,
		const std::array<uint32_t, 4>& terrainInfo_
	) {
		instanceCount = instanceCount_;
		terrainInfo = terrainInfo_;
	}

	void TerrainRenderer::ClearMesh()
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (vertexBuffer != VK_NULL_HANDLE)
			vkDestroyBuffer(device, vertexBuffer, nullptr);
		if (vertexMemory != VK_NULL_HANDLE)
			vkFreeMemory(device, vertexMemory, nullptr);
		if (indexBuffer != VK_NULL_HANDLE)
			vkDestroyBuffer(device, indexBuffer, nullptr);
		if (indexMemory != VK_NULL_HANDLE)
			vkFreeMemory(device, indexMemory, nullptr);

		vertexBuffer = VK_NULL_HANDLE;
		vertexMemory = VK_NULL_HANDLE;
		indexBuffer = VK_NULL_HANDLE;
		indexMemory = VK_NULL_HANDLE;
		indexCount = 0;
		instanceCount = 0;
	}

	void TerrainRenderer::Record(VkCommandBuffer commandBuffer) const
	{
		if (
			!HasMesh() ||
			instanceCount == 0 ||
			pipeline == VK_NULL_HANDLE ||
			textureDescriptorSet == VK_NULL_HANDLE
		) {
			return;
		}

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		constexpr VkDeviceSize vertexOffset = 0;
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
		vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(
			commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0,
			1,
			&textureDescriptorSet,
			0,
			nullptr
		);
		std::array<uint8_t, sizeof(viewProjection) + sizeof(terrainInfo)> pushConstants{};
		std::memcpy(pushConstants.data(), viewProjection.data(), sizeof(viewProjection));
		std::memcpy(
			pushConstants.data() + sizeof(viewProjection),
			terrainInfo.data(),
			sizeof(terrainInfo)
		);
		vkCmdPushConstants(
			commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			static_cast<uint32_t>(pushConstants.size()),
			pushConstants.data()
		);
		vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, 0, 0, 0);
	}
}

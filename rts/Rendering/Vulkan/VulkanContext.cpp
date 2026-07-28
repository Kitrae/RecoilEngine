/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanContext.h"

#include <algorithm>
#include <limits>
#include <string>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

namespace Vulkan
{
	Context::~Context()
	{
		Shutdown();
	}

	bool Context::Initialize(SDL_Window* window_, bool enableValidation, LogCallback logCallback_)
	{
		if (window_ == nullptr)
			return Fail("Cannot initialize Vulkan without an SDL window");

		window = window_;
		logCallback = logCallback_;
		validationEnabled = enableValidation && CheckValidationLayerSupport();

		if (enableValidation && !validationEnabled)
			Log("[Vulkan] Validation requested, but VK_LAYER_KHRONOS_validation is unavailable");

		if (!CreateInstance())
			return false;
		if (!CreateDebugMessenger())
			return false;
		if (!CreateSurface())
			return false;
		if (!SelectPhysicalDevice())
			return false;
		if (!CreateDevice())
			return false;
		if (!CreateSwapchain())
			return false;
		if (!CreateRenderPass())
			return false;
		if (!CreateGraphicsPipeline())
			return false;
		if (!terrainRenderer.Initialize(device, physicalDevice, renderPass))
			return Fail("Failed creating the Vulkan terrain pipeline");
		if (!CreateDepthResources())
			return false;
		if (!CreateFramebuffers())
			return false;
		if (!CreateCommandPool())
			return false;

		constexpr uint8_t solidPixel[] = {255, 255, 255, 255};
		const auto solidTextureHandle = CreateTextureRGBA8(solidPixel, 1, 1);
		if (!solidTextureHandle.has_value())
			return false;
		solidTexture = solidTextureHandle.value();

		constexpr uint8_t fallbackPixel[] = {0, 0, 0, 255};
		if (!UploadTextureRGBA8(fallbackPixel, 1, 1))
			return false;

		if (!CreateGeometryBuffers())
			return false;
		if (!AllocateCommandBuffers())
			return false;

		if (!CreateSyncObjects())
			return false;

		Log("[Vulkan] Initialized device: " + deviceName);
		return true;
	}

	void Context::Shutdown()
	{
		if (device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(device);

		DestroySyncObjects();
		DestroyTextures();
		DestroyDataBuffers();
		DestroyGeometryBuffers();
		terrainRenderer.Shutdown();

		if (device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, commandPool, nullptr);

		commandPool = VK_NULL_HANDLE;
		commandBuffers.clear();

		DestroySwapchain();

		if (device != VK_NULL_HANDLE && textureDescriptorSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout, nullptr);
		textureDescriptorSetLayout = VK_NULL_HANDLE;

		if (device != VK_NULL_HANDLE)
			vkDestroyDevice(device, nullptr);

		device = VK_NULL_HANDLE;
		graphicsQueue = VK_NULL_HANDLE;
		presentQueue = VK_NULL_HANDLE;
		physicalDevice = VK_NULL_HANDLE;
		maxTextureSize = 0;

		if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE)
			vkDestroySurfaceKHR(instance, surface, nullptr);

		surface = VK_NULL_HANDLE;
		DestroyDebugMessenger();

		if (instance != VK_NULL_HANDLE)
			vkDestroyInstance(instance, nullptr);

		instance = VK_NULL_HANDLE;
		window = nullptr;
		currentFrame = 0;
		deviceName.clear();
	}

	bool Context::SetTerrain(
		std::span<const TerrainVertex> vertices,
		std::span<const uint32_t> indices,
		const std::array<TextureHandle, 3>& terrainTextures_,
		uint32_t instanceCount,
		const std::array<uint32_t, 4>& terrainInfo
	) {
		const auto invalidTexture = std::find_if(
			terrainTextures_.begin(),
			terrainTextures_.end(),
			[this](TextureHandle texture) {
				return
					texture >= textures.size() ||
					textures[texture].imageView == VK_NULL_HANDLE ||
					textures[texture].sampler == VK_NULL_HANDLE;
			}
		);
		if (
			invalidTexture != terrainTextures_.end() ||
			vertices.empty() ||
			indices.empty() ||
			instanceCount == 0 ||
			terrainInfo[0] == 0 ||
			terrainInfo[1] == 0 ||
			terrainInfo[2] == 0 ||
			terrainInfo[3] == 0 ||
			indices.size() > std::numeric_limits<uint32_t>::max()
		) {
			return Fail("Invalid Vulkan terrain mesh");
		}

		const auto invalidIndex = std::find_if(indices.begin(), indices.end(), [vertexCount = vertices.size()](uint32_t index) {
			return index >= vertexCount;
		});
		if (invalidIndex != indices.end())
			return Fail("A Vulkan terrain index is outside the vertex mesh");

		if (vkDeviceWaitIdle(device) != VK_SUCCESS)
			return Fail("Failed waiting for Vulkan before replacing terrain");
		if (!terrainRenderer.SetMesh(vertices, indices))
			return Fail("Failed uploading the Vulkan terrain mesh");

		std::array<VkDescriptorImageInfo, 3> imageInfos{};
		for (std::size_t index = 0; index < imageInfos.size(); ++index) {
			imageInfos[index].sampler = textures[terrainTextures_[index]].sampler;
			imageInfos[index].imageView = textures[terrainTextures_[index]].imageView;
			imageInfos[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		terrainRenderer.SetTextures(imageInfos);
		terrainRenderer.SetDrawInfo(instanceCount, terrainInfo);
		terrainTextures = terrainTextures_;
		return true;
	}

	void Context::SetTerrainTransform(const std::array<float, 16>& transform)
	{
		terrainRenderer.SetTransform(transform);
	}

	void Context::ClearTerrain()
	{
		if (device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(device);

		terrainRenderer.ClearMesh();
		terrainTextures.fill(INVALID_TEXTURE_HANDLE);
	}

	bool Context::DrawFrame(const std::array<float, 4>& clearColor)
	{
		if (device == VK_NULL_HANDLE)
			return Fail("Cannot draw without an initialized Vulkan device");

		int drawableWidth = 0;
		int drawableHeight = 0;
		SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
		if (drawableWidth == 0 || drawableHeight == 0)
			return true;
		if (swapchain == VK_NULL_HANDLE)
			return RecreateSwapchain();

		const auto frameFence = frameFences[currentFrame];
		if (vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
			return Fail("Failed waiting for the Vulkan frame fence");
		if (!UpdateGeometryBuffer(currentFrame))
			return false;

		uint32_t imageIndex = 0;
		const auto acquireResult = vkAcquireNextImageKHR(
			device,
			swapchain,
			UINT64_MAX,
			imageAvailableSemaphores[currentFrame],
			VK_NULL_HANDLE,
			&imageIndex
		);

		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
			return RecreateSwapchain();
		if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
			return Fail("Failed to acquire a Vulkan swapchain image: " + std::to_string(acquireResult));

		if (imageFences[imageIndex] != VK_NULL_HANDLE) {
			if (vkWaitForFences(device, 1, &imageFences[imageIndex], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
				return Fail("Failed waiting for a Vulkan swapchain image fence");
		}

		imageFences[imageIndex] = frameFence;

		if (!RecordCommandBuffer(imageIndex, clearColor))
			return false;

		if (vkResetFences(device, 1, &frameFence) != VK_SUCCESS)
			return Fail("Failed resetting the Vulkan frame fence");

		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderFinishedSemaphores[imageIndex];

		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, frameFence) != VK_SUCCESS)
			return Fail("Failed submitting the Vulkan command buffer");

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderFinishedSemaphores[imageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = &imageIndex;

		const auto presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
			if (!RecreateSwapchain())
				return false;
		} else if (presentResult != VK_SUCCESS) {
			return Fail("Failed presenting the Vulkan swapchain image: " + std::to_string(presentResult));
		}

		currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
		return true;
	}

	bool Context::CreateCommandPool()
	{
		VkCommandPoolCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		createInfo.queueFamilyIndex = graphicsQueueFamily;

		if (vkCreateCommandPool(device, &createInfo, nullptr, &commandPool) != VK_SUCCESS)
			return Fail("Failed creating the Vulkan command pool");

		return true;
	}

	bool Context::AllocateCommandBuffers()
	{
		commandBuffers.resize(swapchainImages.size(), VK_NULL_HANDLE);

		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

		if (vkAllocateCommandBuffers(device, &allocateInfo, commandBuffers.data()) != VK_SUCCESS)
			return Fail("Failed allocating Vulkan command buffers");

		return true;
	}

	bool Context::CreateSyncObjects()
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (std::size_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index) {
			if (
				vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[index]) != VK_SUCCESS ||
				vkCreateFence(device, &fenceInfo, nullptr, &frameFences[index]) != VK_SUCCESS
			) {
				return Fail("Failed creating Vulkan frame synchronization objects");
			}
		}

		if (!CreatePresentSemaphores())
			return false;

		imageFences.assign(swapchainImages.size(), VK_NULL_HANDLE);
		return true;
	}

	bool Context::CreatePresentSemaphores()
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		renderFinishedSemaphores.resize(swapchainImages.size(), VK_NULL_HANDLE);
		for (auto& semaphore : renderFinishedSemaphores) {
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
				return Fail("Failed creating Vulkan presentation synchronization objects");
		}

		return true;
	}

	bool Context::RecordCommandBuffer(uint32_t imageIndex, const std::array<float, 4>& clearColor)
	{
		const auto commandBuffer = commandBuffers[imageIndex];
		if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
			return Fail("Failed resetting a Vulkan command buffer");

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
			return Fail("Failed beginning a Vulkan command buffer");

		std::array<VkClearValue, 2> clearValues{};
		std::copy(clearColor.begin(), clearColor.end(), clearValues[0].color.float32);
		clearValues[1].depthStencil = {1.0f, 0};

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
		renderPassInfo.renderArea.extent = swapchainExtent;
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.width = static_cast<float>(swapchainExtent.width);
		viewport.height = static_cast<float>(swapchainExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.extent = swapchainExtent;
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		const bool terrainTexturesValid = std::all_of(
			terrainTextures.begin(),
			terrainTextures.end(),
			[this](TextureHandle texture) {
				return texture < textures.size() && textures[texture].imageView != VK_NULL_HANDLE;
			}
		);
		if (terrainTexturesValid)
			terrainRenderer.Record(commandBuffer);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
		const auto& geometry = frameGeometry[currentFrame];
		if (geometry.indexCount > 0) {
			constexpr VkDeviceSize vertexOffset = 0;
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &geometry.buffer, &vertexOffset);
			vkCmdBindIndexBuffer(commandBuffer, geometry.buffer, geometry.indexOffset, VK_INDEX_TYPE_UINT32);

			TextureHandle boundTexture = INVALID_TEXTURE_HANDLE;
			for (const auto& range : drawRanges) {
				if (range.indexCount == 0)
					continue;

				if (range.texture != boundTexture) {
					const auto descriptorSet = textures[range.texture].descriptorSet;
					vkCmdBindDescriptorSets(
						commandBuffer,
						VK_PIPELINE_BIND_POINT_GRAPHICS,
						pipelineLayout,
						0,
						1,
						&descriptorSet,
						0,
						nullptr
					);
					boundTexture = range.texture;
				}

				vkCmdDrawIndexed(commandBuffer, range.indexCount, 1, range.firstIndex, 0, 0);
			}
		}
		vkCmdEndRenderPass(commandBuffer);

		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
			return Fail("Failed ending a Vulkan command buffer");

		return true;
	}

	void Context::DestroySyncObjects()
	{
		if (device == VK_NULL_HANDLE)
			return;

		for (const auto semaphore : renderFinishedSemaphores) {
			if (semaphore != VK_NULL_HANDLE)
				vkDestroySemaphore(device, semaphore, nullptr);
		}
		renderFinishedSemaphores.clear();

		for (std::size_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index) {
			if (frameFences[index] != VK_NULL_HANDLE)
				vkDestroyFence(device, frameFences[index], nullptr);
			if (imageAvailableSemaphores[index] != VK_NULL_HANDLE)
				vkDestroySemaphore(device, imageAvailableSemaphores[index], nullptr);

			frameFences[index] = VK_NULL_HANDLE;
			imageAvailableSemaphores[index] = VK_NULL_HANDLE;
		}

		imageFences.clear();
	}

	bool Context::Fail(const std::string& message)
	{
		lastError = message;
		Log("[Vulkan] " + message);
		return false;
	}

	void Context::Log(const std::string& message) const
	{
		if (logCallback != nullptr)
			logCallback(message.c_str());
	}
}

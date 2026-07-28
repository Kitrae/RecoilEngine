/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanContext.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

namespace Vulkan
{
	bool Context::RecreateSwapchain()
	{
		int drawableWidth = 0;
		int drawableHeight = 0;
		SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
		if (drawableWidth == 0 || drawableHeight == 0)
			return true;

		if (vkDeviceWaitIdle(device) != VK_SUCCESS)
			return Fail("Failed waiting for the Vulkan device before recreating the swapchain");

		if (!commandBuffers.empty()) {
			vkFreeCommandBuffers(
				device,
				commandPool,
				static_cast<uint32_t>(commandBuffers.size()),
				commandBuffers.data()
			);
			commandBuffers.clear();
		}

		for (const auto semaphore : renderFinishedSemaphores)
			vkDestroySemaphore(device, semaphore, nullptr);
		renderFinishedSemaphores.clear();

		DestroySwapchain();

		if (!CreateSwapchain())
			return false;
		if (!CreateRenderPass())
			return false;
		if (!CreateGraphicsPipeline())
			return false;
		if (!terrainRenderer.RecreatePipeline(renderPass))
			return Fail("Failed recreating the Vulkan terrain pipeline");
		if (!CreateDepthResources())
			return false;
		if (!CreateFramebuffers())
			return false;
		if (!AllocateCommandBuffers())
			return false;
		if (!CreatePresentSemaphores())
			return false;

		imageFences.assign(swapchainImages.size(), VK_NULL_HANDLE);
		return true;
	}

	bool Context::CreateSwapchain()
	{
		const auto support = QuerySwapchainSupport(physicalDevice);
		if (support.formats.empty() || support.presentModes.empty())
			return Fail("The Vulkan surface has no usable swapchain formats or present modes");
		constexpr VkImageUsageFlags requiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if ((support.capabilities.supportedUsageFlags & requiredUsage) != requiredUsage)
			return Fail("The Vulkan surface does not support color attachments");

		const auto surfaceFormat = ChooseSurfaceFormat(support.formats);
		const auto presentMode = ChoosePresentMode(support.presentModes);
		const auto extent = ChooseExtent(support.capabilities);
		if (extent.width == 0 || extent.height == 0)
			return Fail("The Vulkan surface has zero drawable extent");

		uint32_t imageCount = support.capabilities.minImageCount + 1;
		if (support.capabilities.maxImageCount > 0)
			imageCount = std::min(imageCount, support.capabilities.maxImageCount);

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = requiredUsage;

		const uint32_t queueFamilyIndices[] = {
			graphicsQueueFamily,
			presentQueueFamily,
		};
		if (graphicsQueueFamily != presentQueueFamily) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		} else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		createInfo.preTransform = support.capabilities.currentTransform;
		createInfo.compositeAlpha = ChooseCompositeAlpha(support.capabilities);
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;

		const auto result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
		if (result != VK_SUCCESS)
			return Fail("Failed creating the Vulkan swapchain: " + std::to_string(result));

		if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr) != VK_SUCCESS)
			return Fail("Failed querying Vulkan swapchain images");

		swapchainImages.resize(imageCount);
		if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()) != VK_SUCCESS)
			return Fail("Failed retrieving Vulkan swapchain images");

		swapchainFormat = surfaceFormat.format;
		swapchainExtent = extent;

		swapchainImageViews.resize(swapchainImages.size(), VK_NULL_HANDLE);
		for (std::size_t index = 0; index < swapchainImages.size(); ++index) {
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = swapchainImages[index];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = swapchainFormat;
			viewInfo.components = {
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY,
			};
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			if (vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[index]) != VK_SUCCESS)
				return Fail("Failed creating a Vulkan swapchain image view");
		}

		return true;
	}

	bool Context::CreateRenderPass()
	{
		depthFormat = FindDepthFormat();
		if (depthFormat == VK_FORMAT_UNDEFINED)
			return Fail("The Vulkan device has no supported depth attachment format");

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = swapchainFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentReference{};
		colorAttachmentReference.attachment = 0;
		colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = depthFormat;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentReference{};
		depthAttachmentReference.attachment = 1;
		depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentReference;
		subpass.pDepthStencilAttachment = &depthAttachmentReference;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask =
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstStageMask =
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask =
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		const VkAttachmentDescription attachments[] = {
			colorAttachment,
			depthAttachment,
		};
		VkRenderPassCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		createInfo.attachmentCount = static_cast<uint32_t>(std::size(attachments));
		createInfo.pAttachments = attachments;
		createInfo.subpassCount = 1;
		createInfo.pSubpasses = &subpass;
		createInfo.dependencyCount = 1;
		createInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(device, &createInfo, nullptr, &renderPass) != VK_SUCCESS)
			return Fail("Failed creating the Vulkan render pass");

		return true;
	}

	VkFormat Context::FindDepthFormat() const
	{
		constexpr VkFormat formats[] = {
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT,
		};
		for (const auto format : formats) {
			VkFormatProperties properties{};
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
			if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
				return format;
		}

		return VK_FORMAT_UNDEFINED;
	}

	bool Context::CreateDepthResources()
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = {swapchainExtent.width, swapchainExtent.height, 1};
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = depthFormat;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateImage(device, &imageInfo, nullptr, &depthImage) != VK_SUCCESS)
			return Fail("Failed creating the Vulkan depth image");

		VkMemoryRequirements memoryRequirements{};
		vkGetImageMemoryRequirements(device, depthImage, &memoryRequirements);
		const auto memoryType = FindMemoryType(
			memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		if (memoryType == std::numeric_limits<uint32_t>::max()) {
			DestroyDepthResources();
			return Fail("No compatible Vulkan depth-image memory type is available");
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryRequirements.size;
		allocateInfo.memoryTypeIndex = memoryType;
		if (
			vkAllocateMemory(device, &allocateInfo, nullptr, &depthImageMemory) != VK_SUCCESS ||
			vkBindImageMemory(device, depthImage, depthImageMemory, 0) != VK_SUCCESS
		) {
			DestroyDepthResources();
			return Fail("Failed allocating the Vulkan depth image");
		}

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = depthImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = depthFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
			DestroyDepthResources();
			return Fail("Failed creating the Vulkan depth-image view");
		}

		return true;
	}

	bool Context::CreateFramebuffers()
	{
		swapchainFramebuffers.resize(swapchainImageViews.size(), VK_NULL_HANDLE);

		for (std::size_t index = 0; index < swapchainImageViews.size(); ++index) {
			const VkImageView attachments[] = {
				swapchainImageViews[index],
				depthImageView,
			};

			VkFramebufferCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			createInfo.renderPass = renderPass;
			createInfo.attachmentCount = static_cast<uint32_t>(std::size(attachments));
			createInfo.pAttachments = attachments;
			createInfo.width = swapchainExtent.width;
			createInfo.height = swapchainExtent.height;
			createInfo.layers = 1;

			if (vkCreateFramebuffer(device, &createInfo, nullptr, &swapchainFramebuffers[index]) != VK_SUCCESS)
				return Fail("Failed creating a Vulkan swapchain framebuffer");
		}

		return true;
	}

	void Context::DestroySwapchain()
	{
		if (device == VK_NULL_HANDLE)
			return;

		for (const auto framebuffer : swapchainFramebuffers) {
			if (framebuffer != VK_NULL_HANDLE)
				vkDestroyFramebuffer(device, framebuffer, nullptr);
		}
		swapchainFramebuffers.clear();

		terrainRenderer.DestroyPipeline();
		if (graphicsPipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(device, graphicsPipeline, nullptr);
		if (pipelineLayout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		if (renderPass != VK_NULL_HANDLE)
			vkDestroyRenderPass(device, renderPass, nullptr);

		graphicsPipeline = VK_NULL_HANDLE;
		pipelineLayout = VK_NULL_HANDLE;
		renderPass = VK_NULL_HANDLE;

		DestroyDepthResources();

		for (const auto imageView : swapchainImageViews) {
			if (imageView != VK_NULL_HANDLE)
				vkDestroyImageView(device, imageView, nullptr);
		}

		swapchainImageViews.clear();
		swapchainImages.clear();
		imageFences.clear();

		if (swapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(device, swapchain, nullptr);

		swapchain = VK_NULL_HANDLE;
		swapchainFormat = VK_FORMAT_UNDEFINED;
		swapchainExtent = {};
	}

	void Context::DestroyDepthResources()
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (depthImageView != VK_NULL_HANDLE)
			vkDestroyImageView(device, depthImageView, nullptr);
		if (depthImage != VK_NULL_HANDLE)
			vkDestroyImage(device, depthImage, nullptr);
		if (depthImageMemory != VK_NULL_HANDLE)
			vkFreeMemory(device, depthImageMemory, nullptr);

		depthImageView = VK_NULL_HANDLE;
		depthImage = VK_NULL_HANDLE;
		depthImageMemory = VK_NULL_HANDLE;
		depthFormat = VK_FORMAT_UNDEFINED;
	}

	Context::SwapchainSupport Context::QuerySwapchainSupport(VkPhysicalDevice candidate) const
	{
		SwapchainSupport support;
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface, &support.capabilities) != VK_SUCCESS)
			return support;

		uint32_t formatCount = 0;
		if (vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formatCount, nullptr) == VK_SUCCESS && formatCount > 0) {
			support.formats.resize(formatCount);
			if (vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formatCount, support.formats.data()) != VK_SUCCESS)
				support.formats.clear();
		}

		uint32_t presentModeCount = 0;
		if (vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &presentModeCount, nullptr) == VK_SUCCESS && presentModeCount > 0) {
			support.presentModes.resize(presentModeCount);
			if (vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &presentModeCount, support.presentModes.data()) != VK_SUCCESS)
				support.presentModes.clear();
		}

		return support;
	}

	VkSurfaceFormatKHR Context::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
	{
		const auto preferred = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
			return
				format.format == VK_FORMAT_B8G8R8A8_SRGB &&
				format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		});

		return preferred != formats.end() ? *preferred : formats.front();
	}

	VkPresentModeKHR Context::ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const
	{
		const auto preferred = std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR);
		return preferred != presentModes.end() ? *preferred : VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D Context::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
			return capabilities.currentExtent;

		int width = 0;
		int height = 0;
		SDL_Vulkan_GetDrawableSize(window, &width, &height);

		return {
			std::clamp(static_cast<uint32_t>(std::max(width, 0)), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp(static_cast<uint32_t>(std::max(height, 0)), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
		};
	}

	VkCompositeAlphaFlagBitsKHR Context::ChooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities) const
	{
		constexpr VkCompositeAlphaFlagBitsKHR preferredModes[] = {
			VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		};

		for (const auto mode : preferredModes) {
			if ((capabilities.supportedCompositeAlpha & mode) != 0)
				return mode;
		}

		return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	}
}

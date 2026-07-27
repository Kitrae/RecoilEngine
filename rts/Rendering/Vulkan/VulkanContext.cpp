/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanContext.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <set>
#include <string>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "Triangle.frag.spv.h"
#include "Triangle.vert.spv.h"

namespace
{
	constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
	constexpr const char* DEVICE_EXTENSIONS[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo(
		PFN_vkDebugUtilsMessengerCallbackEXT callback,
		void* userData
	) {
		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = callback;
		createInfo.pUserData = userData;
		return createInfo;
	}
}

namespace Vulkan
{
	VKAPI_ATTR VkBool32 VKAPI_CALL Context::DebugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
		void* userData
	) {
		auto* context = static_cast<Context*>(userData);
		if (context == nullptr || callbackData == nullptr || callbackData->pMessage == nullptr)
			return VK_FALSE;

		// The validation layer is the only source with enough context to explain invalid API use.
		context->Log(std::string("[Vulkan] ") + callbackData->pMessage);
		return VK_FALSE;
	}

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
		if (!CreateFramebuffers())
			return false;
		if (!CreateCommandPool())
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

		if (device != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, commandPool, nullptr);

		commandPool = VK_NULL_HANDLE;
		commandBuffers.clear();

		DestroySwapchain();

		if (device != VK_NULL_HANDLE)
			vkDestroyDevice(device, nullptr);

		device = VK_NULL_HANDLE;
		graphicsQueue = VK_NULL_HANDLE;
		presentQueue = VK_NULL_HANDLE;
		physicalDevice = VK_NULL_HANDLE;

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

	bool Context::DrawFrame(const std::array<float, 4>& clearColor)
	{
		if (device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE)
			return Fail("Cannot draw without an initialized Vulkan swapchain");

		int drawableWidth = 0;
		int drawableHeight = 0;
		SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
		if (drawableWidth == 0 || drawableHeight == 0)
			return true;

		const auto frameFence = frameFences[currentFrame];
		if (vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
			return Fail("Failed waiting for the Vulkan frame fence");

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
		if (!CreateFramebuffers())
			return false;
		if (!AllocateCommandBuffers())
			return false;
		if (!CreatePresentSemaphores())
			return false;

		imageFences.assign(swapchainImages.size(), VK_NULL_HANDLE);
		return true;
	}

	bool Context::CreateInstance()
	{
		VkApplicationInfo applicationInfo{};
		applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		applicationInfo.pApplicationName = "RecoilEngine";
		applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
		applicationInfo.pEngineName = "RecoilEngine";
		applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
		applicationInfo.apiVersion = VK_API_VERSION_1_1;

		unsigned int extensionCount = 0;
		if (SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr) != SDL_TRUE)
			return Fail(std::string("SDL could not query Vulkan instance extensions: ") + SDL_GetError());

		std::vector<const char*> extensions(extensionCount);
		if (SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data()) != SDL_TRUE)
			return Fail(std::string("SDL could not provide Vulkan instance extensions: ") + SDL_GetError());

		if (validationEnabled)
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

		const char* layers[] = {
			VALIDATION_LAYER,
		};

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &applicationInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();

		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if (validationEnabled) {
			createInfo.enabledLayerCount = 1;
			createInfo.ppEnabledLayerNames = layers;
			debugCreateInfo = MakeDebugMessengerCreateInfo(DebugCallback, this);
			createInfo.pNext = &debugCreateInfo;
		}

		const auto result = vkCreateInstance(&createInfo, nullptr, &instance);
		if (result != VK_SUCCESS)
			return Fail("Failed creating the Vulkan instance: " + std::to_string(result));

		return true;
	}

	bool Context::CreateDebugMessenger()
	{
		if (!validationEnabled)
			return true;

		const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")
		);
		if (createMessenger == nullptr)
			return Fail("The Vulkan loader does not expose vkCreateDebugUtilsMessengerEXT");

		const auto createInfo = MakeDebugMessengerCreateInfo(DebugCallback, this);
		const auto result = createMessenger(instance, &createInfo, nullptr, &debugMessenger);
		if (result != VK_SUCCESS)
			return Fail("Failed creating the Vulkan debug messenger: " + std::to_string(result));

		return true;
	}

	bool Context::CreateSurface()
	{
		if (SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE)
			return Fail(std::string("SDL could not create the Vulkan surface: ") + SDL_GetError());

		return true;
	}

	bool Context::SelectPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0)
			return Fail("No Vulkan physical devices are available");

		std::vector<VkPhysicalDevice> candidates(deviceCount);
		if (vkEnumeratePhysicalDevices(instance, &deviceCount, candidates.data()) != VK_SUCCESS)
			return Fail("Failed enumerating Vulkan physical devices");

		int bestScore = -1;
		for (const auto candidate : candidates) {
			const auto queueFamilies = FindQueueFamilies(candidate);
			if (!queueFamilies.IsComplete() || !CheckDeviceExtensionSupport(candidate))
				continue;

			const auto swapchainSupport = QuerySwapchainSupport(candidate);
			if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty())
				continue;

			VkPhysicalDeviceProperties properties{};
			vkGetPhysicalDeviceProperties(candidate, &properties);

			int score = static_cast<int>(properties.limits.maxImageDimension2D);
			if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				score += 100000;

			if (score <= bestScore)
				continue;

			bestScore = score;
			physicalDevice = candidate;
			graphicsQueueFamily = queueFamilies.graphics.value();
			presentQueueFamily = queueFamilies.present.value();
			deviceName = properties.deviceName;
		}

		if (physicalDevice == VK_NULL_HANDLE)
			return Fail("No Vulkan device supports graphics, presentation, and VK_KHR_swapchain");

		return true;
	}

	bool Context::CreateDevice()
	{
		const std::set<uint32_t> queueFamilies = {
			graphicsQueueFamily,
			presentQueueFamily,
		};

		constexpr float queuePriority = 1.0f;
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		queueCreateInfos.reserve(queueFamilies.size());

		for (const auto queueFamily : queueFamilies) {
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceFeatures features{};

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.pEnabledFeatures = &features;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(DEVICE_EXTENSIONS));
		createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS;

		const auto result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
		if (result != VK_SUCCESS)
			return Fail("Failed creating the Vulkan device: " + std::to_string(result));

		vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
		vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);
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

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentReference;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		createInfo.attachmentCount = 1;
		createInfo.pAttachments = &colorAttachment;
		createInfo.subpassCount = 1;
		createInfo.pSubpasses = &subpass;
		createInfo.dependencyCount = 1;
		createInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(device, &createInfo, nullptr, &renderPass) != VK_SUCCESS)
			return Fail("Failed creating the Vulkan render pass");

		return true;
	}

	bool Context::CreateShaderModule(const uint32_t* code, std::size_t size, VkShaderModule& shaderModule)
	{
		if (code == nullptr || size == 0 || (size % sizeof(uint32_t)) != 0)
			return Fail("Invalid SPIR-V shader bytecode");

		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = size;
		createInfo.pCode = code;

		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
			return Fail("Failed creating a Vulkan shader module");

		return true;
	}

	bool Context::CreateGraphicsPipeline()
	{
		VkShaderModule vertexShader = VK_NULL_HANDLE;
		VkShaderModule fragmentShader = VK_NULL_HANDLE;

		if (!CreateShaderModule(RECOIL_VULKAN_TRIANGLE_VERT_SPV, sizeof(RECOIL_VULKAN_TRIANGLE_VERT_SPV), vertexShader))
			return false;

		if (!CreateShaderModule(RECOIL_VULKAN_TRIANGLE_FRAG_SPV, sizeof(RECOIL_VULKAN_TRIANGLE_FRAG_SPV), fragmentShader)) {
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

		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
			vkDestroyShaderModule(device, fragmentShader, nullptr);
			vkDestroyShaderModule(device, vertexShader, nullptr);
			return Fail("Failed creating the Vulkan pipeline layout");
		}

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = static_cast<uint32_t>(std::size(shaderStages));
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;

		const auto result = vkCreateGraphicsPipelines(
			device,
			VK_NULL_HANDLE,
			1,
			&pipelineInfo,
			nullptr,
			&graphicsPipeline
		);

		vkDestroyShaderModule(device, fragmentShader, nullptr);
		vkDestroyShaderModule(device, vertexShader, nullptr);

		if (result != VK_SUCCESS)
			return Fail("Failed creating the Vulkan graphics pipeline: " + std::to_string(result));

		return true;
	}

	bool Context::CreateFramebuffers()
	{
		swapchainFramebuffers.resize(swapchainImageViews.size(), VK_NULL_HANDLE);

		for (std::size_t index = 0; index < swapchainImageViews.size(); ++index) {
			const VkImageView attachments[] = {
				swapchainImageViews[index],
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

		VkClearValue clearValue{};
		std::copy(clearColor.begin(), clearColor.end(), clearValue.color.float32);

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
		renderPassInfo.renderArea.extent = swapchainExtent;
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearValue;

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

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		vkCmdEndRenderPass(commandBuffer);

		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
			return Fail("Failed ending a Vulkan command buffer");

		return true;
	}

	void Context::DestroyDebugMessenger()
	{
		if (instance == VK_NULL_HANDLE || debugMessenger == VK_NULL_HANDLE)
			return;

		const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
		);
		if (destroyMessenger != nullptr)
			destroyMessenger(instance, debugMessenger, nullptr);

		debugMessenger = VK_NULL_HANDLE;
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

		if (graphicsPipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(device, graphicsPipeline, nullptr);
		if (pipelineLayout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		if (renderPass != VK_NULL_HANDLE)
			vkDestroyRenderPass(device, renderPass, nullptr);

		graphicsPipeline = VK_NULL_HANDLE;
		pipelineLayout = VK_NULL_HANDLE;
		renderPass = VK_NULL_HANDLE;

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

	bool Context::CheckValidationLayerSupport() const
	{
		uint32_t layerCount = 0;
		if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) != VK_SUCCESS)
			return false;

		std::vector<VkLayerProperties> layers(layerCount);
		if (vkEnumerateInstanceLayerProperties(&layerCount, layers.data()) != VK_SUCCESS)
			return false;

		return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer) {
			return std::strcmp(layer.layerName, VALIDATION_LAYER) == 0;
		});
	}

	bool Context::CheckDeviceExtensionSupport(VkPhysicalDevice candidate) const
	{
		uint32_t extensionCount = 0;
		if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr) != VK_SUCCESS)
			return false;

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, availableExtensions.data()) != VK_SUCCESS)
			return false;

		std::set<std::string> requiredExtensions(std::begin(DEVICE_EXTENSIONS), std::end(DEVICE_EXTENSIONS));
		for (const auto& extension : availableExtensions)
			requiredExtensions.erase(extension.extensionName);

		return requiredExtensions.empty();
	}

	Context::QueueFamilies Context::FindQueueFamilies(VkPhysicalDevice candidate) const
	{
		QueueFamilies result;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

		for (uint32_t index = 0; index < queueFamilyCount; ++index) {
			if ((queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
				result.graphics = index;

			VkBool32 presentSupport = VK_FALSE;
			if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface, &presentSupport) == VK_SUCCESS && presentSupport)
				result.present = index;

			if (result.IsComplete())
				break;
		}

		return result;
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

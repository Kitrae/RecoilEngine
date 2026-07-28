/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanContext.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

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
			maxTextureSize = properties.limits.maxImageDimension2D;
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
}

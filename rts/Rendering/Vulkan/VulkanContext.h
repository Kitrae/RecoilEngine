/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

struct SDL_Window;

namespace Vulkan
{
	class Context
	{
	public:
		using LogCallback = void (*)(const char* message);

		Context() = default;
		~Context();

		Context(const Context&) = delete;
		Context(Context&&) = delete;
		Context& operator=(const Context&) = delete;
		Context& operator=(Context&&) = delete;

		bool Initialize(SDL_Window* window, bool enableValidation, LogCallback logCallback = nullptr);
		void Shutdown();

		bool DrawFrame(const std::array<float, 4>& clearColor);
		bool RecreateSwapchain();

		const std::string& GetDeviceName() const { return deviceName; }
		const std::string& GetLastError() const { return lastError; }

	private:
		static constexpr std::size_t MAX_FRAMES_IN_FLIGHT = 2;

		struct QueueFamilies
		{
			std::optional<uint32_t> graphics;
			std::optional<uint32_t> present;

			bool IsComplete() const { return graphics.has_value() && present.has_value(); }
		};

		struct SwapchainSupport
		{
			VkSurfaceCapabilitiesKHR capabilities{};
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentModes;
		};

		bool CreateInstance();
		bool CreateDebugMessenger();
		bool CreateSurface();
		bool SelectPhysicalDevice();
		bool CreateDevice();
		bool CreateSwapchain();
		bool CreateRenderPass();
		bool CreateGraphicsPipeline();
		bool CreateFramebuffers();
		bool CreateCommandPool();
		bool AllocateCommandBuffers();
		bool CreateSyncObjects();
		bool CreatePresentSemaphores();
		bool CreateShaderModule(const uint32_t* code, std::size_t size, VkShaderModule& shaderModule);
		bool RecordCommandBuffer(uint32_t imageIndex, const std::array<float, 4>& clearColor);
		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageTypes,
			const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
			void* userData
		);

		void DestroyDebugMessenger();
		void DestroySwapchain();
		void DestroySyncObjects();

		bool CheckValidationLayerSupport() const;
		bool CheckDeviceExtensionSupport(VkPhysicalDevice candidate) const;
		QueueFamilies FindQueueFamilies(VkPhysicalDevice candidate) const;
		SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice candidate) const;

		VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
		VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
		VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
		VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities) const;

		bool Fail(const std::string& message);
		void Log(const std::string& message) const;

	private:
		SDL_Window* window = nullptr;
		LogCallback logCallback = nullptr;
		bool validationEnabled = false;

		VkInstance instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		VkQueue graphicsQueue = VK_NULL_HANDLE;
		VkQueue presentQueue = VK_NULL_HANDLE;
		uint32_t graphicsQueueFamily = 0;
		uint32_t presentQueueFamily = 0;

		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D swapchainExtent{};
		std::vector<VkImage> swapchainImages;
		std::vector<VkImageView> swapchainImageViews;
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> swapchainFramebuffers;

		VkCommandPool commandPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> commandBuffers;

		std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableSemaphores{};
		std::array<VkFence, MAX_FRAMES_IN_FLIGHT> frameFences{};
		std::vector<VkSemaphore> renderFinishedSemaphores;
		std::vector<VkFence> imageFences;
		std::size_t currentFrame = 0;

		std::string deviceName;
		std::string lastError;
	};
}

/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "VulkanBuffer.h"
#include "VulkanTerrainRenderer.h"
#include "VulkanTexture.h"

struct SDL_Window;

namespace Vulkan
{
	using TextureHandle = uint32_t;
	static constexpr TextureHandle INVALID_TEXTURE_HANDLE = UINT32_MAX;
	using BufferHandle = uint32_t;
	static constexpr BufferHandle INVALID_BUFFER_HANDLE = UINT32_MAX;

	struct Vertex2D
	{
		std::array<float, 2> position;
		std::array<float, 2> textureCoordinates;
		std::array<uint8_t, 4> color;
	};

	struct DrawRange
	{
		uint32_t firstIndex;
		uint32_t indexCount;
		TextureHandle texture;
	};

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

		void BeginFrame();
		bool DrawFrame(const std::array<float, 4>& clearColor);
		bool RecreateSwapchain();
		std::optional<TextureHandle> CreateTexture(
			const void* pixels,
			std::size_t size,
			uint32_t width,
			uint32_t height,
			TextureFormat format
		);
		std::optional<TextureHandle> CreateCubemap(
			const void* pixels,
			std::size_t size,
			uint32_t faceSize,
			TextureFormat format
		);
		bool UpdateTexture(
			TextureHandle handle,
			const void* pixels,
			std::size_t size,
			uint32_t width,
			uint32_t height,
			TextureFormat format
		);
		std::optional<TextureHandle> CreateTextureRGBA8(const uint8_t* pixels, uint32_t width, uint32_t height);
		bool UpdateTextureRGBA8(TextureHandle handle, const uint8_t* pixels, uint32_t width, uint32_t height);
		bool DestroyTexture(TextureHandle handle);
		std::optional<BufferHandle> CreateDataBuffer(
			const void* data,
			std::size_t dataSize,
			std::size_t capacity,
			BufferType type
		);
		bool UpdateDataBuffer(BufferHandle handle, const void* data, std::size_t size, std::size_t offset);
		bool DestroyDataBuffer(BufferHandle handle);
		bool SetDrawBatch(
			std::span<const Vertex2D> vertices,
			std::span<const uint32_t> indices,
			std::span<const DrawRange> ranges
		);
		bool AppendDrawBatch(
			std::span<const Vertex2D> vertices,
			std::span<const uint32_t> indices,
			std::span<const DrawRange> ranges
		);
		bool SetTerrain(
			std::span<const TerrainVertex> vertices,
			std::span<const uint32_t> indices,
			const std::array<TextureHandle, 3>& textures,
			uint32_t instanceCount,
			const std::array<uint32_t, 4>& terrainInfo
		);
		void SetTerrainTransform(const std::array<float, 16>& transform);
		void ClearTerrain();
		bool UploadTextureRGBA8(const uint8_t* pixels, uint32_t width, uint32_t height);

		const std::string& GetDeviceName() const { return deviceName; }
		const std::string& GetLastError() const { return lastError; }
		uint32_t GetMaxTextureSize() const { return maxTextureSize; }
		TextureHandle GetSolidTexture() const { return solidTexture; }
		TextureHandle GetStartupTexture() const { return startupTexture; }

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

		struct FrameGeometry
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			void* mappedMemory = nullptr;
			VkDeviceSize capacity = 0;
			VkDeviceSize indexOffset = 0;
			uint32_t indexCount = 0;
		};

		struct Texture
		{
			VkImage image = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			VkImageView imageView = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
			VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			VkFormat format = VK_FORMAT_UNDEFINED;
			VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
			uint32_t arrayLayers = 1;
		};

		struct DataBuffer
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			void* mappedMemory = nullptr;
			VkDeviceSize capacity = 0;
		};

		bool CreateInstance();
		bool CreateDebugMessenger();
		bool CreateSurface();
		bool SelectPhysicalDevice();
		bool CreateDevice();
		bool CreateSwapchain();
		bool CreateRenderPass();
		bool CreateGraphicsPipeline();
		bool CreateDepthResources();
		bool CreateFramebuffers();
		bool CreateCommandPool();
		bool AllocateCommandBuffers();
		bool CreateGeometryBuffers();
		bool CreateSyncObjects();
		bool CreatePresentSemaphores();
		bool CreateShaderModule(const uint32_t* code, std::size_t size, VkShaderModule& shaderModule);
		bool CreateBuffer(
			VkDeviceSize size,
			VkBufferUsageFlags usage,
			VkMemoryPropertyFlags properties,
			VkBuffer& buffer,
			VkDeviceMemory& memory
		);
		bool ResizeGeometryBuffer(FrameGeometry& geometry, VkDeviceSize requiredSize);
		bool UpdateGeometryBuffer(std::size_t frameIndex);
		bool ValidateDrawBatch(
			std::span<const Vertex2D> vertices,
			std::span<const uint32_t> indices,
			std::span<const DrawRange> ranges
		);
		bool CreateImage(
			uint32_t width,
			uint32_t height,
			uint32_t arrayLayers,
			VkImageCreateFlags flags,
			VkFormat format,
			VkImage& image,
			VkDeviceMemory& memory
		);
		bool SubmitTextureUpload(
			VkBuffer stagingBuffer,
			VkImage image,
			uint32_t width,
			uint32_t height,
			uint32_t arrayLayers,
			VkDeviceSize layerSize
		);
		bool CreateTextureResource(
			const void* pixels,
			std::size_t size,
			uint32_t width,
			uint32_t height,
			uint32_t arrayLayers,
			VkImageCreateFlags flags,
			VkImageViewType viewType,
			TextureFormat format,
			Texture& texture
		);
		bool CreateTextureDescriptor(Texture& texture);
		bool RecordCommandBuffer(uint32_t imageIndex, const std::array<float, 4>& clearColor);
		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageTypes,
			const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
			void* userData
		);

		void DestroyDebugMessenger();
		void DestroyDataBuffer(DataBuffer& buffer);
		void DestroyDataBuffers();
		void DestroyGeometryBuffers();
		void DestroyGeometryBuffer(FrameGeometry& geometry);
		void DestroyDepthResources();
		void DestroySwapchain();
		void DestroySyncObjects();
		void DestroyTexture(Texture& texture);
		void DestroyTextures();

		bool CheckValidationLayerSupport() const;
		bool CheckDeviceExtensionSupport(VkPhysicalDevice candidate) const;
		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
		VkFormat FindDepthFormat() const;
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
		uint32_t maxTextureSize = 0;

		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D swapchainExtent{};
		std::vector<VkImage> swapchainImages;
		std::vector<VkImageView> swapchainImageViews;
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkFormat depthFormat = VK_FORMAT_UNDEFINED;
		VkImage depthImage = VK_NULL_HANDLE;
		VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
		VkImageView depthImageView = VK_NULL_HANDLE;
		VkDescriptorSetLayout textureDescriptorSetLayout = VK_NULL_HANDLE;
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline graphicsPipeline = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> swapchainFramebuffers;

		std::vector<Texture> textures;
		std::vector<DataBuffer> dataBuffers;
		TextureHandle solidTexture = INVALID_TEXTURE_HANDLE;
		TextureHandle startupTexture = INVALID_TEXTURE_HANDLE;

		std::array<FrameGeometry, MAX_FRAMES_IN_FLIGHT> frameGeometry;
		std::vector<Vertex2D> drawVertices;
		std::vector<uint32_t> drawIndices;
		std::vector<DrawRange> drawRanges;
		TerrainRenderer terrainRenderer;
		std::array<TextureHandle, 3> terrainTextures{
			INVALID_TEXTURE_HANDLE,
			INVALID_TEXTURE_HANDLE,
			INVALID_TEXTURE_HANDLE,
		};

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

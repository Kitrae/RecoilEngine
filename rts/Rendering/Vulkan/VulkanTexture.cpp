/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanContext.h"

#include <cstring>
#include <limits>
#include <utility>

namespace Vulkan
{
	bool Context::CreateImage(uint32_t width, uint32_t height, VkImage& image, VkDeviceMemory& memory)
	{
		VkPhysicalDeviceProperties deviceProperties{};
		vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
		if (width > deviceProperties.limits.maxImageDimension2D || height > deviceProperties.limits.maxImageDimension2D)
			return Fail("The Vulkan texture exceeds the device's maximum 2D image dimension");

		VkFormatProperties formatProperties{};
		vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8G8B8A8_SRGB, &formatProperties);
		constexpr VkFormatFeatureFlags requiredFeatures =
			VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
			VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
		if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
			return Fail("The Vulkan device cannot upload and sample RGBA8 sRGB textures");

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = {width, height, 1};
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
			return Fail("Failed creating a Vulkan texture image");

		VkMemoryRequirements memoryRequirements{};
		vkGetImageMemoryRequirements(device, image, &memoryRequirements);

		const uint32_t memoryType = FindMemoryType(
			memoryRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		if (memoryType == std::numeric_limits<uint32_t>::max()) {
			vkDestroyImage(device, image, nullptr);
			image = VK_NULL_HANDLE;
			return Fail("No compatible Vulkan texture memory type is available");
		}

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryRequirements.size;
		allocateInfo.memoryTypeIndex = memoryType;

		if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
			vkDestroyImage(device, image, nullptr);
			image = VK_NULL_HANDLE;
			return Fail("Failed allocating Vulkan texture memory");
		}

		if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
			vkFreeMemory(device, memory, nullptr);
			vkDestroyImage(device, image, nullptr);
			memory = VK_NULL_HANDLE;
			image = VK_NULL_HANDLE;
			return Fail("Failed binding Vulkan texture memory");
		}

		return true;
	}

	bool Context::SubmitTextureUpload(VkBuffer stagingBuffer, VkImage image, uint32_t width, uint32_t height)
	{
		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) != VK_SUCCESS)
			return Fail("Failed allocating a Vulkan texture upload command buffer");

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
			return Fail("Failed beginning a Vulkan texture upload command buffer");
		}

		VkImageMemoryBarrier uploadBarrier{};
		uploadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		uploadBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		uploadBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		uploadBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		uploadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		uploadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		uploadBarrier.image = image;
		uploadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		uploadBarrier.subresourceRange.levelCount = 1;
		uploadBarrier.subresourceRange.layerCount = 1;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&uploadBarrier
		);

		VkBufferImageCopy copyRegion{};
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = {width, height, 1};
		vkCmdCopyBufferToImage(
			commandBuffer,
			stagingBuffer,
			image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion
		);

		VkImageMemoryBarrier sampleBarrier = uploadBarrier;
		sampleBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		sampleBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		sampleBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		sampleBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		vkCmdPipelineBarrier(
			commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0,
			nullptr,
			0,
			nullptr,
			1,
			&sampleBarrier
		);

		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
			vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
			return Fail("Failed ending a Vulkan texture upload command buffer");
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		const auto submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		const auto waitResult = submitResult == VK_SUCCESS ? vkQueueWaitIdle(graphicsQueue) : VK_ERROR_UNKNOWN;
		vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

		if (submitResult != VK_SUCCESS)
			return Fail("Failed submitting the Vulkan texture upload");
		if (waitResult != VK_SUCCESS)
			return Fail("Failed waiting for the Vulkan texture upload");

		return true;
	}

	bool Context::CreateTextureDescriptor(Texture& texture)
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = texture.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(device, &viewInfo, nullptr, &texture.imageView) != VK_SUCCESS)
			return Fail("Failed creating a Vulkan texture image view");

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxLod = 0.0f;
		if (vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS)
			return Fail("Failed creating a Vulkan texture sampler");

		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &texture.descriptorPool) != VK_SUCCESS)
			return Fail("Failed creating a Vulkan texture descriptor pool");

		VkDescriptorSetAllocateInfo descriptorInfo{};
		descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorInfo.descriptorPool = texture.descriptorPool;
		descriptorInfo.descriptorSetCount = 1;
		descriptorInfo.pSetLayouts = &textureDescriptorSetLayout;
		if (vkAllocateDescriptorSets(device, &descriptorInfo, &texture.descriptorSet) != VK_SUCCESS)
			return Fail("Failed allocating a Vulkan texture descriptor set");

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = texture.sampler;
		imageInfo.imageView = texture.imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = texture.descriptorSet;
		descriptorWrite.dstBinding = 0;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
		return true;
	}

	bool Context::CreateTextureResource(const uint8_t* pixels, uint32_t width, uint32_t height, Texture& texture)
	{
		if (pixels == nullptr || width == 0 || height == 0)
			return Fail("Cannot upload an empty Vulkan texture");

		constexpr VkDeviceSize bytesPerPixel = 4;
		constexpr VkDeviceSize maxSize = std::numeric_limits<VkDeviceSize>::max();
		if (width > maxSize / height / bytesPerPixel)
			return Fail("The Vulkan texture dimensions are too large");
		const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
		if (!CreateBuffer(
			imageSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingMemory
		)) {
			return false;
		}

		void* mappedMemory = nullptr;
		if (vkMapMemory(device, stagingMemory, 0, imageSize, 0, &mappedMemory) != VK_SUCCESS) {
			vkDestroyBuffer(device, stagingBuffer, nullptr);
			vkFreeMemory(device, stagingMemory, nullptr);
			return Fail("Failed mapping Vulkan texture staging memory");
		}
		std::memcpy(mappedMemory, pixels, static_cast<std::size_t>(imageSize));
		vkUnmapMemory(device, stagingMemory);

		bool uploaded = CreateImage(width, height, texture.image, texture.memory);
		if (uploaded)
			uploaded = SubmitTextureUpload(stagingBuffer, texture.image, width, height);
		if (uploaded)
			uploaded = CreateTextureDescriptor(texture);

		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingMemory, nullptr);

		if (!uploaded) {
			DestroyTexture(texture);
			return false;
		}

		return true;
	}

	std::optional<TextureHandle> Context::CreateTextureRGBA8(const uint8_t* pixels, uint32_t width, uint32_t height)
	{
		if (textures.size() >= INVALID_TEXTURE_HANDLE) {
			Fail("The Vulkan texture handle space is exhausted");
			return std::nullopt;
		}
		if (vkDeviceWaitIdle(device) != VK_SUCCESS) {
			Fail("Failed waiting for the Vulkan device before uploading a texture");
			return std::nullopt;
		}

		Texture texture;
		if (!CreateTextureResource(pixels, width, height, texture))
			return std::nullopt;

		const auto handle = static_cast<TextureHandle>(textures.size());
		textures.push_back(texture);
		return handle;
	}

	bool Context::UpdateTextureRGBA8(
		TextureHandle handle,
		const uint8_t* pixels,
		uint32_t width,
		uint32_t height
	) {
		if (handle >= textures.size() || textures[handle].descriptorSet == VK_NULL_HANDLE)
			return Fail("Cannot update an invalid Vulkan texture");

		Texture replacement;
		if (!CreateTextureResource(pixels, width, height, replacement))
			return false;

		std::swap(textures[handle], replacement);
		DestroyTexture(replacement);
		return true;
	}

	bool Context::UploadTextureRGBA8(const uint8_t* pixels, uint32_t width, uint32_t height)
	{
		const auto texture = CreateTextureRGBA8(pixels, width, height);
		if (!texture.has_value())
			return false;

		const auto previousStartupTexture = startupTexture;
		startupTexture = texture.value();
		for (auto& range : drawRanges) {
			if (range.texture == previousStartupTexture)
				range.texture = startupTexture;
		}

		return true;
	}

	void Context::DestroyTexture(Texture& texture)
	{
		if (device == VK_NULL_HANDLE)
			return;

		if (texture.descriptorPool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(device, texture.descriptorPool, nullptr);
		if (texture.sampler != VK_NULL_HANDLE)
			vkDestroySampler(device, texture.sampler, nullptr);
		if (texture.imageView != VK_NULL_HANDLE)
			vkDestroyImageView(device, texture.imageView, nullptr);
		if (texture.image != VK_NULL_HANDLE)
			vkDestroyImage(device, texture.image, nullptr);
		if (texture.memory != VK_NULL_HANDLE)
			vkFreeMemory(device, texture.memory, nullptr);

		texture = {};
	}

	void Context::DestroyTextures()
	{
		for (auto& texture : textures)
			DestroyTexture(texture);

		textures.clear();
		startupTexture = INVALID_TEXTURE_HANDLE;
	}
}

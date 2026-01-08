#include "vulkanexamplebase.h"

class VulkanExample : public VulkanExampleBase
{
public:
	struct UniformData
	{
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
	} uniformData;
	std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	std::array<VkDescriptorSet, maxConcurrentFrames> descriptorSets;

	VulkanExample()
	{
		title = "Triangle";
		camera.type = Camera::CameraType::lookat;
	}

	~VulkanExample()
	{

	}

	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers)
		{
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer, sizeof(UniformData)));
			VK_CHECK_RESULT(buffer.map());
		}
	}

	void setupDescriptors()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames),
		};
		VkDescriptorPoolCreateInfo descriptorPoolCI = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames);
		vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool);

		std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings =
		{
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(descriptorSetLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout))

		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		for (size_t i = 0; i < uniformBuffers.size(); i++)
		{
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i]));
			std::vector<VkWriteDescriptorSet> writeDescriptorSet =
			{
				vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSet.size()), writeDescriptorSet.data(), 0, nullptr);
		}
	}

	void preparePipelines()
	{

	}

	void prepare() override
	{
		VulkanExampleBase::prepare();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		prepared = true;
	}

	void updateUniformBuffers()
	{
		uniformData.projection = camera.matrices.perspective;
		uniformData.view = camera.matrices.view;
		uniformData.model = glm::mat4(1.0f);
		memcpy(uniformBuffers[currentBuffer].mapped, &uniformData, sizeof(UniformData));
	}

	void buildCommandBuffer()
	{
		
	}

	void render() override
	{
		if (!prepared) return;
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}
};

VULKAN_EXAMPLE_MAIN()
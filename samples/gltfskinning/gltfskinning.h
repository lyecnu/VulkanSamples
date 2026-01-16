#pragma once

#include "vulkanexamplebase.h"

class VulkanglTFModel
{
public:
	vks::VulkanDevice* vulkanDevice;
	VkQueue copyQueue;

	struct Vertices
	{
		VkBuffer       buffer;
		VkDeviceMemory memory;
	} vertices;

	struct Indices
	{
		int            count;
		VkBuffer       buffer;
		VkDeviceMemory memory;
	} indices;

	struct Node;

	struct Material
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		uint32_t  baseColorTextureIndex;
	};

	struct Image
	{
		vks::Texture2D  texture;
		VkDescriptorSet descriptorSet;
	};

	struct Texture
	{
		int32_t imageIndex;
	};

	struct Primitive
	{
		uint32_t firstIndex;
		uint32_t indexCount;
		int32_t  materialIndex;
	};

	struct Mesh
	{
		std::vector<Primitive> primitives;
	};

	struct Node
	{
		Node* parent;
		uint32_t           index;
		std::vector<Node*> children;
		Mesh               mesh;
		glm::vec3          translation{};
		glm::vec3          scale{ 1.0f };
		glm::quat          rotation{};
		int32_t            skin = -1;
		glm::mat4          matrix;
		glm::mat4          getLocalMatrix();
	};

	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 color;
		glm::vec4 jointIndices;
		glm::vec4 jointWeights;
	};

	struct Skin
	{
		std::string                 name;
		Node* skeletonRoot = nullptr;
		std::vector<glm::mat4> inverseBindMatrices;
		std::vector<Node*> joints;
		std::array<vks::Buffer, maxConcurrentFrames> storageBuffers;
		std::array<vks::Buffer, maxConcurrentFrames> descriptorSets;
	};



	std::vector<Image>     images;
	std::vector<Texture>   textures;
	std::vector<Material>  materials;
	std::vector<Node*>    nodes;
	std::vector<Skin>      skins;
	std::vector<Animation> animations;
};
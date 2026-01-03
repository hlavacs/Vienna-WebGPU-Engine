#include "engine/scene/Scene.h"
#include "engine/scene/nodes/CameraNode.h"
#include "engine/scene/nodes/LightNode.h"
#include "engine/scene/nodes/RenderNode.h"
#include "engine/scene/nodes/UpdateNode.h"
#include <algorithm>
#include <functional>

namespace engine::scene
{

Scene::Scene() : m_root(nullptr)
{
	// Always create a root node
	m_root = std::make_shared<nodes::Node>();

	// Always create a camera node as a child of the root
	auto cameraNode = std::make_shared<nodes::CameraNode>();
	m_root->addChild(cameraNode);
	setMainCamera(cameraNode);
}

void Scene::update(float deltaTime)
{
	if (!m_root)
		return;

	// Process all UpdateNodes in the scene graph
	std::function<void(nodes::Node::Ptr, float)> processUpdateNodes;
	processUpdateNodes = [&processUpdateNodes](nodes::Node::Ptr node, float dt)
	{
		if (node->isEnabled())
		{
			// If this is an UpdateNode, call its update method
			if (node->isUpdate())
			{
				auto updateNode = node->asUpdateNode();
				if (updateNode)
				{
					updateNode->update(dt);
				}
			}

			// Process children
			for (const auto &child : node->getChildren())
			{
				processUpdateNodes(child, dt);
			}
		}
	};

	// Start traversal from root
	processUpdateNodes(m_root, deltaTime);
}

void Scene::lateUpdate(float deltaTime)
{
	if (!m_root)
		return;

	// Process all UpdateNodes in the scene graph for lateUpdate
	std::function<void(nodes::Node::Ptr, float)> processLateUpdateNodes;
	processLateUpdateNodes = [&processLateUpdateNodes](nodes::Node::Ptr node, float dt)
	{
		if (node->isEnabled())
		{
			// If this is an UpdateNode, call its lateUpdate method
			if (node->isUpdate())
			{
				auto updateNode = node->asUpdateNode();
				if (updateNode)
				{
					updateNode->lateUpdate(dt);
				}
			}

			// Process children
			for (const auto &child : node->getChildren())
			{
				processLateUpdateNodes(child, dt);
			}
		}
	};

	// Start traversal from root
	processLateUpdateNodes(m_root, deltaTime);
}

void Scene::collectRenderProxies(
	std::vector<std::shared_ptr<engine::rendering::RenderProxy>> &outProxies,
	std::vector<engine::rendering::LightStruct> &outLights
)
{
	if (!m_root)
		return;

	// Process all RenderNodes in the scene graph to collect render proxies
	std::function<void(nodes::Node::Ptr)> processRenderNodes;
	processRenderNodes = [&](nodes::Node::Ptr node)
	{
		if (node->isEnabled())
		{
			if (node->isRender())
			{
				auto renderNode = node->asRenderNode();
				if (renderNode)
				{
					// Collect render proxies (models, lights, UI, debug)
					renderNode->collectRenderProxies(outProxies);
				}
			}

			for (const auto &child : node->getChildren())
			{
				processRenderNodes(child);
			}
		}
	};

	// Start traversal from root
	processRenderNodes(m_root);

	// Sort proxies by layer (lower layers render first)
	std::sort(
		outProxies.begin(),
		outProxies.end(),
		[](const auto &a, const auto &b)
		{
			return a->getLayer() < b->getLayer();
		}
	);

	// Extract lights from LightRenderProxies
	for (const auto &proxy : outProxies)
	{
		if (auto lightProxy = std::dynamic_pointer_cast<engine::rendering::LightRenderProxy>(proxy))
		{
			outLights.push_back(lightProxy->lightData);
		}
		else if (auto cameraProxy = std::dynamic_pointer_cast<engine::rendering::CameraRenderProxy>(proxy))
		{
			// Register camera with scene if it's not already registered
			if (cameraProxy->camera)
			{
				addCamera(cameraProxy->camera);
			}
		}
	}
}

void Scene::collectDebugData()
{
	if (!m_root)
		return;

	// Clear previous debug data
	m_debugCollector.clear();

	// Process all nodes with debug enabled
	std::function<void(nodes::Node::Ptr)> processDebugNodes;
	processDebugNodes = [&processDebugNodes, this](nodes::Node::Ptr node)
	{
		if (node->isEnabled() && node->isDebugEnabled())
		{
			// Call the node's debug draw method
			node->onDebugDraw(m_debugCollector);
		}

		// Process children regardless of debug state (children may have debug enabled)
		for (const auto &child : node->getChildren())
		{
			processDebugNodes(child);
		}
	};

	// Start traversal from root
	processDebugNodes(m_root);
}

void Scene::preRender()
{
	if (!m_root)
		return;

	for (auto &cam : m_cameras)
	{
		if (cam->isEnabled())
			cam->preRender();
	}

	// Process all RenderNodes in the scene graph for preRender
	std::function<void(nodes::Node::Ptr)> processPreRenderNodes;
	processPreRenderNodes = [&processPreRenderNodes](nodes::Node::Ptr node)
	{
		if (node->isEnabled())
		{
			// If this is a RenderNode, call its preRender method
			if (node->isRender())
			{
				auto renderNode = node->asRenderNode();
				if (renderNode)
				{
					renderNode->preRender();
				}
			}

			// Process children
			for (const auto &child : node->getChildren())
			{
				processPreRenderNodes(child);
			}
		}
	};

	// Start traversal from root
	processPreRenderNodes(m_root);
}

void Scene::postRender()
{
	if (!m_root)
		return;

	// Process all RenderNodes in the scene graph for postRender
	std::function<void(nodes::Node::Ptr)> processPostRenderNodes;
	processPostRenderNodes = [&processPostRenderNodes](nodes::Node::Ptr node)
	{
		if (node->isEnabled())
		{
			// If this is a RenderNode, call its postRender method
			if (node->isRender())
			{
				auto renderNode = node->asRenderNode();
				if (renderNode)
				{
					renderNode->postRender();
				}
			}

			// Process children
			for (const auto &child : node->getChildren())
			{
				processPostRenderNodes(child);
			}
		}
	};

	// Start traversal from root
	processPostRenderNodes(m_root);
}

} // namespace engine::scene
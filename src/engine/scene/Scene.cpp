#include "engine/scene/Scene.h"
#include "engine/scene/entity/RenderNode.h"
#include "engine/scene/entity/UpdateNode.h"
#include <functional>

namespace engine::scene
{

Scene::Scene() : m_root(nullptr), m_activeCamera(nullptr)
{
	// Always create a root node
	m_root = std::make_shared<entity::Node>();

	// Always create a camera node as a child of the root
	auto cameraNode = std::make_shared<CameraNode>();
	m_root->addChild(cameraNode);
	m_activeCamera = cameraNode;
}

void Scene::onFrame(float deltaTime)
{
	// Clear render collector at the start of each frame
	m_renderCollector.clear();
	
	// Complete frame lifecycle
	update(deltaTime);
	lateUpdate(deltaTime);
	collectRenderData(); // Collect and sort render items
	preRender();         // Prepare nodes for rendering
	// Note: actual rendering happens in Application via Renderer
	// postRender() is called by Application after rendering
}

void Scene::update(float deltaTime)
{
	if (!m_root)
		return;

	// Process all UpdateNodes in the scene graph
	std::function<void(entity::Node::Ptr, float)> processUpdateNodes;
	processUpdateNodes = [&processUpdateNodes](entity::Node::Ptr node, float dt)
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
	std::function<void(entity::Node::Ptr, float)> processLateUpdateNodes;
	processLateUpdateNodes = [&processLateUpdateNodes](entity::Node::Ptr node, float dt)
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

void Scene::collectRenderData()
{
	if (!m_root || !m_activeCamera)
		return;

	// Process all RenderNodes in the scene graph for render collection
	std::function<void(entity::Node::Ptr)> processRenderNodes;
	processRenderNodes = [&processRenderNodes, this](entity::Node::Ptr node)
	{
		if (node->isEnabled())
		{
			// If this is a RenderNode, call its onRenderCollect method
			if (node->isRender())
			{
				auto renderNode = node->asRenderNode();
				if (renderNode)
				{
					renderNode->onRenderCollect(m_renderCollector);
				}
			}

			// Process children
			for (const auto &child : node->getChildren())
			{
				processRenderNodes(child);
			}
		}
	};

	// Start traversal from root
	processRenderNodes(m_root);
	
	// Sort collected items for optimal rendering
	m_renderCollector.sort();
}

void Scene::preRender()
{
	if (!m_root)
		return;

	// First, ensure the active camera is prepared
	if (m_activeCamera)
	{
		m_activeCamera->preRender();
	}

	// Process all RenderNodes in the scene graph for preRender
	std::function<void(entity::Node::Ptr)> processPreRenderNodes;
	processPreRenderNodes = [&processPreRenderNodes](entity::Node::Ptr node)
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
	std::function<void(entity::Node::Ptr)> processPostRenderNodes;
	processPostRenderNodes = [&processPostRenderNodes](entity::Node::Ptr node)
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
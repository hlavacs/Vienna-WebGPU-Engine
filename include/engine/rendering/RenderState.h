#pragma once
#include <vector>
#include <memory>

namespace engine::rendering
{

	class Model;
	class Camera;

	struct RenderState
	{
		std::vector<std::shared_ptr<Model>> models;
		std::shared_ptr<Camera> camera;
		// Add more as needed (lights, environment, etc.)
	};

} // namespace engine::rendering

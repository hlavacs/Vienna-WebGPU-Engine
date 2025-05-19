#include "GeometryLoader.h"

namespace engine::io
{

	GeometryLoader::GeometryLoader(std::filesystem::path basePath, std::shared_ptr<spdlog::logger> logger)
		: engine::debug::Loggable(std::move(logger)),
		  m_basePath(std::move(basePath)) {}


		  
	// Compute the TBN local to a triangle face from its corners and return it as
	// a matrix whose columns are the T, B and N vectors.
	glm::mat3x3 GeometryLoader::computeTBN(const engine::rendering::Vertex corners[3], const glm::vec3 &expectedN)
	{
		// What we call e in the figure
		glm::vec3 ePos1 = corners[1].position - corners[0].position;
		glm::vec3 ePos2 = corners[2].position - corners[0].position;

		// What we call \bar e in the figure
		glm::vec2 eUV1 = corners[1].uv - corners[0].uv;
		glm::vec2 eUV2 = corners[2].uv - corners[0].uv;

		glm::vec3 T = glm::normalize(ePos1 * eUV2.y - ePos2 * eUV1.y);
		glm::vec3 B = glm::normalize(ePos2 * eUV1.x - ePos1 * eUV2.x);
		glm::vec3 N = cross(T, B);

		// Fix overall orientation
		if (dot(N, expectedN) < 0.0)
		{
			T = -T;
			B = -B;
			N = -N;
		}

		// Ortho-glm::normalize the (T, B, expectedN) frame
		// a. "Remove" the part of T that is along expected N
		N = expectedN;
		T = glm::normalize(T - dot(T, N) * N);
		// b. Recompute B from N and T
		B = glm::cross(N, T);

		return glm::mat3x3(T, B, N);
	}

} // namespace engine::io

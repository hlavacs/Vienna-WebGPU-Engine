#pragma once
#include <glm/glm.hpp>
#include <unordered_map>

namespace engine::math
{
	// CoordinateSystem class to handle transformations between different Cartesian coordinate systems
	class CoordinateSystem
	{
	public:
		// Common Cartesian coordinate system definitions
		// These enums represent various coordinate systems used in 3D graphics.
		enum class Cartesian
		{
			// +Y up, +Z forward (left-handed): Unity, Direct3D, WebGPU (GLM w/ LH_ZO)
			LH_Y_UP_Z_FORWARD,
			// +Y up, +Z forward (right-handed): OpenGL, Vulkan default, GLM
			RH_Y_UP_Z_FORWARD,
			// +Y up, -Z forward (right-handed): Maya, COLLADA, glTF, Film/Animation
			RH_Y_UP_NEGATIVE_Z_FORWARD,
			// +Z up, +X forward (left-handed): Unreal Engine
			LH_Z_UP_X_FORWARD,
			// +Z up, +Y forward (right-handed): 3ds Max (default), Blender (obj export format)
			RH_Z_UP_Y_FORWARD
		};

		static constexpr Cartesian DEFAULT = Cartesian::LH_Y_UP_Z_FORWARD;
		static constexpr Cartesian UNITY = Cartesian::LH_Y_UP_Z_FORWARD;
		static constexpr Cartesian DIRECT3D = Cartesian::LH_Y_UP_Z_FORWARD;
		static constexpr Cartesian WEBGPU = Cartesian::LH_Y_UP_Z_FORWARD;
		static constexpr Cartesian OPENGL = Cartesian::RH_Y_UP_Z_FORWARD;
		static constexpr Cartesian VULKAN = Cartesian::RH_Y_UP_Z_FORWARD;
		static constexpr Cartesian MAYA = Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;
		static constexpr Cartesian GLTF = Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;
		static constexpr Cartesian UNREAL = Cartesian::LH_Z_UP_X_FORWARD;
		static constexpr Cartesian BLENDER = Cartesian::RH_Z_UP_Y_FORWARD;
		static constexpr Cartesian MAX3DS = Cartesian::RH_Z_UP_Y_FORWARD;

		// Transform a vector between coordinate systems
		static glm::vec3 transform(const glm::vec3 &v, Cartesian src, Cartesian dst);

	private:
		static glm::mat3 basis(Cartesian cs) {
			switch (cs) {
				case Cartesian::LH_Y_UP_Z_FORWARD:      // Unity
					return glm::mat3( glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) );
				case Cartesian::RH_Y_UP_Z_FORWARD:      // OpenGL
					return glm::mat3( glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,-1) );
				case Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD: // Maya
					return glm::mat3( glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,-1) );
				case Cartesian::LH_Z_UP_X_FORWARD:      // Unreal
					return glm::mat3( glm::vec3(0,0,1), glm::vec3(1,0,0), glm::vec3(0,1,0) );
				case Cartesian::RH_Z_UP_Y_FORWARD:      // 3ds Max
					return glm::mat3( glm::vec3(0,1,0), glm::vec3(0,0,1), glm::vec3(1,0,0) );
			}
			return glm::mat3(1.0f);
		}
	};

} // namespace engine::math

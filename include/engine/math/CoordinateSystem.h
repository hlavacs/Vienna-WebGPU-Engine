#pragma once
#include <glm/glm.hpp>
#include <unordered_map>

namespace engine::math
{
class CoordinateSystem
{
  public:
	// Common Cartesian coordinate system definitions
	enum class Cartesian
	{
		// Left-handed systems
		LH_Y_UP_Z_FORWARD, // Unity, Direct3D (LH, +Y up, +Z forward)
		LH_Z_UP_X_FORWARD, // Unreal Engine (LH, +Z up, +X forward)

		// Right-handed systems
		RH_Y_UP_NEGATIVE_Z_FORWARD, // WebGPU, Vulkan, OpenGL, Maya, glTF (RH, +Y up, -Z forward)
		RH_Z_UP_NEGATIVE_Y_FORWARD,	// Blender, 3ds Max (RH, +Z up, -Y forward)
	};

	enum class Handedness
	{
		LEFT_HANDED,
		RIGHT_HANDED
	};

	struct BasisInfo
	{
		glm::mat3 axes;	  // Column vectors of the basis
		int forwardIndex; // 0=X, 1=Y, 2=Z
		Handedness handedness;
	};

	/*
	* @brief Default coordinate system used by the engine
	*/
	static constexpr Cartesian DEFAULT = Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;

	static constexpr Cartesian UNITY = Cartesian::LH_Y_UP_Z_FORWARD;
	static constexpr Cartesian DIRECT3D = Cartesian::LH_Y_UP_Z_FORWARD;

	static constexpr Cartesian WEBGPU = Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;
	static constexpr Cartesian VULKAN = Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;
	static constexpr Cartesian OPENGL = Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;

	static constexpr Cartesian MAYA = Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;
	static constexpr Cartesian GLTF = Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD;

	static constexpr Cartesian UNREAL = Cartesian::LH_Z_UP_X_FORWARD;
	static constexpr Cartesian BLENDER = Cartesian::RH_Z_UP_NEGATIVE_Y_FORWARD;
	static constexpr Cartesian MAX3DS = Cartesian::RH_Z_UP_NEGATIVE_Y_FORWARD;

	// Transform a vector between coordinate systems (to be implemented)
	static glm::vec3 transform(const glm::vec3 &v, Cartesian src, Cartesian dst);

  private:
	static BasisInfo basisInfo(Cartesian cs);

	static bool isLeftHanded(Cartesian cs);
};
} // namespace engine::math

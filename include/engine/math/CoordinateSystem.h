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
		RH_Z_UP_NEGATIVE_Y_FORWARD, // Blender, 3ds Max (RH, +Z up, -Y forward)
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

	/**
	 * @brief Transform a vector from source coordinate system to destination coordinate system.
	 * @param v The vector to transform.
	 * @param src The source coordinate system of the vector.
	 * @param dst The destination coordinate system to transform to.
	 * @return The transformed vector in the destination coordinate system.
	 */
	static glm::vec3 transform(const glm::vec3 &v, Cartesian src, Cartesian dst);

	/**
	 * @brief Transform a vector with an optional w component (e.g., for tangents) from source coordinate system to destination coordinate system.
	 * The w component is used to preserve handedness for tangents. It will be flipped if the source and destination coordinate systems have different handedness.
	 * @param v The vector to transform (x,y,z) and its w component for handedness.
	 * @param src The source coordinate system of the vector.
	 * @param dst The destination coordinate system to transform to.
	 * @return The transformed vector in the destination coordinate system, with w component adjusted for handedness if necessary.
	 */
	static glm::vec4 transform(const glm::vec4 &v, Cartesian src, Cartesian dst);

  private:
	static BasisInfo basisInfo(Cartesian cs);

	static bool isLeftHanded(Cartesian cs);
};
} // namespace engine::math

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <memory>

namespace engine::scene
{
namespace nodes
{
class SpatialNode; // Forward declaration for friend access
}

/**
 * @brief Represents a position, rotation, and scale in 3D space.
 * Transform is hierarchy-agnostic - it only knows its parent, not its children.
 * The Node hierarchy is the single source of truth for the scene graph.
 */
class Transform : public std::enable_shared_from_this<Transform>
{
	// Only SpatialNode can modify parent relationships
	friend class nodes::SpatialNode;

  public:
	using Ptr = std::shared_ptr<Transform>;

	/**
	 * @brief Constructs a new Transform with default position (0), rotation (identity), and scale (1).
	 */
	Transform();

	/**
	 * @brief Destructor.
	 */
	~Transform();

	// --- Local Transform ---

	/**
	 * @brief Sets the local position.
	 * @param position Local space position.
	 */
	void setLocalPosition(const glm::vec3 &position);

	/**
	 * @brief Sets the local rotation as a quaternion.
	 * @param rotation Local space rotation.
	 */
	void setLocalRotation(const glm::quat &rotation);

	/**
	 * @brief Sets the local rotation from Euler angles in degrees.
	 * @param euler Euler angles (degrees).
	 */
	void setLocalEulerAngles(const glm::vec3 &euler);

	/**
	 * @brief Sets the local scale.
	 * @param scale Local scale vector.
	 */
	void setLocalScale(const glm::vec3 &scale);

	/**
	 * @brief Gets the local position.
	 * @return The local position.
	 */
	const glm::vec3 &getLocalPosition() const;

	/**
	 * @brief Gets the local rotation as a quaternion.
	 * @return The local rotation.
	 */
	const glm::quat &getLocalRotation() const;

	/**
	 * @brief Gets the local Euler angles in degrees.
	 * @return Euler angles in degrees.
	 */
	glm::vec3 getLocalEulerAngles() const;

	/**
	 * @brief Gets the local scale.
	 * @return Local scale vector.
	 */
	const glm::vec3 &getLocalScale() const;

	// --- World Transform ---

	/**
	 * @brief Gets the world position.
	 * @return World space position.
	 */
	glm::vec3 getPosition() const;

	/**
	 * @brief Gets the world rotation as a quaternion.
	 * @return World space rotation.
	 */
	glm::quat getRotation() const;

	/**
	 * @brief Gets the world scale.
	 * @return World scale vector.
	 */
	glm::vec3 getScale() const;

	/**
	 * @brief Gets the world Euler angles in degrees.
	 * @return Euler angles (degrees).
	 */
	glm::vec3 getEulerAngles() const;

	/**
	 * @brief Sets the world position, adjusting local transform based on parent.
	 * @param position World space position.
	 */
	void setWorldPosition(const glm::vec3 &position);

	/**
	 * @brief Sets the world rotation, adjusting local transform based on parent.
	 * @param rotation World space rotation quaternion.
	 */
	void setWorldRotation(const glm::quat &rotation);

	/**
	 * @brief Sets the world scale, adjusting local transform based on parent.
	 * @param scale World space scale.
	 */
	void setWorldScale(const glm::vec3 &scale);

	/**
	 * @brief Gets the local transformation matrix.
	 * @return Local model matrix.
	 */
	glm::mat4 getLocalMatrix() const;

	/**
	 * @brief Gets the world transformation matrix.
	 * @return World model matrix.
	 */
	glm::mat4 getWorldMatrix() const;

	// --- Direction Vectors ---

	/**
	 * @brief Gets the forward direction in world space.
	 * @return Forward vector (-Z).
	 */
	glm::vec3 forward() const;

	/**
	 * @brief Gets the right direction in world space.
	 * @return Right vector (+X).
	 */
	glm::vec3 right() const;

	/**
	 * @brief Gets the up direction in world space.
	 * @return Up vector (+Y).
	 */
	glm::vec3 up() const;

	// --- Operations ---

	/**
	 * @brief Translates the transform by the given delta.
	 * @param delta Translation vector.
	 * @param local Whether to apply in local space (true) or world space (false).
	 */
	void translate(const glm::vec3 &delta, bool local = true);

	/**
	 * @brief Rotates the transform by the given Euler angles (in degrees).
	 * @param eulerDegrees Rotation delta (degrees).
	 * @param local Whether to apply in local space (true) or world space (false).
	 */
	void rotate(const glm::vec3 &eulerDegrees, bool local = true);

	/**
	 * @brief Rotates the transform to face a target position in world space.
	 * @param target The point to look at (world space).
	 * @param up The up direction (default is world up).
	 */
	void lookAt(const glm::vec3 &target, const glm::vec3 &up = glm::vec3(0, 1, 0));

	// --- Parenting (Read-Only) ---

	/**
	 * @brief Gets the parent transform.
	 * @return Parent transform or nullptr if root.
	 * @note Parent can only be set by SpatialNode during hierarchy updates.
	 */
	Ptr getParent() const;

  private:
	// Local transform data
	glm::vec3 _localPosition = glm::vec3(0.0f);
	glm::quat _localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 _localScale = glm::vec3(1.0f);

	// Cached world matrix
	mutable glm::mat4 _worldMatrixCache = glm::mat4(1.0f);
	mutable bool _dirty = true;

	// Hierarchy (parent only - children are managed by Node hierarchy)
	Ptr _parent = nullptr;

	/**
	 * @brief Marks this transform as needing matrix recomputation.
	 */
	void markDirty();

	/**
	 * @brief Updates the world matrix cache if dirty.
	 */
	void updateWorldMatrix() const;

	/**
	 * @brief Internal method to set parent transform.
	 * Only accessible by SpatialNode via friend access.
	 * @param parent The new parent transform.
	 * @param keepWorld If true, keeps the current world-space transform.
	 */
	void setParentInternal(Ptr parent, bool keepWorld = true);
};

} // namespace engine::scene

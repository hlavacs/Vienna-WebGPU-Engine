#include "engine/math/CoordinateSystem.h"

namespace engine::math
{

glm::vec3 CoordinateSystem::transform(const glm::vec3 &v, Cartesian src, Cartesian dst)
{
	if (src == dst)
		return v;

	auto srcInfo = basisInfo(src);
	auto dstInfo = basisInfo(dst);

	// Map vector to dst axes
	glm::vec3 result = glm::transpose(dstInfo.axes) * (srcInfo.axes * v);

	// Flip X axis if handedness differs (LH → RH)
	if (srcInfo.handedness != dstInfo.handedness)
	{
		switch (srcInfo.forwardIndex)
		{
		case 0:
			result.x = -result.x;
			break;
		case 1:
			result.y = -result.y;
			break;
		case 2:
			result.z = -result.z;
			break;
		}
	}

	return result;
}

CoordinateSystem::BasisInfo CoordinateSystem::basisInfo(Cartesian cs)
{
	BasisInfo info{};

	switch (cs)
	{
	case Cartesian::LH_Y_UP_Z_FORWARD:
		info.axes = glm::mat3(
			glm::vec3(1.0f, 0.0f, 0.0f), // X axis
			glm::vec3(0.0f, 1.0f, 0.0f), // Y axis
			glm::vec3(0.0f, 0.0f, 1.0f)	 // Z axis
		);
		info.forwardIndex = 2;
		info.handedness = Handedness::LEFT_HANDED;
		break;
	case Cartesian::LH_Z_UP_X_FORWARD:
		info.axes = glm::mat3(
			glm::vec3(0.0f, 0.0f, 1.0f), // X axis
			glm::vec3(0.0f, 1.0f, 0.0f), // Y axis
			glm::vec3(1.0f, 0.0f, 0.0f)	 // Z axis
		);
		info.forwardIndex = 0;
		info.handedness = Handedness::LEFT_HANDED;
		break;
	case Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD:
		info.axes = glm::mat3(
			glm::vec3(1.0f, 0.0f, 0.0f), // X axis
			glm::vec3(0.0f, 1.0f, 0.0f), // Y axis
			glm::vec3(0.0f, 0.0f, -1.0f) // Z axis
		);
		info.forwardIndex = 2;
		info.handedness = Handedness::RIGHT_HANDED;
		break;
	case Cartesian::RH_Z_UP_NEGATIVE_Y_FORWARD:
		info.axes = glm::mat3(
			glm::vec3(1.0f, 0.0f, 0.0f), // X axis
			glm::vec3(0.0f, 0.0f, 1.0f), // Z axis
			glm::vec3(0.0f, -1.0f, 0.0f) // Y axis
		);
		info.forwardIndex = 1;
		info.handedness = Handedness::RIGHT_HANDED;
		break;
	}
	return info;
}

bool CoordinateSystem::isLeftHanded(Cartesian cs)
{
	switch (cs)
	{
	case Cartesian::LH_Y_UP_Z_FORWARD:
	case Cartesian::LH_Z_UP_X_FORWARD:
		return true;
	case Cartesian::RH_Y_UP_NEGATIVE_Z_FORWARD:
	case Cartesian::RH_Z_UP_NEGATIVE_Y_FORWARD:
		return false;
	default:
		return false;
	}
}

} // namespace engine::math

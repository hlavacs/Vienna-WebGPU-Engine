#include "engine/math/CoordinateSystem.h"

namespace engine::math
{

	glm::vec3 CoordinateSystem::transform(const glm::vec3 &v, Cartesian src, Cartesian dst)
	{
		if (src == dst)
			return v;

		const glm::mat3 &Msrc = basis(src);
		const glm::mat3 &Mdst = basis(dst);

		// Umrechnung: dst^-1 * src * v
		glm::mat3 MdstInv = glm::inverse(Mdst);

		return MdstInv * (Msrc * v);
	}

} // namespace engine::math

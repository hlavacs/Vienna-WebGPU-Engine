#include "GeometryLoader.h"

namespace engine::io
{
	class GltfLoader : public GeometryLoader
	{
	public:
		[[nodiscard]]
		std::optional<engine::rendering::Mesh> load(const std::filesystem::path &file, bool indexed = true) override;

	protected:
	};

}
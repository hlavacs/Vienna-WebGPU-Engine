
#include "engine/resources/GeometryLoader.h"
#include "engine/io/tiny_obj_loader.h"

namespace engine::resources
{
	class GltfLoader : public GeometryLoader
	{
	public:
		[[nodiscard]]
		std::optional<engine::rendering::Mesh> load(const std::filesystem::path &file, bool indexed = true) override;

	protected:
	};

}
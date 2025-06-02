#pragma once

#include <memory>
#include <string>
#include <optional>
#include "engine/core/Identifiable.h"
#include "engine/core/Handle.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Material.h"

namespace engine::rendering
{
	using MaterialHandle = engine::core::Handle<Material>;

	struct Model : public engine::core::Identifiable<Model>
	{
	public:
		using Handle = engine::core::Handle<Model>;
		using Ptr = std::shared_ptr<Model>;

		// Constructors
		Model() = default;

		Model(Mesh mesh, MaterialHandle material, const std::string &filePath)
			: engine::core::Identifiable<Model>(),
			  m_mesh(std::move(mesh)),
			  m_material(material),
			  m_filePath(filePath)
		{
		}

		Model(Mesh mesh, MaterialHandle material, const std::string &filePath, const std::string &name)
			: engine::core::Identifiable<Model>(std::move(name)),
			  m_mesh(std::move(mesh)),
			  m_material(material),
			  m_filePath(filePath)
		{
		}

		// Move only
		Model(Model &&) noexcept = default;
		Model &operator=(Model &&) noexcept = default;

		// No copy
		Model(const Model &) = delete;
		Model &operator=(const Model &) = delete;

		// Accessors
		const Mesh &getMesh() const { return m_mesh; }
		Mesh &getMesh() { return m_mesh; }

		MaterialHandle getMaterial() const { return m_material; }
		void setMaterial(MaterialHandle handle) { m_material = handle; }

		const std::string &getFilePath() const { return m_filePath; }
		void setFilePath(const std::string &path) { m_filePath = path; }

		bool hasMaterial() const { return m_material.valid(); }

	private:
		Mesh m_mesh;
		MaterialHandle m_material;
		std::string m_filePath;
	};

} // namespace engine::rendering
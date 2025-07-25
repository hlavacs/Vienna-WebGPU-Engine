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
	using MaterialHandle = Material::Handle;
	using MeshHandle = Mesh::Handle;

	struct Model : public engine::core::Identifiable<Model>
	{
	public:
		using Handle = engine::core::Handle<Model>;
		using Ptr = std::shared_ptr<Model>;

		// Constructors
		Model() = default;

		Model(MeshHandle mesh, MaterialHandle material, const std::string &filePath)
			: engine::core::Identifiable<Model>(),
			  m_mesh(mesh),
			  m_material(material),
			  m_filePath(filePath)
		{
		}

		Model(MeshHandle mesh, MaterialHandle material, const std::string &filePath, const std::string &name)
			: engine::core::Identifiable<Model>(std::move(name)),
			  m_mesh(mesh),
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
		MeshHandle getMesh() const { return m_mesh; }
		void setMesh(MeshHandle handle) { m_mesh = handle; }

		MaterialHandle getMaterial() const { return m_material; }
		void setMaterial(MaterialHandle handle) { m_material = handle; }

		const std::string &getFilePath() const { return m_filePath; }
		void setFilePath(const std::string &path) { m_filePath = path; }

		bool hasMaterial() const { return m_material.valid(); }
		bool hasMesh() const { return m_mesh.valid(); }

	private:
		MeshHandle m_mesh;
		MaterialHandle m_material;
		std::string m_filePath;
	};

} // namespace engine::rendering
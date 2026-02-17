#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Submesh.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace engine::rendering
{

using MaterialHandle = Material::Handle;
using MeshHandle = Mesh::Handle;

struct Model : public engine::core::Identifiable<Model>, public engine::core::Versioned
{
  public:
	using Handle = engine::core::Handle<Model>;
	using Ptr = std::shared_ptr<Model>;

	Model() = default;
	Model(MeshHandle mesh, std::string filePath, const std::string &name = "") : engine::core::Identifiable<Model>(name), m_mesh(mesh), m_filePath(std::move(filePath))
	{
	}

	// Move only
	Model(Model &&) = default;
	Model &operator=(Model &&) = delete;

	Model(const Model &) = delete;
	Model &operator=(const Model &) = delete;

	/**
	 * @brief Get the mesh handle of the model.
	 * @return The mesh handle.
	 */
	MeshHandle getMesh() const { return m_mesh; }

	/**
	 * @brief Check if the model has a valid mesh.
	 * @return True if the mesh handle is valid, false otherwise.
	 */
	bool hasMesh() const { return m_mesh.valid(); }

	/**
	 * @brief Add a submesh to the model.
	 * @param submesh The submesh to add.
	 */
	void addSubmesh(Submesh submesh)
	{
		m_submeshes.push_back(std::move(submesh));
		incrementVersion();
	}

	/**
	 * @brief Get the submeshes of the model.
	 * @return Vector of submeshes.
	 */
	const std::vector<Submesh> &getSubmeshes() const { return m_submeshes; }

	/**
	 * @brief Get the submeshes of the model.
	 * @return Vector of submeshes.
	 */
	std::vector<Submesh> &getSubmeshes() { return m_submeshes; }

	/**
	 * @brief Get the number of submeshes in the model.
	 * @return Number of submeshes.
	 */
	size_t getSubmeshCount() const { return m_submeshes.size(); }

	/**
	 * @brief Get the file path of the model.
	 * @return The file path as a string.
	 */
	const std::string &getFilePath() const { return m_filePath; }

  private:
	MeshHandle m_mesh;
	std::string m_filePath;
	std::vector<Submesh> m_submeshes;
};

} // namespace engine::rendering

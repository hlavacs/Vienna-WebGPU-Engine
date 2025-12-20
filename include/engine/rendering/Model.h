#pragma once

#include "engine/core/Handle.h"
#include "engine/core/Identifiable.h"
#include "engine/core/Versioned.h"
#include "engine/rendering/Mesh.h"
#include "engine/rendering/Submesh.h"
#include "engine/rendering/Material.h"
#include <memory>
#include <string>
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
    Model(MeshHandle mesh, const std::string& filePath, const std::string& name = "")
        : engine::core::Identifiable<Model>(name), m_mesh(mesh), m_filePath(filePath)
    {}

    // Move only
    Model(Model&&) noexcept = default;
    Model& operator=(Model&&) noexcept = default;

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    MeshHandle getMesh() const { return m_mesh; }
    bool hasMesh() const { return m_mesh.valid(); }

    // ToDo: Doku
    void addSubmesh(Submesh submesh)
    {
        m_submeshes.push_back(std::move(submesh));
        incrementVersion();
    }

    const std::vector<Submesh> &getSubmeshes() const { return m_submeshes; }
    std::vector<Submesh> &getSubmeshes() { return m_submeshes; }

    const std::string& getFilePath() const { return m_filePath; }

private:
    MeshHandle m_mesh;
    std::string m_filePath;
    std::vector<Submesh> m_submeshes;
};

} // namespace engine::rendering

#pragma once
#include <webgpu/webgpu.hpp>
#include <memory>

#include "engine/rendering/Material.h"
#include "engine/rendering/webgpu/WebGPURenderObject.h"

namespace engine::rendering::webgpu
{
    class WebGPUTexture;

    /**
     * @brief Options for configuring a WebGPUMaterial.
     */
    struct WebGPUMaterialOptions
    {
    };

    /**
     * @class WebGPUMaterial
     * @brief GPU-side material: wraps bind groups and layout setup, maintains a handle to the CPU-side Material.
     */
    class WebGPUMaterial : public WebGPURenderObject<engine::rendering::Material>
    {
    public:
        /**
         * @brief Construct a WebGPUMaterial from a Material handle and bind group.
         * @param context The WebGPU context.
         * @param materialHandle Handle to the CPU-side Material.
         * @param bindGroup The GPU-side bind group for this material.
         * @param options Optional material options.
         */
        WebGPUMaterial(
            WebGPUContext &context,
            const engine::rendering::Material::Handle &materialHandle,
            wgpu::BindGroup bindGroup,
            WebGPUMaterialOptions options = {});

        /**
         * @brief Set up material state for rendering.
         * @param encoder The command encoder for recording commands.
         * @param renderPass The render pass for drawing.
         */
        void render(wgpu::CommandEncoder &encoder, wgpu::RenderPassEncoder &renderPass) override;

        /**
         * @brief Get the GPU-side bind group for this material.
         * @return The WebGPU bind group.
         */
        wgpu::BindGroup getBindGroup() const { return m_bindGroup; }

        /**
         * @brief Get the material options used for this WebGPUMaterial.
         * @return Reference to the options struct.
         */
        const WebGPUMaterialOptions &getOptions() const { return m_options; }

    protected:
        /**
         * @brief Update GPU resources from CPU data.
         * Implementation of WebGPURenderObject::updateGPUResources().
         */
        void updateGPUResources() override;

    private:
        /**
         * @brief The GPU-side bind group for this material.
         */
        wgpu::BindGroup m_bindGroup;

        /**
         * @brief Options used for configuring this WebGPUMaterial.
         */
        WebGPUMaterialOptions m_options;
    };

} // namespace engine::rendering::webgpu

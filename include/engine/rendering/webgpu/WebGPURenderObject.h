#pragma once
/**
 * @file WebGPURenderObject.h
 * @brief Base class for all GPU-side render objects in the WebGPU backend.
 */
#include "engine/rendering/webgpu/WebGPUContext.h"
#include <string>
#include <chrono>
#include "engine/core/Identifiable.h"

namespace engine::rendering::webgpu
{

	/**
	 * @class WebGPURenderObject
	 * @brief Base class for all GPU-side render objects (camera, mesh, material, etc.) in the WebGPU backend.
	 *        Provides context, name, type, dirty flag, and timestamps.
	 */
	class WebGPURenderObject : public engine::core::Identifiable<WebGPURenderObject>
	{
	public:
		/**
		 * @brief Type of the render object for RTTI and debugging.
		 */
		enum class Type
		{
			Unknown,  ///< Unknown type
			Camera,	  ///< Camera object
			Mesh,	  ///< Mesh object
			Material, ///< Material object
			Model,	  ///< Model object
			Texture	  ///< Texture object
		};

		/**
		 * @brief Construct a WebGPURenderObject.
		 * @param context The WebGPU context.
		 * @param type The type of the render object.
		 * @param name Optional name for debugging.
		 */
		explicit WebGPURenderObject(WebGPUContext &context, Type type = Type::Unknown, std::optional<std::string> name = std::nullopt)
			: Identifiable(std::move(name)), m_context(context), m_dirty(true), m_type(type),
			  m_creationTime(std::chrono::steady_clock::now()), m_lastUpdateTime(m_creationTime) {}
		virtual ~WebGPURenderObject() = default;

		/**
		 * @brief Update the GPU-side resources if needed. Override in derived classes. Call base class implementation in order to update timestamps.
		 * @note This method is called automatically by the rendering system when the object is dirty.
		 */
		virtual void update()
		{
			m_lastUpdateTime = std::chrono::steady_clock::now();
		}

		/** @brief Get the WebGPU context. */
		WebGPUContext &getContext() { return m_context; }
		
		/** @brief Check if the object is dirty (needs update). */
		bool isDirty() const { return m_dirty; }

		/** @brief Set the dirty flag. */
		void setDirty(bool dirty) { m_dirty = dirty; }

		/** @brief Get the type of the object. */
		Type getType() const { return m_type; }

		/** @brief Set the type of the object. */
		void setType(Type type) { m_type = type; }

		/** @brief Get the creation time of the object. */
		std::chrono::steady_clock::time_point getCreationTime() const { return m_creationTime; }

		/** @brief Get the last update time of the object. */
		std::chrono::steady_clock::time_point getLastUpdateTime() const { return m_lastUpdateTime; }

	protected:
		WebGPUContext &m_context;
		bool m_dirty;
		Type m_type;
		std::chrono::steady_clock::time_point m_creationTime;
		std::chrono::steady_clock::time_point m_lastUpdateTime;
	};

} // namespace engine::rendering::webgpu

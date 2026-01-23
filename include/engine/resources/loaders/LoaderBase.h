#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "engine/debug/Loggable.h"

namespace engine::resources::loaders
{

/**
 * @brief Generic base class for resource loaders.
 * Provides common functionality such as base path management and logging.
 * @param T The type of resource being loaded.
 * @note Derived classes must implement the load() method to handle specific resource loading logic.
 */
template <typename T>
class LoaderBase : public engine::debug::Loggable
{
  protected:
	using Loaded = T;

	explicit LoaderBase(std::filesystem::path basePath = {}) :
		m_basePath(std::filesystem::absolute(std::move(basePath)))
	{
	}
	~LoaderBase() override = default;

  public:
	/**
	 * @brief Loads a resource from a file.
	 *
	 * @param file Relative or absolute path to the resource file.
	 * @return Optional T to the loaded resource, std::nullopt on failure.
	 */
	[[nodiscard]]
	virtual std::optional<Loaded> load(const std::filesystem::path &file) = 0;
	/**
	 * @brief Gets the base path used for loading images.
	 * @return The base filesystem path.
	 */
	const std::filesystem::path &getBasePath() const { return m_basePath; }

	/**
	 * @brief Sets the base path used for loading images.
	 * @param basePath The new base filesystem path.
	 */
	void setBasePath(const std::filesystem::path &basePath) { m_basePath = basePath; }

	/**
	 * @brief Resolves the full path for a given file, combining with base path if relative.
	 * @param file The relative or absolute file path.
	 * @return The resolved absolute file path.
	 */
	std::filesystem::path resolvePath(const std::filesystem::path &file) const
	{
		return file.is_absolute() ? file : std::filesystem::absolute(m_basePath / file);
	}

  protected:
	std::filesystem::path m_basePath;
};

} // namespace engine::resources::loaders

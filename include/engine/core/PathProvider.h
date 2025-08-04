#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::core
{
/**
 * @brief Provides centralized and platform-independent access to important engine file paths.
 *
 * PathProvider resolves paths relative to the running executable.
 * It supports common resource folders such as assets, textures, shaders, and models,
 * and allows appending subdirectories or filenames in a safe, OS-agnostic way using std::filesystem.
 *
 * Features:
 *  - Initialize with the executable path at startup.
 *  - Get base paths for assets, textures, shaders, models, scripts.
 *  - Safe path joining with variadic arguments.
 *  - Override any base path dynamically.
 *  - Query the engine library path for internal resource location.
 *
 * Usage:
 *  PathProvider::initialize();  // call once at app startup
 *  auto texturePath = PathProvider::getTextures("materials", "brick.png");
 */
class PathProvider
{
  public:
	/// @brief Initializes the PathProvider by setting the base path and engine lib path.
	/// @param path The base path to initialize with. If this is an empty string, the path of the executable will be used as the default base path.
	/// @param libPath The lib path to initialize with. If this is an empty string, the path of the engine will be used as the default lib path.
	static void initialize(const std::string &path = "", const std::string &libPath = "");

	/// @brief Returns the root directory next to the running executable.
	///
	/// This is typically used in Release builds, where asset folders (like "assets/") are located
	/// next to the application binary for deployment.
	///
	/// @return A reference to the filesystem path containing the executable.
	static const std::filesystem::path &getExecutableRoot();

	/// @brief Returns the path to the engine's library binary (DLL, .so, .dylib).
	///
	/// This is useful for locating engine-internal resources that are packaged alongside the library itself.
	///
	/// @return A reference to the filesystem path containing the engine's library.
	static const std::filesystem::path &getLibraryRoot();

	/// @brief Returns the root path for engine-internal resources (such as default shaders, fonts, etc.).
	///
	/// Typically located at:
	///
	///   - `<libraryRoot>/resources/` in Release builds
	///
	///   - `${DEBUG_ROOT_DIR}` (defined via CMake) in Debug builds
	///
	/// Used to store and load engine-side resources that are not part of the user’s asset pipeline.
	///
	/// @return A reference to the filesystem path for internal engine resources.
	static const std::filesystem::path &getResourceRoot();

	template <typename... Args>
	static std::filesystem::path getResource(Args &&...parts)
	{
		return join(resourceRoot, std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getAssets(Args &&...parts)
	{
		return join(resolve("assets"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getTextures(Args &&...parts)
	{
		return join(resolve("textures"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getShaders(Args &&...parts)
	{
		return join(resolve("shaders"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getModels(Args &&...parts)
	{
		return join(resolve("models"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getScenes(Args &&...parts)
	{
		return join(resolve("scenes"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getPrefabs(Args &&...parts)
	{
		return join(resolve("prefabs"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getMaterials(Args &&...parts)
	{
		return join(resolve("materials"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getConfigs(Args &&...parts)
	{
		return join(resolve("configs"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getLogs(Args &&...parts)
	{
		return join(resolve("logs"), std::forward<Args>(parts)...);
	}

	template <typename... Args>
	static std::filesystem::path getAudio(Args &&...parts)
	{
		return join(resolve("audio"), std::forward<Args>(parts)...);
	}

	/// @brief Overrides a default path for a given key used by resolve().
	///
	/// This allows you to customize or redirect the default path resolution logic for asset categories
	/// such as "textures", "shaders", "models", etc.
	///
	/// @param key  The logical name of the asset category to override (e.g., "textures", "shaders").
	/// @param path The new path to use for that category. This path will take precedence over internal defaults.
	///
	/// Example:
	/// @code
	///     PathProvider::overridePath("shaders", "C:/custom/shaders");
	/// @endcode
	static void overridePath(const std::string &key, const std::filesystem::path &path);

	/// @brief Resolves a logical asset path by key, accounting for build type and any overrides.
	///
	/// If an override exists for the given key, it returns the override. Otherwise, it returns the default path
	/// based on the base path.
	///
	/// Common keys include: "assets", "textures", "shaders", "models", "audio", "scenes", "prefabs", "materials", "configs", "logs"
	///
	/// @param key The name of the logical path group to resolve.
	/// @return    The fully resolved filesystem path.
	static std::filesystem::path resolve(const std::string &key);

  private:
	/// @brief  Normaly represents the executable path. For Debug builds it will be the resource dir instead.
	static std::filesystem::path basePath;
	/// @brief Path to the engine's library binary (e.g., `.dll`, `.so`, or `.dylib`).
	static std::filesystem::path libraryRoot;
	/// @brief Root directory for internal engine resource files.
	static std::filesystem::path resourceRoot;

	/// @brief Optional overrides for specific asset categories or paths.
	static std::unordered_map<std::string, std::filesystem::path> overrides;

	template <typename... Args>
	static std::filesystem::path join(const std::filesystem::path &base, Args &&...parts)
	{
		std::filesystem::path result = base;
		(result /= ... /= std::filesystem::path(std::forward<Args>(parts)));
		return result;
	}

	static std::filesystem::path getEnginePath();

	static std::filesystem::path getExecutablePath();
};
} // namespace engine::core
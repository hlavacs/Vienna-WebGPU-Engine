#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "engine/core/PathProvider.h"
namespace engine::core
{
	std::filesystem::path PathProvider::basePath;
	std::filesystem::path PathProvider::resourceRoot;
	std::filesystem::path PathProvider::libraryRoot;
	std::unordered_map<std::string, std::filesystem::path> PathProvider::overrides;

	void PathProvider::initialize(const std::string &path, const std::string &libPath)
	{
		basePath = path.empty() ? PathProvider::getExecutablePath() : std::filesystem::absolute(path);
		libraryRoot = libPath.empty() ? PathProvider::getEnginePath() : std::filesystem::absolute(libPath);
		resourceRoot = getLibraryRoot() / "resources";
	}

	const std::filesystem::path &PathProvider::getExecutableRoot()
	{
		return basePath;
	}

	const std::filesystem::path &PathProvider::getResourceRoot()
	{
		return resourceRoot;
	}

	const std::filesystem::path &PathProvider::getLibraryRoot()
	{
		return libraryRoot;
	}

	void PathProvider::overridePath(const std::string &key, const std::filesystem::path &path)
	{
		overrides[key] = path;
	}

	std::filesystem::path PathProvider::resolve(const std::string &key)
	{
		if (overrides.find(key) != overrides.end())
			return overrides[key];

		if (key == "assets")
			return basePath / "assets";
		if (key == "textures")
			return basePath / "assets" / "textures";
		if (key == "shaders")
			return basePath / "assets" / "shaders";
		if (key == "models")
			return basePath / "assets" / "models";
		if (key == "scenes")
			return basePath / "assets" / "scenes";
		if (key == "prefabs")
			return basePath / "assets" / "prefabs";
		if (key == "materials")
			return basePath / "assets" / "materials";
		if (key == "audio")
			return basePath / "assets" / "audio";
		if (key == "configs")
			return basePath / "configs";
		if (key == "logs")
			return basePath / "logs";

		return basePath;
	}

	std::filesystem::path PathProvider::getEnginePath()
	{
#if defined(__EMSCRIPTEN__)
		return executableRoot;
#elif defined(_WIN32)
		HMODULE hModule = nullptr;
		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						  reinterpret_cast<LPCSTR>(&getEnginePath), &hModule);
		char path[MAX_PATH];
		GetModuleFileNameA(hModule, path, MAX_PATH);
		return std::filesystem::path(path).parent_path();
#elif defined(__linux__)
		Dl_info info;
		dladdr(reinterpret_cast<void *>(&detectLibraryPath), &info);
		return std::filesystem::path(info.dli_fname).parent_path();
#elif defined(__APPLE__)
		char path[1024];
		uint32_t size = sizeof(path);
		if (_NSGetExecutablePath(path, &size) == 0)
		{
			return std::filesystem::path(path).parent_path();
		}
		return executableRoot;
#else
#error "Unsupported platform"
#endif
	};

	std::filesystem::path PathProvider::getExecutablePath()
	{

#if defined(__EMSCRIPTEN__)
#error "TODO: Emscripten exe path"
#elif defined(_WIN32)
		char buffer[MAX_PATH];
		DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(std::string(buffer, length)).parent_path();
#elif defined(__linux__)
		char buffer[1024];
		ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if (length != -1)
		{
			buffer[length] = '\0';
			return std::filesystem::path(buffer).parent_path();
		}
		return {};
#elif defined(__APPLE__)
		char buffer[1024];
		uint32_t size = sizeof(buffer);
		if (_NSGetExecutablePath(buffer, &size) == 0)
		{
			return std::filesystem::path(buffer).parent_path();
		}
		return {};
#else
#error "Unsupported platform"
#endif
	}
}
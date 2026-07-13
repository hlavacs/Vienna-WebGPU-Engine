#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#include <dlfcn.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "engine/core/PathProvider.h"
namespace engine::core
{
std::filesystem::path PathProvider::basePath;
std::filesystem::path PathProvider::resourceRoot;
std::filesystem::path PathProvider::libraryRoot;
std::filesystem::path PathProvider::assetRootOverride;
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

std::filesystem::path PathProvider::assetsBase()
{
	return assetRootOverride.empty() ? basePath / "assets" : assetRootOverride;
}

void PathProvider::setAssetRoot(const std::filesystem::path &assetsDir)
{
	assetRootOverride = assetsDir.empty() ? std::filesystem::path{} : std::filesystem::absolute(assetsDir);
}

void PathProvider::clearAssetRoot()
{
	assetRootOverride.clear();
}

std::filesystem::path PathProvider::resolve(const std::string &key)
{
	if (overrides.find(key) != overrides.end())
		return overrides[key];

	// Project assets (repointable via setAssetRoot); configs/logs stay next to the
	// executable so editor-global state (recent lists) is not per-project.
	if (key == "assets")
		return assetsBase();
	if (key == "textures")
		return assetsBase() / "textures";
	if (key == "shaders")
		return assetsBase() / "shaders";
	if (key == "models")
		return assetsBase() / "models";
	if (key == "scenes")
		return assetsBase() / "scenes";
	if (key == "prefabs")
		return assetsBase() / "prefabs";
	if (key == "materials")
		return assetsBase() / "materials";
	if (key == "audio")
		return assetsBase() / "audio";
	if (key == "configs")
		return basePath / "configs";
	if (key == "logs")
		return basePath / "logs";

	return basePath;
}

namespace
{
// Lexically test whether `p` (made absolute) lies inside `root`, returning the
// relative remainder. Pure path math; no filesystem access beyond cwd.
bool computeRelative(const std::filesystem::path &p, const std::filesystem::path &root, std::filesystem::path &relOut)
{
	// weakly_canonical normalizes separators and (on Windows) the on-disk case of
	// the existing path components, so a browsed file path and the configured root
	// compare correctly even when they differ in case or slash direction. Falls
	// back to a plain absolute path if canonicalization fails.
	std::error_code ec;
	std::filesystem::path absolutePath = std::filesystem::weakly_canonical(std::filesystem::absolute(p, ec), ec);
	if (ec || absolutePath.empty())
	{
		ec.clear();
		absolutePath = std::filesystem::absolute(p, ec);
		if (ec)
			absolutePath = p;
	}
	ec.clear();
	std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
	if (ec || canonicalRoot.empty())
		canonicalRoot = root;

	const std::filesystem::path rel = absolutePath.lexically_relative(canonicalRoot);
	if (rel.empty())
		return false;
	const std::string s = rel.generic_string();
	if (s == "." || s.rfind("..", 0) == 0)
		return false;
	relOut = rel;
	return true;
}
} // namespace

std::filesystem::path PathProvider::getAssetRoot()
{
	return resolve("assets");
}

bool PathProvider::isUnderResources(const std::filesystem::path &p)
{
	std::filesystem::path rel;
	return computeRelative(p, resourceRoot, rel);
}

bool PathProvider::isUnderAssets(const std::filesystem::path &p)
{
	std::filesystem::path rel;
	return computeRelative(p, getAssetRoot(), rel);
}

std::optional<std::string> PathProvider::toResourceRelative(const std::filesystem::path &abs)
{
	std::filesystem::path rel;
	if (computeRelative(abs, resourceRoot, rel))
		return rel.generic_string();
	return std::nullopt;
}

std::optional<std::string> PathProvider::toAssetRelative(const std::filesystem::path &abs)
{
	std::filesystem::path rel;
	if (computeRelative(abs, getAssetRoot(), rel))
		return rel.generic_string();
	return std::nullopt;
}

std::string PathProvider::toEnginePath(const std::filesystem::path &abs)
{
	if (auto asset = toAssetRelative(abs))
		return std::string(kAssetScheme) + *asset;
	if (auto resource = toResourceRelative(abs))
		return std::string(kResourceScheme) + *resource;
	return abs.generic_string();
}

std::filesystem::path PathProvider::resolveEnginePath(const std::string &token)
{
	const std::string assetScheme = kAssetScheme;
	const std::string resourceScheme = kResourceScheme;
	if (token.rfind(assetScheme, 0) == 0)
		return getAssetRoot() / token.substr(assetScheme.size());
	if (token.rfind(resourceScheme, 0) == 0)
		return resourceRoot / token.substr(resourceScheme.size());
	return std::filesystem::path(token);
}

std::filesystem::path PathProvider::getEnginePath()
{
#if defined(__EMSCRIPTEN__)
	return basePath;
#elif defined(_WIN32)
	HMODULE hModule = nullptr;
	GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&getEnginePath), &hModule);
	char path[MAX_PATH];
	GetModuleFileNameA(hModule, path, MAX_PATH);
	return std::filesystem::path(path).parent_path();
#elif defined(__linux__)
	Dl_info info;
	dladdr(reinterpret_cast<void *>(&getEnginePath), &info);
	return std::filesystem::path(info.dli_fname).parent_path();
#elif defined(__APPLE__)
	char path[1024];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size) == 0)
	{
		return std::filesystem::path(path).parent_path();
	}
	return basePath;
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
} // namespace engine::core
#include "SceneImporter.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "engine/scene/NodeTypeRegistry.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace editor
{
namespace
{
constexpr const char *kAssetScheme = "asset://";

bool isAssetToken(const std::string &s)
{
	return s.rfind(kAssetScheme, 0) == 0;
}

std::string assetSubpath(const std::string &token)
{
	return token.substr(std::char_traits<char>::length(kAssetScheme));
}

std::string makeAssetToken(const std::string &subpath)
{
	return std::string(kAssetScheme) + subpath;
}

// FNV-1a 64-bit over the file's bytes. Returns 0 if the file cannot be read, so
// two unreadable files never compare equal.
uint64_t hashFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return 0;
	uint64_t hash = 1469598103934665603ull;
	char buffer[4096];
	while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0)
	{
		const std::streamsize count = in.gcount();
		for (std::streamsize i = 0; i < count; ++i)
		{
			hash ^= static_cast<unsigned char>(buffer[i]);
			hash *= 1099511628211ull;
		}
	}
	return hash;
}

// True when both files exist and have byte-identical content.
bool sameContent(const fs::path &a, const fs::path &b)
{
	std::error_code ec;
	if (!fs::exists(a, ec) || !fs::exists(b, ec))
		return false;
	if (fs::file_size(a, ec) != fs::file_size(b, ec))
		return false;
	return hashFile(a) == hashFile(b);
}

// First free "stem_N.ext" in dir (dir/stem.ext is assumed already taken).
fs::path freeName(const fs::path &dir, const std::string &stem, const std::string &ext)
{
	std::error_code ec;
	for (int i = 1; i < 100000; ++i)
	{
		fs::path candidate = dir / (stem + "_" + std::to_string(i) + ext);
		if (!fs::exists(candidate, ec))
			return candidate;
	}
	return dir / (stem + "_dup" + ext);
}

// Nearest ancestor directory of a scene file named "assets" (empty if none).
fs::path findAssetRoot(const fs::path &scenePath)
{
	for (fs::path p = scenePath.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path())
		if (p.filename() == "assets")
			return p;
	return {};
}

// The .cpp under scriptsDir that registers node type `typeName` (via the
// REGISTER_NODE(typeName) marker, with a <typeName>.cpp fast path). Empty if none.
fs::path findScriptDefining(const fs::path &scriptsDir, const std::string &typeName)
{
	std::error_code ec;
	if (!fs::exists(scriptsDir, ec))
		return {};

	const std::string marker = "REGISTER_NODE(" + typeName + ")";
	auto registersType = [&](const fs::path &file)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in)
			return false;
		const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return content.find(marker) != std::string::npos;
	};

	const fs::path fast = scriptsDir / (typeName + ".cpp");
	if (fs::exists(fast, ec) && registersType(fast))
		return fast;
	for (fs::directory_iterator it(scriptsDir, ec), end; it != end && !ec; it.increment(ec))
	{
		std::error_code fileEc;
		if (it->is_regular_file(fileEc) && it->path().extension() == ".cpp" && registersType(it->path()))
			return it->path();
	}
	return {};
}

// Carries the source/target locations and per-import dedup caches so each asset,
// material and script is resolved exactly once.
struct Importer
{
	fs::path srcScene;
	fs::path srcAssetRoot;
	fs::path srcScriptsDir;
	fs::path targetAssetRoot;
	fs::path targetProjectDir;
	fs::path targetScriptsDir;
	engine::Project *project = nullptr;
	ImportReport report;

	std::unordered_map<std::string, std::string> fileRemap;		// asset subpath -> final subpath
	std::unordered_map<std::string, std::string> materialRemap; // src material name -> final name
	std::unordered_set<std::string> scriptsHandled;				// type names already processed

	// Bring a plain asset file (by its subpath relative to the asset root, e.g.
	// "textures/foo.jpg") into the target; returns the FINAL subpath there.
	std::string importFile(const std::string &subpath)
	{
		if (auto it = fileRemap.find(subpath); it != fileRemap.end())
			return it->second;

		std::error_code ec;
		const fs::path src = srcAssetRoot / subpath;
		if (!fs::exists(src, ec))
		{
			report.warnings.push_back("missing asset at source: " + subpath);
			fileRemap[subpath] = subpath;
			return subpath;
		}

		const fs::path dst = targetAssetRoot / subpath;
		std::string finalSub = subpath;
		if (!fs::exists(dst, ec))
		{
			fs::create_directories(dst.parent_path(), ec);
			fs::copy_file(src, dst, ec);
			report.copied.push_back(subpath);
		}
		else if (sameContent(src, dst))
		{
			report.deduplicated.push_back(subpath);
		}
		else
		{
			const fs::path renamed = freeName(dst.parent_path(), dst.stem().string(), dst.extension().string());
			fs::create_directories(renamed.parent_path(), ec);
			fs::copy_file(src, renamed, ec);
			finalSub = fs::relative(renamed, targetAssetRoot, ec).generic_string();
			report.renamed.push_back(subpath + " -> " + finalSub);
		}
		fileRemap[subpath] = finalSub;
		return finalSub;
	}

	// Bring a material (referenced by name) into the target: import its textures,
	// dedup/rename by name + content, record it in the project. Returns the FINAL name.
	std::string importMaterial(const std::string &name)
	{
		if (auto it = materialRemap.find(name); it != materialRemap.end())
			return it->second;

		std::error_code ec;
		const fs::path srcMat = srcAssetRoot / "materials" / (name + ".mat.json");
		if (!fs::exists(srcMat, ec))
		{
			report.warnings.push_back("material '" + name + "' not found at source; reference kept");
			materialRemap[name] = name;
			return name;
		}

		json material;
		{
			std::ifstream in(srcMat);
			try
			{
				in >> material;
			}
			catch (...)
			{
				report.warnings.push_back("material '" + name + "' is not valid JSON; skipped");
				materialRemap[name] = name;
				return name;
			}
		}

		// Import referenced textures and repoint their tokens at the target copies.
		if (material.contains("textures") && material["textures"].is_object())
		{
			for (auto &slot : material["textures"].items())
			{
				auto &entry = slot.value();
				if (entry.is_object() && entry.contains("path") && entry["path"].is_string())
				{
					const std::string token = entry["path"];
					if (isAssetToken(token))
						entry["path"] = makeAssetToken(importFile(assetSubpath(token)));
				}
			}
		}

		const fs::path materialsDir = targetAssetRoot / "materials";
		fs::create_directories(materialsDir, ec);

		std::string finalName = name;
		fs::path dst = materialsDir / (finalName + ".mat.json");
		bool write = true;
		if (fs::exists(dst, ec))
		{
			json existing;
			{
				std::ifstream in(dst);
				try
				{
					in >> existing;
				}
				catch (...)
				{
				}
			}
			material["name"] = finalName;
			if (existing == material)
			{
				write = false; // identical - reuse silently
			}
			else
			{
				for (int i = 1;; ++i)
				{
					const std::string candidate = name + "_" + std::to_string(i);
					fs::path candidatePath = materialsDir / (candidate + ".mat.json");
					if (!fs::exists(candidatePath, ec))
					{
						finalName = candidate;
						dst = candidatePath;
						break;
					}
				}
				report.materialsRenamed.push_back(name + " -> " + finalName);
			}
		}
		else
		{
			report.materialsImported.push_back(finalName);
		}

		material["name"] = finalName;
		if (write)
		{
			std::ofstream out(dst);
			out << material.dump(2) << "\n";
		}

		const std::string token = makeAssetToken((fs::path("materials") / (finalName + ".mat.json")).generic_string());
		if (std::find(project->materials.begin(), project->materials.end(), token) == project->materials.end())
			project->materials.push_back(token);

		materialRemap[name] = finalName;
		return finalName;
	}

	// Ensure the script defining node type `typeName` exists in the target. A
	// built-in / already-registered type needs nothing. A same-name-different-code
	// clash is reported, not overwritten.
	void importScript(const std::string &typeName)
	{
		if (!scriptsHandled.insert(typeName).second)
			return;
		if (engine::scene::NodeTypeRegistry::instance().find(typeName) != nullptr)
			return;

		const fs::path srcCpp = findScriptDefining(srcScriptsDir, typeName);
		if (srcCpp.empty())
		{
			report.warnings.push_back("type '" + typeName + "' has no script in the source project; node kept as placeholder");
			return;
		}

		std::error_code ec;
		fs::create_directories(targetScriptsDir, ec);
		const fs::path existing = findScriptDefining(targetScriptsDir, typeName);
		if (!existing.empty())
		{
			if (!sameContent(srcCpp, existing))
			{
				report.scriptClashes.push_back(typeName + " (existing " + existing.filename().generic_string() +
											   " differs from source " + srcCpp.filename().generic_string() + ")");
				return; // user must resolve; never overwrite compiled behaviour
			}
			// identical code already present - just make sure the project lists it
		}
		else
		{
			fs::copy_file(srcCpp, targetScriptsDir / srcCpp.filename(), fs::copy_options::overwrite_existing, ec);
			report.scriptsImported.push_back(srcCpp.filename().generic_string());
		}

		const std::string rel = "scripts/" + srcCpp.filename().generic_string();
		if (std::find(project->scripts.begin(), project->scripts.end(), rel) == project->scripts.end())
			project->scripts.push_back(rel);
	}

	// Rewrite a node subtree in place (asset tokens + material names) and pull in
	// the scripts its node types need. Recurses into children.
	void rewriteNode(json &node)
	{
		if (node.contains("props") && node["props"].is_object())
		{
			json &props = node["props"];
			if (props.contains("material") && props["material"].is_string())
			{
				const std::string matName = props["material"];
				if (!matName.empty())
					props["material"] = importMaterial(matName);
			}
			for (auto &kv : props.items())
			{
				if (kv.value().is_string())
				{
					const std::string value = kv.value();
					if (isAssetToken(value))
						kv.value() = makeAssetToken(importFile(assetSubpath(value)));
				}
			}
		}
		if (node.contains("type") && node["type"].is_string())
			importScript(node["type"].get<std::string>());
		if (node.contains("children") && node["children"].is_array())
			for (auto &child : node["children"])
				rewriteNode(child);
	}

	ImportReport run()
	{
		std::error_code ec;
		if (!fs::exists(srcScene, ec))
		{
			report.error = "source scene not found: " + srcScene.string();
			return report;
		}
		srcAssetRoot = findAssetRoot(srcScene);
		if (srcAssetRoot.empty())
		{
			report.error = "source scene is not inside a project 'assets/' folder";
			return report;
		}
		srcScriptsDir = srcAssetRoot.parent_path() / "scripts";
		targetAssetRoot = targetProjectDir / "assets";
		targetScriptsDir = targetProjectDir / "scripts";

		if (fs::weakly_canonical(srcAssetRoot, ec) == fs::weakly_canonical(targetAssetRoot, ec))
		{
			report.error = "source and target are the same project";
			return report;
		}

		json scene;
		{
			std::ifstream in(srcScene);
			try
			{
				in >> scene;
			}
			catch (...)
			{
				report.error = "source scene is not valid JSON";
				return report;
			}
		}
		if (!scene.contains("root"))
		{
			report.error = "source file is not a scene (it has no root node)";
			return report;
		}

		rewriteNode(scene["root"]);

		// Write the scene into the target, keeping its scenes/<Dir>/ layout and not
		// clobbering an existing scene of the same name.
		fs::path dstScene = targetAssetRoot / fs::relative(srcScene, srcAssetRoot, ec);
		if (fs::exists(dstScene, ec))
		{
			const fs::path parent = dstScene.parent_path();
			if (parent.filename() != "scenes") // scenes/<Dir>/file -> scenes/<Dir_N>/file
			{
				const std::string dir = parent.filename().string();
				for (int i = 1;; ++i)
				{
					fs::path candidate = parent.parent_path() / (dir + "_" + std::to_string(i));
					if (!fs::exists(candidate, ec))
					{
						dstScene = candidate / dstScene.filename();
						break;
					}
				}
			}
			else
			{
				dstScene = freeName(parent, dstScene.stem().string(), dstScene.extension().string());
			}
		}
		fs::create_directories(dstScene.parent_path(), ec);
		{
			std::ofstream out(dstScene);
			out << scene.dump(2) << "\n";
		}

		const std::string sceneToken = makeAssetToken(fs::relative(dstScene, targetAssetRoot, ec).generic_string());
		if (std::find(project->scenes.begin(), project->scenes.end(), sceneToken) == project->scenes.end())
			project->scenes.push_back(sceneToken);

		report.importedScenePath = dstScene;
		report.ok = true;
		return report;
	}
};
} // namespace

std::string ImportReport::summary() const
{
	if (!ok)
		return "Import failed: " + error;

	std::string text = "Imported scene: " + importedScenePath.filename().generic_string() + "\n";
	auto line = [&text](const char *label, const std::vector<std::string> &items)
	{
		if (items.empty())
			return;
		text += label;
		text += " (" + std::to_string(items.size()) + "): ";
		for (size_t i = 0; i < items.size(); ++i)
		{
			text += items[i];
			if (i + 1 < items.size())
				text += ", ";
		}
		text += "\n";
	};
	line("Copied", copied);
	line("Reused (identical)", deduplicated);
	line("Renamed (name clash)", renamed);
	line("Materials imported", materialsImported);
	line("Materials renamed", materialsRenamed);
	line("Scripts imported", scriptsImported);
	line("Script CLASHES - resolve manually", scriptClashes);
	line("Warnings", warnings);
	return text;
}

ImportReport SceneImporter::importScene(const fs::path &sourceScene, const fs::path &targetProjectDir, engine::Project &targetProject)
{
	Importer importer;
	importer.srcScene = sourceScene;
	importer.targetProjectDir = targetProjectDir;
	importer.project = &targetProject;
	ImportReport report = importer.run();
	if (report.ok)
		spdlog::info("SceneImporter: imported '{}'", report.importedScenePath.generic_string());
	else
		spdlog::error("SceneImporter: {}", report.error);
	return report;
}

} // namespace editor

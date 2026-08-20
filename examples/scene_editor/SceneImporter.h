#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "engine/core/Project.h"

namespace editor
{

/**
 * @brief Result of a cross-project scene import: what was copied, reused,
 * renamed, and any C++ script type-name clashes the user must resolve by hand.
 */
struct ImportReport
{
	bool ok = false;
	std::string error;						 ///< set when ok == false
	std::filesystem::path importedScenePath; ///< the .vscene written into the target project

	std::vector<std::string> copied;		 ///< assets brought in (new to the target)
	std::vector<std::string> deduplicated;	 ///< assets already present with identical content
	std::vector<std::string> renamed;		 ///< "name -> name_1" (same name, different content)
	std::vector<std::string> materialsImported;
	std::vector<std::string> materialsRenamed;
	std::vector<std::string> scriptsImported;
	std::vector<std::string> scriptClashes; ///< same type name, different code - user must resolve
	std::vector<std::string> warnings;		///< non-fatal (e.g. a referenced material/script missing at source)

	/// A short multi-line human summary (counts + clashes) for the report popup / log.
	[[nodiscard]] std::string summary() const;
};

/**
 * @brief Imports a scene authored in another project into the currently open one.
 *
 * Pure filesystem + JSON: it copies the scene's referenced meshes, textures,
 * materials and scripts into the target project, deduplicating anything already
 * present with identical content (by content hash), renaming on a same-name /
 * different-content clash and rewriting the scene's references accordingly, then
 * writing the rewritten scene into the target. Engine resources (`resource://`)
 * are shared and never copied. A C++ script type-name clash (same registered
 * type name but different code) cannot be resolved automatically and is reported
 * for the user to fix before building.
 *
 * No GPU work happens here; the caller loads the imported materials and opens
 * the written scene through the normal paths to display it.
 */
class SceneImporter
{
  public:
	/// @param sourceScene      a .vscene inside another project
	/// @param targetProjectDir the open project's root (contains assets/ and scripts/)
	/// @param targetProject    the open project, updated in place (materials / scripts / scenes)
	static ImportReport importScene(const std::filesystem::path &sourceScene,
									const std::filesystem::path &targetProjectDir,
									engine::Project &targetProject);
};

} // namespace editor

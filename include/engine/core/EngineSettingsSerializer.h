#pragma once

#include <nlohmann/json.hpp>

namespace engine
{
struct GameEngineOptions;
}

namespace engine::settings
{

/**
 * @brief JSON (de)serialization for the engine's configurable settings.
 *
 * Only the user-facing, portable fields of GameEngineOptions are written;
 * machine-specific bits (applied device limits, override handles) are skipped.
 * Used by the project file and the editor's settings panel so engine settings
 * can be stored and restored.
 */
nlohmann::json toJson(const GameEngineOptions &options);

/// Apply the fields present in @p json onto @p options (missing keys keep their
/// current value), so partial overrides (per-scene) merge cleanly.
void fromJson(const nlohmann::json &json, GameEngineOptions &options);

} // namespace engine::settings

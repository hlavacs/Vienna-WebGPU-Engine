#include "engine/core/EngineSettingsSerializer.h"

#include "engine/GameEngine.h" // GameEngineOptions full definition

namespace engine::settings
{

nlohmann::json toJson(const GameEngineOptions &o)
{
	nlohmann::json j;
	j["fixedDeltaTime"] = o.fixedDeltaTime;
	j["maxDeltaTime"] = o.maxDeltaTime;
	j["targetFrameRate"] = o.targetFrameRate;
	j["enableVSync"] = o.enableVSync;
	j["limitFrameRate"] = o.limitFrameRate;
	j["maxSubSteps"] = o.maxSubSteps;
	j["runPhysics"] = o.runPhysics;
	j["showFrameStats"] = o.showFrameStats;
	j["logSubsystemErrors"] = o.logSubsystemErrors;
	j["enableHotReload"] = o.enableHotReload;
	j["windowWidth"] = o.windowWidth;
	j["windowHeight"] = o.windowHeight;
	j["fullscreen"] = o.fullscreen;
	j["resizableWindow"] = o.resizableWindow;
	j["enableAudio"] = o.enableAudio;
	j["masterVolume"] = o.masterVolume;
	j["msaaSampleCount"] = o.msaaSampleCount;
	return j;
}

void fromJson(const nlohmann::json &j, GameEngineOptions &o)
{
	if (!j.is_object())
		return;
	o.fixedDeltaTime = j.value("fixedDeltaTime", o.fixedDeltaTime);
	o.maxDeltaTime = j.value("maxDeltaTime", o.maxDeltaTime);
	o.targetFrameRate = j.value("targetFrameRate", o.targetFrameRate);
	o.enableVSync = j.value("enableVSync", o.enableVSync);
	o.limitFrameRate = j.value("limitFrameRate", o.limitFrameRate);
	o.maxSubSteps = j.value("maxSubSteps", o.maxSubSteps);
	o.runPhysics = j.value("runPhysics", o.runPhysics);
	o.showFrameStats = j.value("showFrameStats", o.showFrameStats);
	o.logSubsystemErrors = j.value("logSubsystemErrors", o.logSubsystemErrors);
	o.enableHotReload = j.value("enableHotReload", o.enableHotReload);
	o.windowWidth = j.value("windowWidth", o.windowWidth);
	o.windowHeight = j.value("windowHeight", o.windowHeight);
	o.fullscreen = j.value("fullscreen", o.fullscreen);
	o.resizableWindow = j.value("resizableWindow", o.resizableWindow);
	o.enableAudio = j.value("enableAudio", o.enableAudio);
	o.masterVolume = j.value("masterVolume", o.masterVolume);
	o.msaaSampleCount = j.value("msaaSampleCount", o.msaaSampleCount);
}

} // namespace engine::settings

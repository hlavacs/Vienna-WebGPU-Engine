#pragma once

#include <string>

#include <glm/glm.hpp>

namespace engine::reflection
{
/**
 * @brief Visitor that a node exposes its editable fields to, via Node::reflect().
 *
 * A node lists each field once with `r("name", member)`; the engine then drives
 * serialization, deserialization AND the inspector UI from that single list -
 * there is no separate save/load/UI code to keep in sync. This is the closest a
 * language without built-in reflection gets to Unity's "public fields just work":
 * one line per field, a sensible default from the member's initializer.
 *
 * Concrete reflectors implement the field() overloads (write to JSON, read from
 * JSON, draw an ImGui widget). Types beyond these can be added as overloads.
 */
class Reflector
{
  public:
	virtual ~Reflector() = default;

	virtual void field(const char *name, float &value) = 0;
	virtual void field(const char *name, int &value) = 0;
	virtual void field(const char *name, bool &value) = 0;
	virtual void field(const char *name, glm::vec2 &value) = 0;
	virtual void field(const char *name, glm::vec3 &value) = 0;
	virtual void field(const char *name, glm::vec4 &value) = 0;
	virtual void field(const char *name, std::string &value) = 0;

	// Widget hints (the Unity-attribute equivalent): pick the inspector control for
	// a field. Serialization ignores the hint - these default to the plain field(),
	// so only the inspector reflector needs to override them.
	virtual void field(const char *name, float &value, float /*min*/, float /*max*/) { field(name, value); }
	virtual void field(const char *name, int &value, int /*min*/, int /*max*/) { field(name, value); }
	virtual void colorField(const char *name, glm::vec3 &value) { field(name, value); }
	virtual void colorField(const char *name, glm::vec4 &value) { field(name, value); }

	/// Terse alias so nodes can write `r("speed", speed)`; forwards to field().
	template <class T>
	void operator()(const char *name, T &value)
	{
		field(name, value);
	}

	/// A slider with an explicit range, e.g. `r.range("speed", speed, 0.0f, 100.0f)`.
	template <class T, class Bound>
	void range(const char *name, T &value, Bound min, Bound max)
	{
		field(name, value, static_cast<T>(min), static_cast<T>(max));
	}

	/// A colour picker instead of raw numbers, e.g. `r.color("tint", tint)`.
	void color(const char *name, glm::vec3 &value) { colorField(name, value); }
	void color(const char *name, glm::vec4 &value) { colorField(name, value); }
};

} // namespace engine::reflection

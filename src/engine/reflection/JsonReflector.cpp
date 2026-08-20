#include "engine/reflection/JsonReflector.h"

#include "engine/reflection/Reflector.h"
#include "engine/scene/nodes/Node.h"

namespace engine::reflection
{
namespace
{
using json = nlohmann::json;

class JsonWriteReflector : public Reflector
{
  public:
	explicit JsonWriteReflector(json &out) : m_json(out) {}

	void field(const char *name, float &value) override { m_json[name] = value; }
	void field(const char *name, int &value) override { m_json[name] = value; }
	void field(const char *name, bool &value) override { m_json[name] = value; }
	void field(const char *name, glm::vec2 &value) override { m_json[name] = json::array({value.x, value.y}); }
	void field(const char *name, glm::vec3 &value) override { m_json[name] = json::array({value.x, value.y, value.z}); }
	void field(const char *name, glm::vec4 &value) override { m_json[name] = json::array({value.x, value.y, value.z, value.w}); }
	void field(const char *name, std::string &value) override { m_json[name] = value; }

  private:
	json &m_json;
};

class JsonReadReflector : public Reflector
{
  public:
	explicit JsonReadReflector(const json &in) : m_json(in) {}

	void field(const char *name, float &value) override
	{
		if (m_json.contains(name) && m_json[name].is_number())
			value = m_json[name].get<float>();
	}
	void field(const char *name, int &value) override
	{
		if (m_json.contains(name) && m_json[name].is_number())
			value = m_json[name].get<int>();
	}
	void field(const char *name, bool &value) override
	{
		if (m_json.contains(name) && m_json[name].is_boolean())
			value = m_json[name].get<bool>();
	}
	void field(const char *name, glm::vec2 &value) override { readVec(name, &value.x, 2); }
	void field(const char *name, glm::vec3 &value) override { readVec(name, &value.x, 3); }
	void field(const char *name, glm::vec4 &value) override { readVec(name, &value.x, 4); }
	void field(const char *name, std::string &value) override
	{
		if (m_json.contains(name) && m_json[name].is_string())
			value = m_json[name].get<std::string>();
	}

  private:
	void readVec(const char *name, float *out, int count)
	{
		if (m_json.contains(name) && m_json[name].is_array() && static_cast<int>(m_json[name].size()) >= count)
			for (int i = 0; i < count; ++i)
				out[i] = m_json[name][i].get<float>();
	}

	const json &m_json;
};
} // namespace

void reflectToJson(const engine::scene::nodes::Node &node, json &out)
{
	JsonWriteReflector writer(out);
	// The write path only reads the fields; reflect() is non-const so it can also
	// serve the read/inspector paths from the same single field list.
	const_cast<engine::scene::nodes::Node &>(node).reflect(writer);
}

void reflectFromJson(engine::scene::nodes::Node &node, const json &in)
{
	JsonReadReflector reader(in);
	node.reflect(reader);
}

} // namespace engine::reflection

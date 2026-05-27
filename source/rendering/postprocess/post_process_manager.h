#ifndef RME_RENDERING_POSTPROCESS_MANAGER_H
#define RME_RENDERING_POSTPROCESS_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

class ShaderProgram;
class GLTextureResource;

struct PostProcessAuxTexture {
	std::string uniform_name; // e.g. "u_Tex1"
	std::string file_path;    // path relative to data directory, e.g. "shaders/nevasca.png"
	std::shared_ptr<GLTextureResource> texture;
};

struct PostProcessEffect {
	std::string name;
	std::string fragment_source;
	std::string vertex_source;
	std::shared_ptr<ShaderProgram> shader;
	std::vector<PostProcessAuxTexture> aux_textures;

	PostProcessEffect(std::string n, std::string frag, std::string vert = "") : name(n), fragment_source(frag), vertex_source(vert) { }
};

class PostProcessManager {
public:
	static PostProcessManager& Instance();

	void Register(const std::string& name, const std::string& fragment_source, const std::string& vertex_source = "");

	// Register an auxiliary texture for a previously-registered effect.
	// uniform_name is the GLSL sampler2D name (e.g. "u_Tex1").
	// file_path is relative to the data directory.
	void RegisterAuxTexture(const std::string& effect_name, const std::string& uniform_name, const std::string& file_path);

	void Initialize(const std::string& default_vertex_source); // Compiles all registered shaders and loads aux textures

	// Returns the shader program for the given name.
	// If not found, returns the first available shader (usually "None") or nullptr.
	ShaderProgram* GetEffect(const std::string& name);

	// Returns the full effect entry (shader + aux textures), nullptr if not found.
	const PostProcessEffect* GetEffectEntry(const std::string& name);

	// Returns a list of all registered effect names (for UI)
	std::vector<std::string> GetEffectNames() const;

private:
	PostProcessManager() = default;

	std::vector<std::shared_ptr<PostProcessEffect>> effects;

	bool initialized = false;
};

namespace ShaderNames {
	constexpr const char* NONE = "None";
	constexpr const char* SCANLINE = "Scanline";
	constexpr const char* XBRZ = "4xBRZ";
	constexpr const char* NEVASCA = "Nevasca";
	constexpr const char* CEMETERY = "Cemetery";
}

#endif

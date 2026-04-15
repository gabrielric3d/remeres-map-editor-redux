#include "rendering/postprocess/post_process_manager.h"
#include "rendering/core/shader_program.h"
#include "rendering/core/gl_resources.h"
#include "util/file_system.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <wx/image.h>
#include <wx/filename.h>
#include <vector>

PostProcessManager& PostProcessManager::Instance() {
	static PostProcessManager instance;
	return instance;
}

void PostProcessManager::Register(const std::string& name, const std::string& fragment_source, const std::string& vertex_source) {
	for (const auto& effect : effects) {
		if (effect->name == name) {
			return;
		}
	}

	effects.push_back(std::make_shared<PostProcessEffect>(name, fragment_source, vertex_source));
}

void PostProcessManager::RegisterAuxTexture(const std::string& effect_name, const std::string& uniform_name, const std::string& file_path) {
	for (const auto& effect : effects) {
		if (effect->name == effect_name) {
			effect->aux_textures.push_back({ uniform_name, file_path, nullptr });
			return;
		}
	}
	spdlog::warn("PostProcessManager: RegisterAuxTexture for unknown effect '{}'", effect_name);
}

namespace {

	std::shared_ptr<GLTextureResource> LoadPngTexture(const std::string& relative_path) {
		wxFileName fname;
		fname.Assign(FileSystem::GetDataDirectory());
		// Append relative_path components
		wxString rel = wxString::FromUTF8(relative_path.c_str());
		rel.Replace("\\", "/");
		wxArrayString parts = wxSplit(rel, '/');
		for (size_t i = 0; i + 1 < parts.GetCount(); ++i) {
			fname.AppendDir(parts[i]);
		}
		if (!parts.IsEmpty()) {
			fname.SetFullName(parts.Last());
		}

		wxImage image;
		if (!image.LoadFile(fname.GetFullPath(), wxBITMAP_TYPE_PNG)) {
			spdlog::error("PostProcessManager: failed to load PNG '{}'", fname.GetFullPath().ToStdString());
			return nullptr;
		}

		const int w = image.GetWidth();
		const int h = image.GetHeight();
		if (w <= 0 || h <= 0) {
			spdlog::error("PostProcessManager: PNG '{}' has invalid dimensions", fname.GetFullPath().ToStdString());
			return nullptr;
		}

		// wxImage stores RGB separately from alpha. Pack to RGBA8.
		const unsigned char* rgb = image.GetData();
		const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;

		std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
		for (int i = 0; i < w * h; ++i) {
			rgba[i * 4 + 0] = rgb[i * 3 + 0];
			rgba[i * 4 + 1] = rgb[i * 3 + 1];
			rgba[i * 4 + 2] = rgb[i * 3 + 2];
			rgba[i * 4 + 3] = alpha ? alpha[i] : 255;
		}

		auto tex = std::make_shared<GLTextureResource>(GL_TEXTURE_2D);
		glTextureStorage2D(tex->GetID(), 1, GL_RGBA8, w, h);
		glTextureSubImage2D(tex->GetID(), 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		glTextureParameteri(tex->GetID(), GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(tex->GetID(), GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureParameteri(tex->GetID(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(tex->GetID(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		return tex;
	}

} // namespace

void PostProcessManager::Initialize(const std::string& default_vertex_source) {
	if (initialized) {
		return;
	}

	for (auto& effect : effects) {
		effect->shader = std::make_shared<ShaderProgram>();

		std::string v_source = effect->vertex_source;
		if (v_source.empty()) {
			v_source = default_vertex_source;
		}

		if (!effect->shader->Load(v_source, effect->fragment_source)) {
			spdlog::error("PostProcessManager: Failed to load shader '{}'", effect->name);
			effect->shader = nullptr;
			continue;
		}

		// Load aux textures for this effect
		for (auto& aux : effect->aux_textures) {
			aux.texture = LoadPngTexture(aux.file_path);
			if (!aux.texture) {
				spdlog::error("PostProcessManager: effect '{}' missing aux texture '{}' ({})", effect->name, aux.uniform_name, aux.file_path);
			}
		}
	}

	// Remove effects whose shader failed to compile.
	effects.erase(std::remove_if(effects.begin(), effects.end(), [](const std::shared_ptr<PostProcessEffect>& effect) {
					  return !effect->shader || !effect->shader->IsValid();
				  }),
				  effects.end());

	initialized = true;
}

ShaderProgram* PostProcessManager::GetEffect(const std::string& name) {
	auto find_shader = [this](const std::string& effect_name) -> ShaderProgram* {
		for (const auto& effect : effects) {
			if (effect->name == effect_name) {
				return effect->shader.get();
			}
		}
		return nullptr;
	};

	ShaderProgram* shader = nullptr;

	std::string target_name = name.empty() ? ShaderNames::NONE : name;
	shader = find_shader(target_name);

	if (!shader && target_name != ShaderNames::NONE) {
		shader = find_shader(ShaderNames::NONE);
	}

	if (!shader && !effects.empty()) {
		return effects[0]->shader.get();
	}

	return shader;
}

const PostProcessEffect* PostProcessManager::GetEffectEntry(const std::string& name) {
	std::string target_name = name.empty() ? ShaderNames::NONE : name;
	for (const auto& effect : effects) {
		if (effect->name == target_name) {
			return effect.get();
		}
	}
	return nullptr;
}

std::vector<std::string> PostProcessManager::GetEffectNames() const {
	std::vector<std::string> names;
	for (const auto& effect : effects) {
		names.push_back(effect->name);
	}
	return names;
}

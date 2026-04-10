//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/utilities/light_drawer.h"
#include "rendering/utilities/light_calculator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <format>
#include <limits>
#include "map/tile.h"
#include "game/item.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/render_view.h"
#include "rendering/core/gl_scoped_state.h"
#include "rendering/core/forced_light_zone.h"
#include "util/common.h"

// GPULight struct moved to header

LightDrawer::LightDrawer() {
}

LightDrawer::~LightDrawer() {
	// Resources are RAII managed
}

void LightDrawer::InitFBO() {
	fbo = std::make_unique<GLFramebuffer>();
	fbo_texture = std::make_unique<GLTextureResource>(GL_TEXTURE_2D);

	// Initial dummy size
	ResizeFBO(32, 32);

	glNamedFramebufferTexture(fbo->GetID(), GL_COLOR_ATTACHMENT0, fbo_texture->GetID(), 0);

	GLenum status = glCheckNamedFramebufferStatus(fbo->GetID(), GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		spdlog::error("LightDrawer FBO Incomplete: {}", status);
	}
}

void LightDrawer::ResizeFBO(int width, int height) {
	if (width == buffer_width && height == buffer_height) {
		return;
	}

	buffer_width = width;
	buffer_height = height;

	glTextureStorage2D(fbo_texture->GetID(), 1, GL_RGBA8, width, height);

	// Set texture parameters
	glTextureParameteri(fbo_texture->GetID(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTextureParameteri(fbo_texture->GetID(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTextureParameteri(fbo_texture->GetID(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(fbo_texture->GetID(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void LightDrawer::draw(const RenderView& view, bool fog, const LightBuffer& light_buffer, const wxColor& global_color, float light_intensity, float ambient_light_level, bool shadow_occlusion, const LightBuffer::BlockingGrid* blocking, const std::vector<const ForcedLightZone*>& zones) {
	if (!shader) {
		initRenderResources();
	}

	if (!fbo) {
		InitFBO();
	}

	int buffer_w = view.screensize_x;
	int buffer_h = view.screensize_y;

	if (buffer_w <= 0 || buffer_h <= 0) {
		return;
	}

	// 1. Resize FBO if needed (ensure it covers the visible screen area)
	if (buffer_width < buffer_w || buffer_height < buffer_h) {
		// Re-create texture if we need to grow
		fbo_texture = std::make_unique<GLTextureResource>(GL_TEXTURE_2D);
		ResizeFBO(std::max(buffer_width, buffer_w), std::max(buffer_height, buffer_h));
		glNamedFramebufferTexture(fbo->GetID(), GL_COLOR_ATTACHMENT0, fbo_texture->GetID(), 0);
	}

	// 2. Prepare Lights
	// Filter and convert lights to GPU format
	gpu_lights_.clear();
	gpu_lights_.reserve(light_buffer.lights.size());

	for (const auto& light : light_buffer.lights) {
		int lx_px = light.map_x * TILE_SIZE + TILE_SIZE / 2;
		int ly_px = light.map_y * TILE_SIZE + TILE_SIZE / 2;

		float map_pos_x = static_cast<float>(lx_px - view.view_scroll_x);
		float map_pos_y = static_cast<float>(ly_px - view.view_scroll_y);

		// Convert to Screen Pixels
		float screen_x = map_pos_x / view.zoom;
		float screen_y = map_pos_y / view.zoom;
		float screen_radius = (light.intensity * TILE_SIZE) / view.zoom;

		// Check overlap with Screen Rect
		if (screen_x + screen_radius < 0 || screen_x - screen_radius > buffer_w || screen_y + screen_radius < 0 || screen_y - screen_radius > buffer_h) {
			continue;
		}

		wxColor c = colorFromEightBit(light.color);

		gpu_lights_.push_back({ .position = { screen_x, screen_y }, .intensity = static_cast<float>(light.intensity), .padding = 0.0f,
								// Pre-multiply intensity here if needed, or in shader
								.color = { (c.Red() / 255.0f) * light_intensity, (c.Green() / 255.0f) * light_intensity, (c.Blue() / 255.0f) * light_intensity, 1.0f } });
	}

	if (gpu_lights_.empty()) {
		// Just render ambient? We still need to clear the FBO/screen area or simpy fill it.
		// If no lights, the overlay should just be ambient color.
	} else {
		// Upload Lights
		size_t needed_size = gpu_lights_.size() * sizeof(GPULight);
		if (needed_size > light_ssbo_capacity_) {
			light_ssbo_capacity_ = std::max(needed_size, static_cast<size_t>(light_ssbo_capacity_ * 1.5));
			if (light_ssbo_capacity_ < 1024) {
				light_ssbo_capacity_ = 1024;
			}
			// Destroy and recreate buffer for Immutable Storage
			light_ssbo = std::make_unique<GLBuffer>();
			glNamedBufferStorage(light_ssbo->GetID(), light_ssbo_capacity_, nullptr, GL_DYNAMIC_STORAGE_BIT);
		}
		glNamedBufferSubData(light_ssbo->GetID(), 0, needed_size, gpu_lights_.data());
	}

	// 3. Render to FBO
	{
		ScopedGLFramebuffer fboScope(GL_FRAMEBUFFER, fbo->GetID());
		ScopedGLViewport viewportScope(0, 0, buffer_w, buffer_h);

		// Clear to Ambient Color
		float ambient_r = (global_color.Red() / 255.0f) * ambient_light_level;
		float ambient_g = (global_color.Green() / 255.0f) * ambient_light_level;
		float ambient_b = (global_color.Blue() / 255.0f) * ambient_light_level;

		// If global_color is (0,0,0) (not set), use a default dark ambient
		if (global_color.Red() == 0 && global_color.Green() == 0 && global_color.Blue() == 0) {
			ambient_r = 0.5f * ambient_light_level;
			ambient_g = 0.5f * ambient_light_level;
			ambient_b = 0.5f * ambient_light_level;
		}

		glClearColor(ambient_r, ambient_g, ambient_b, 1.0f);
		// Actually, for "Max" blending, we want to start with Ambient.
		glClear(GL_COLOR_BUFFER_BIT);

		// Per-zone ambient: overwrite zone areas with their specific ambient color
		// using scissor test, replacing the global ambient in those regions
		for (const auto* zone : zones) {
			if (zone) {
				drawZoneAmbient(view, *zone);
			}
		}

		if (!gpu_lights_.empty()) {
			shader->Use();

			// Setup Projection for FBO: Ortho 0..buffer_w, buffer_h..0 (Y-down)
			// This matches screen coordinate system and avoids flips
			glm::mat4 fbo_projection = glm::ortho(0.0f, static_cast<float>(buffer_w), static_cast<float>(buffer_h), 0.0f);
			shader->SetMat4("uProjection", fbo_projection);
			shader->SetFloat("uTileSize", static_cast<float>(TILE_SIZE) / view.zoom);

			// Shadow occlusion: upload blocking grid as SSBO
			bool use_shadow = shadow_occlusion && blocking && blocking->width > 0 && blocking->height > 0;
			shader->SetInt("uShadowEnabled", use_shadow ? 1 : 0);

			if (use_shadow) {
				// Prepare blocking SSBO data: 4 ints header + flat data
				size_t header_size = 4 * sizeof(int32_t);
				size_t data_size = blocking->data.size() * sizeof(int32_t); // expand to int32 for SSBO alignment
				size_t total_size = header_size + data_size;

				if (total_size > blocking_ssbo_capacity_) {
					blocking_ssbo_capacity_ = std::max(total_size, static_cast<size_t>(blocking_ssbo_capacity_ * 1.5));
					if (blocking_ssbo_capacity_ < 1024) {
						blocking_ssbo_capacity_ = 1024;
					}
					blocking_ssbo = std::make_unique<GLBuffer>();
					glNamedBufferStorage(blocking_ssbo->GetID(), blocking_ssbo_capacity_, nullptr, GL_DYNAMIC_STORAGE_BIT);
				}

				// Upload header
				int32_t header[4] = {
					static_cast<int32_t>(blocking->origin_x),
					static_cast<int32_t>(blocking->origin_y),
					static_cast<int32_t>(blocking->width),
					static_cast<int32_t>(blocking->height)
				};
				glNamedBufferSubData(blocking_ssbo->GetID(), 0, header_size, header);

				// Upload blocking data (expand uint8 to int32 for shader)
				std::vector<int32_t> blocking_int(blocking->data.size());
				for (size_t i = 0; i < blocking->data.size(); ++i) {
					blocking_int[i] = blocking->data[i];
				}
				glNamedBufferSubData(blocking_ssbo->GetID(), header_size, data_size, blocking_int.data());

				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, blocking_ssbo->GetID());

				// Pass view parameters for tile coordinate conversion
				shader->SetFloat("uViewScrollX", static_cast<float>(view.view_scroll_x));
				shader->SetFloat("uViewScrollY", static_cast<float>(view.view_scroll_y));
				shader->SetFloat("uZoom", view.zoom);
			}

			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, light_ssbo->GetID());
			glBindVertexArray(vao->GetID());

			// Enable MAX blending
			{
				ScopedGLCapability blendCap(GL_BLEND);
				ScopedGLBlend blendState(GL_ONE, GL_ONE, GL_MAX); // Factors don't matter much for MAX, but usually 1,1 is safe

				if (gpu_lights_.size() > static_cast<size_t>(std::numeric_limits<GLsizei>::max())) {
					spdlog::error("Too many lights for glDrawArraysInstanced");
					return;
				}
				glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, static_cast<GLsizei>(gpu_lights_.size()));
			}

			glBindVertexArray(0);
		}
	}

	// 4. Composite FBO to Screen
	// Actually MapDrawer doesn't seem to set viewport every time, but `view.projectionMatrix` assumes 0..screensize.

	// Use PrimitiveRenderer or just a simple quad?
	// We can use a simple Blit shader or re-use `sprite_drawer->glBlitSquare` if it supports textures.
	// Or just do a quick manual draw using fixed pipeline or a simple shader.
	// Let's use `glBlitNamedFramebuffer`? No, we need blending (Multiply).
	// So we draw a textured quad.

	// We can reuse the `shader` but we need a "PASS THROUGH" mode or a separate shader.
	// Easier to just use `glEnable(GL_TEXTURE_2D)` and fixed function if compatible? No, we are in Core Profile likely.
	// Let's assume we need a simple texture shader.
	// BUT wait, `LightDrawer::draw` previously drew a quad with the computed light.
	// We can just use a simple "Texture Shader" here.

	// WARNING: We don't have a generic texture shader easily accessible here?
	// `floor_drawer` etc use `sprite_batch`.
	// We can use `sprite_batch` to draw the FBO texture!
	// `sprite_batch` usually takes Atlas Region.
	// Does `SpriteBatch` support raw texture ID?
	// `SpriteBatch` seems to assume Atlas.

	// Let's use the local `shader` with a "Mode" switch? Or just a second tiny shader.
	// Adding a mode switch to `shader` is easiest. "uMode": 0 = Light Render, 1 = Composite.

	shader->Use();
	shader->SetInt("uMode", 1); // Composite Mode

	// Bind FBO texture
	glBindTextureUnit(0, fbo_texture->GetID());
	shader->SetInt("uTexture", 0);

	// Quad Transform for Screen
	float draw_dest_x = 0.0f;
	float draw_dest_y = 0.0f;
	float draw_dest_w = static_cast<float>(buffer_w) * view.zoom;
	float draw_dest_h = static_cast<float>(buffer_h) * view.zoom;

	glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(draw_dest_x, draw_dest_y, 0.0f));
	model = glm::scale(model, glm::vec3(draw_dest_w, draw_dest_h, 1.0f));
	shader->SetMat4("uProjection", view.projectionMatrix * view.viewMatrix * model); // reusing uProjection as MVP

	float uv_w = static_cast<float>(buffer_w) / static_cast<float>(buffer_width);
	float uv_h = static_cast<float>(buffer_h) / static_cast<float>(buffer_height);

	shader->SetVec2("uUVMin", glm::vec2(0.0f, uv_h));
	shader->SetVec2("uUVMax", glm::vec2(uv_w, 0.0f));

	// Blending: Dst * Src
	{
		ScopedGLCapability blendCap(GL_BLEND);
		ScopedGLBlend blendState(GL_DST_COLOR, GL_ZERO);

		glBindVertexArray(vao->GetID());
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
		glBindVertexArray(0);
	}

	shader->SetInt("uMode", 0); // Reset
}

void LightDrawer::initRenderResources() {
	// Modes: 0 = Light Generation (Instanced), 1 = Composite (Simple Texture)
	const char* vs = R"(
		#version 450 core
		layout (location = 0) in vec2 aPos; // 0..1 Quad

		uniform int uMode;
		uniform mat4 uProjection;
		uniform float uTileSize;
		uniform vec2 uUVMin;
		uniform vec2 uUVMax;

		struct Light {
			vec2 position;
			float intensity;
			float padding;
			vec4 color;
		};
		layout(std430, binding = 0) buffer LightBlock {
			Light uLights[];
		};

		out vec2 TexCoord;
		out vec4 FragColor; // For Mode 0
		flat out vec2 LightScreenPos; // Light center in screen pixels (for shadow)
		flat out float LightIntensity; // Light intensity in tiles (for shadow)

		void main() {
			if (uMode == 0) {
				// LIGHT GENERATION
				Light l = uLights[gl_InstanceID];

				float radiusPx = l.intensity * uTileSize;
				float size = radiusPx * 2.0;

				vec2 center = l.position;

				vec2 localPos = (aPos - 0.5) * size;
				vec2 worldPos = center + localPos;

				gl_Position = uProjection * vec4(worldPos, 0.0, 1.0);

				TexCoord = aPos - 0.5; // -0.5 to 0.5
				FragColor = l.color;
				LightScreenPos = center;
				LightIntensity = l.intensity;
			} else {
				// COMPOSITE
				gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
				TexCoord = mix(uUVMin, uUVMax, aPos);
				LightScreenPos = vec2(0.0);
				LightIntensity = 0.0;
			}
		}
	)";

	const char* fs = R"(
		#version 450 core
		in vec2 TexCoord;
		in vec4 FragColor; // From VS
		flat in vec2 LightScreenPos;
		flat in float LightIntensity;

		uniform int uMode;
		uniform sampler2D uTexture;
		uniform int uShadowEnabled;
		uniform float uViewScrollX;
		uniform float uViewScrollY;
		uniform float uZoom;
		uniform float uTileSize;

		layout(std430, binding = 1) buffer BlockingBlock {
			int gridOriginX;
			int gridOriginY;
			int gridWidth;
			int gridHeight;
			int blockingData[];
		};

		bool isTileBlocked(int tx, int ty) {
			int lx = tx - gridOriginX;
			int ly = ty - gridOriginY;
			if (lx < 0 || lx >= gridWidth || ly < 0 || ly >= gridHeight) return false;
			return blockingData[ly * gridWidth + lx] != 0;
		}

		bool rayBlocked(ivec2 from, ivec2 to) {
			int dx = abs(to.x - from.x);
			int dy = abs(to.y - from.y);
			int sx = from.x < to.x ? 1 : -1;
			int sy = from.y < to.y ? 1 : -1;
			int err = dx - dy;
			int x = from.x, y = from.y;
			bool first = true;
			int maxIter = 40; // safety limit
			while ((x != to.x || y != to.y) && maxIter > 0) {
				if (!first && isTileBlocked(x, y)) return true;
				first = false;
				int e2 = 2 * err;
				if (e2 > -dy) { err -= dy; x += sx; }
				if (e2 < dx)  { err += dx; y += sy; }
				maxIter--;
			}
			return false;
		}

		float getShadowVisibility(vec2 fragScreenPos) {
			// Convert screen position to map tile coordinates
			float tilePixels = uTileSize * uZoom; // should not be needed, uTileSize is already in screen pixels
			// Actually uTileSize = TILE_SIZE / zoom in the draw code
			// fragScreenPos is in FBO screen pixels
			// map_x = (fragScreenPos.x * zoom + viewScrollX) / TILE_SIZE
			float tileSizeMap = uTileSize * uZoom; // = TILE_SIZE (map pixels per tile)
			int fragTileX = int(floor((fragScreenPos.x * uZoom + uViewScrollX) / tileSizeMap));
			int fragTileY = int(floor((fragScreenPos.y * uZoom + uViewScrollY) / tileSizeMap));
			int lightTileX = int(floor((LightScreenPos.x * uZoom + uViewScrollX) / tileSizeMap));
			int lightTileY = int(floor((LightScreenPos.y * uZoom + uViewScrollY) / tileSizeMap));

			// Single-sample ray for performance
			if (rayBlocked(ivec2(lightTileX, lightTileY), ivec2(fragTileX, fragTileY))) {
				return 0.0;
			}
			return 1.0;
		}

		out vec4 OutColor;

		void main() {
			if (uMode == 0) {
				// Light Falloff
				float dist = length(TexCoord) * 2.0;
				if (dist > 1.0) discard;

				float falloff = 1.0 - dist;

				// Shadow occlusion
				if (uShadowEnabled != 0) {
					// Get fragment position in screen pixels from gl_FragCoord
					float visibility = getShadowVisibility(gl_FragCoord.xy);
					falloff *= visibility;
				}

				OutColor = FragColor * falloff;
			} else {
				// Texture fetch
				OutColor = texture(uTexture, TexCoord);
			}
		}
	)";

	shader = std::make_unique<ShaderProgram>();
	shader->Load(vs, fs);

	float vertices[] = {
		0.0f, 0.0f, // BL
		1.0f, 0.0f, // BR
		1.0f, 1.0f, // TR
		0.0f, 1.0f // TL
	};

	vao = std::make_unique<GLVertexArray>();
	vbo = std::make_unique<GLBuffer>();
	light_ssbo = std::make_unique<GLBuffer>();

	glNamedBufferStorage(vbo->GetID(), sizeof(vertices), vertices, 0);

	glVertexArrayVertexBuffer(vao->GetID(), 0, vbo->GetID(), 0, 2 * sizeof(float));

	glEnableVertexArrayAttrib(vao->GetID(), 0);
	glVertexArrayAttribFormat(vao->GetID(), 0, 2, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribBinding(vao->GetID(), 0, 0);

	glBindVertexArray(0);
}

void LightDrawer::drawZoneAmbient(const RenderView& view, const ForcedLightZone& zone) {
	// This method draws a dark quad over the zone area on the FBO
	// It should be called between the FBO clear and the light rendering pass
	// The zone ambient replaces the global ambient in the zone area

	if (!shader || !fbo || !vao) {
		return;
	}

	Position bmin = zone.getBoundsMin();
	Position bmax = zone.getBoundsMax();

	// Convert zone bounds to screen coordinates
	float minScreenX = static_cast<float>((bmin.x * TILE_SIZE - view.view_scroll_x)) / view.zoom;
	float minScreenY = static_cast<float>((bmin.y * TILE_SIZE - view.view_scroll_y)) / view.zoom;
	float maxScreenX = static_cast<float>(((bmax.x + 1) * TILE_SIZE - view.view_scroll_x)) / view.zoom;
	float maxScreenY = static_cast<float>(((bmax.y + 1) * TILE_SIZE - view.view_scroll_y)) / view.zoom;

	// Zone ambient color
	wxColor zc = colorFromEightBit(zone.ambientColor);
	float zone_ambient = zone.ambient / 255.0f;

	float r = (zc.Red() / 255.0f) * zone_ambient;
	float g = (zc.Green() / 255.0f) * zone_ambient;
	float b = (zc.Blue() / 255.0f) * zone_ambient;

	// Use a simple colored quad approach: write the zone ambient color directly
	// We use the composite mode (mode 1) with a solid color
	// Since the FBO is already cleared with global ambient, we draw the zone area
	// with the zone's specific ambient (which is usually darker)

	// We use glScissor to restrict drawing to the zone area, then clear with zone ambient
	int scissor_x = static_cast<int>(std::max(minScreenX, 0.0f));
	int scissor_y = static_cast<int>(std::max(minScreenY, 0.0f));
	int scissor_w = static_cast<int>(maxScreenX - minScreenX);
	int scissor_h = static_cast<int>(maxScreenY - minScreenY);

	// The FBO uses Y-down, but glScissor uses Y-up from bottom
	int fbo_h = view.screensize_y;
	int scissor_y_gl = fbo_h - scissor_y - scissor_h;

	if (scissor_w <= 0 || scissor_h <= 0) {
		return;
	}

	glEnable(GL_SCISSOR_TEST);
	glScissor(scissor_x, scissor_y_gl, scissor_w, scissor_h);
	glClearColor(r, g, b, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);
}

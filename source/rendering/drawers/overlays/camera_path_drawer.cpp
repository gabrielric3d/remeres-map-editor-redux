#include "app/main.h"
#include "rendering/drawers/overlays/camera_path_drawer.h"
#include "rendering/core/primitive_renderer.h"
#include "rendering/core/drawing_options.h"
#include "editor/editor.h"
#include "map/map.h"
#include "app/definitions.h"

#include <nanovg.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {
	// Half the side of a keyframe marker, in map pixels.
	constexpr int kMarkerHalf = 8;

	// Dark copies around the text first, then the text: readable over any ground.
	void drawOutlinedText(NVGcontext* vg, float x, float y, const char* text, NVGcolor color) {
		nvgFillColor(vg, nvgRGBAf(0.0f, 0.0f, 0.0f, color.a * 0.85f));
		for (int dx = -1; dx <= 1; dx += 2) {
			for (int dy = -1; dy <= 1; dy += 2) {
				nvgText(vg, x + dx, y + dy, text, nullptr);
			}
		}
		nvgFillColor(vg, color);
		nvgText(vg, x, y, text, nullptr);
	}

	// "|| 2.0s" pill under a keyframe that holds the camera still.
	void drawHoldBadge(NVGcontext* vg, float cx, float top, double hold, NVGcolor tint, float alpha) {
		char text[32];
		std::snprintf(text, sizeof(text), "%.1fs", hold);

		nvgFontSize(vg, 11.0f);
		float bounds[4];
		nvgTextBounds(vg, 0.0f, 0.0f, text, nullptr, bounds);
		const float text_w = bounds[2] - bounds[0];

		constexpr float pad = 4.0f;
		constexpr float icon_w = 7.0f;
		constexpr float gap = 3.0f;
		constexpr float height = 14.0f;
		const float width = pad + icon_w + gap + text_w + pad;
		const float left = cx - width * 0.5f;
		const float mid_y = top + height * 0.5f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, left, top, width, height, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.0f, 0.0f, 0.0f, 0.7f * alpha));
		nvgFill(vg);

		// Pause sign in the path color.
		nvgBeginPath(vg);
		nvgRect(vg, left + pad, mid_y - 4.0f, 2.5f, 8.0f);
		nvgRect(vg, left + pad + 4.5f, mid_y - 4.0f, 2.5f, 8.0f);
		nvgFillColor(vg, tint);
		nvgFill(vg);

		nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, alpha));
		nvgText(vg, left + pad + icon_w + gap + text_w * 0.5f, mid_y, text, nullptr);
	}
} // namespace

CameraPathDrawer::CameraPathDrawer() {
}

CameraPathDrawer::~CameraPathDrawer() {
}

void CameraPathDrawer::draw(PrimitiveRenderer& renderer, const RenderView& view, const DrawingOptions& options, Editor& editor) {
	if (!options.show_camera_paths) {
		return;
	}

	const CameraPaths& cam_paths = editor.map.camera_paths;
	const std::vector<CameraPath>& paths = cam_paths.getPaths();
	if (paths.empty()) {
		return;
	}

	const CameraPath* active_path = cam_paths.getActivePath();
	int active_keyframe = cam_paths.getActiveKeyframe();
	constexpr int half_tile = TILE_SIZE / 2;

	// The map -> screen transform is linear, so the spline's fractional tiles land
	// between tiles instead of snapping to them.
	int origin_x = 0;
	int origin_y = 0;
	view.getScreenPosition(0, 0, view.floor, origin_x, origin_y);
	auto toScreen = [&](const CameraPathSample& sample) {
		return glm::vec2(
			static_cast<float>(origin_x + sample.x * TILE_SIZE + half_tile),
			static_cast<float>(origin_y + sample.y * TILE_SIZE + half_tile));
	};

	for (const auto& path : paths) {
		bool is_active = (active_path && path.name == active_path->name);
		float alpha = is_active ? 0.9f : 0.4f;
		float line_alpha = is_active ? 0.7f : 0.3f;

		glm::vec4 path_color(
			path.color.r / 255.0f,
			path.color.g / 255.0f,
			path.color.b / 255.0f,
			line_alpha);

		// Draw spline segments
		if (path.keyframes.size() >= 2) {
			size_t count = path.keyframes.size();
			size_t segments = path.loop ? count : (count - 1);
			constexpr int steps = 12;

			for (size_t seg = 0; seg < segments; ++seg) {
				const auto& kf = path.keyframes[seg];
				size_t next_idx = path.loop ? ((seg + 1) % count) : (seg + 1);
				const auto& kf_next = path.keyframes[next_idx];

				// Only draw segments where at least one keyframe is on the current floor
				if (kf.pos.z != view.floor && kf_next.pos.z != view.floor) {
					continue;
				}

				// Sampled by segment, not by time: durations and holds change how long the
				// camera takes, not the shape of the curve.
				for (int s = 0; s < steps; ++s) {
					const CameraPathSample s1 = SampleCameraPathSegment(path, seg, static_cast<double>(s) / steps, path.loop);
					const CameraPathSample s2 = SampleCameraPathSegment(path, seg, static_cast<double>(s + 1) / steps, path.loop);
					renderer.drawLine(toScreen(s1), toScreen(s2), path_color);
				}
			}
		}

		// Draw keyframe markers
		for (size_t i = 0; i < path.keyframes.size(); ++i) {
			const auto& kf = path.keyframes[i];
			if (kf.pos.z != view.floor) {
				continue;
			}

			int sx, sy;
			view.getScreenPosition(kf.pos.x, kf.pos.y, kf.pos.z, sx, sy);

			int marker_size = kMarkerHalf;
			int cx = sx + half_tile;
			int cy = sy + half_tile;

			glm::vec4 fill_color(
				path.color.r / 255.0f,
				path.color.g / 255.0f,
				path.color.b / 255.0f,
				alpha);

			renderer.drawRect(
				glm::vec4(cx - marker_size, cy - marker_size, marker_size * 2, marker_size * 2),
				fill_color);

			// Highlight active keyframe
			if (is_active && static_cast<int>(i) == active_keyframe) {
				glm::vec4 border_color(1.0f, 1.0f, 1.0f, 0.9f);
				renderer.drawBox(
					glm::vec4(cx - marker_size - 1, cy - marker_size - 1, marker_size * 2 + 2, marker_size * 2 + 2),
					border_color, 2.0f);
			}
		}
	}
}

void CameraPathDrawer::drawLabels(NVGcontext* vg, const RenderView& view, const DrawingOptions& options, Editor& editor) {
	if (!vg || !options.show_camera_paths) {
		return;
	}

	const CameraPaths& cam_paths = editor.map.camera_paths;
	const std::vector<CameraPath>& paths = cam_paths.getPaths();
	if (paths.empty()) {
		return;
	}

	const CameraPath* active_path = cam_paths.getActivePath();
	// RenderView positions are in map pixels; NanoVG draws in window pixels.
	const float zoom = (view.zoom > 0.0f) ? view.zoom : 1.0f;
	const float half_tile = TILE_SIZE * 0.5f / zoom;
	const float marker_half = kMarkerHalf / zoom;

	nvgSave(vg);
	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

	for (const auto& path : paths) {
		const bool is_active = (active_path && path.name == active_path->name);
		const float alpha = is_active ? 1.0f : 0.6f;
		const NVGcolor tint = nvgRGBAf(path.color.r / 255.0f, path.color.g / 255.0f, path.color.b / 255.0f, alpha);

		for (size_t i = 0; i < path.keyframes.size(); ++i) {
			const auto& kf = path.keyframes[i];
			if (kf.pos.z != view.floor) {
				continue;
			}

			int sx, sy;
			if (!view.IsTileVisible(kf.pos.x, kf.pos.y, kf.pos.z, sx, sy)) {
				continue;
			}
			const float cx = sx / zoom + half_tile;
			const float cy = sy / zoom + half_tile;

			const std::string number = std::to_string(i + 1);
			nvgFontSize(vg, 12.0f);
			drawOutlinedText(vg, cx, cy, number.c_str(), nvgRGBAf(1.0f, 1.0f, 1.0f, alpha));

			if (kf.hold > 0.0) {
				drawHoldBadge(vg, cx, cy + std::max(marker_half, 7.0f) + 3.0f, kf.hold, tint, alpha);
			}
		}
	}

	nvgRestore(vg);
}

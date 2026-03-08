#include "app/main.h"
#include "rendering/drawers/overlays/camera_path_drawer.h"
#include "rendering/core/primitive_renderer.h"
#include "rendering/core/drawing_options.h"
#include "editor/editor.h"
#include "map/map.h"
#include "app/definitions.h"

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
	const int half_tile = TileSize / 2;

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

				for (int s = 0; s < steps; ++s) {
					double t1 = static_cast<double>(s) / steps;
					double t2 = static_cast<double>(s + 1) / steps;

					CameraPathSample s1 = SampleCameraPathByTime(path,
						GetCameraPathDuration(path, path.loop) * (static_cast<double>(seg) + t1) / segments,
						path.loop);
					CameraPathSample s2 = SampleCameraPathByTime(path,
						GetCameraPathDuration(path, path.loop) * (static_cast<double>(seg) + t2) / segments,
						path.loop);

					int x1, y1, x2, y2;
					view.getScreenPosition(static_cast<int>(s1.x), static_cast<int>(s1.y), view.floor, x1, y1);
					view.getScreenPosition(static_cast<int>(s2.x), static_cast<int>(s2.y), view.floor, x2, y2);

					x1 += half_tile;
					y1 += half_tile;
					x2 += half_tile;
					y2 += half_tile;

					renderer.drawLine(
						glm::vec2(x1, y1),
						glm::vec2(x2, y2),
						path_color);
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

			int marker_size = 8;
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

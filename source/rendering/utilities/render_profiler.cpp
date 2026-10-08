//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/utilities/render_profiler.h"

#include "rendering/core/drawing_options.h"
#include "rendering/core/render_order.h"

#include <glad/glad.h>
#include <nanovg.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <string>
#include <vector>

namespace RenderProfiler {

	namespace detail {
		bool enabled = false;
		std::array<double, kSectionCount> frame_ms {};
		std::array<int64_t, kCounterCount> frame_counters {};
	}

	namespace {
		using Clock = std::chrono::steady_clock;

		struct SectionInfo {
			const char* label; // HUD
			const char* log_name; // rme_debug.log
			uint8_t depth;
		};

		constexpr SectionInfo kSectionInfo[] = {
			{ "frame (OnPaint)", "Frame", 0 },
			{ "preloader drain", "PreloadDrain", 1 },
			{ "options.Update", "OptionsUpdate", 1 },
			{ "SetupVars", "SetupVars", 1 },
			{ "SetupGL", "SetupGL", 1 },
			{ "MapDrawer::Draw", "Draw", 1 },
			{ "chunk cache bookkeeping", "ChunkBookkeeping", 2 },
			{ "TooltipCollector", "TooltipCollect", 2 },
			{ "DrawMap (all floors)", "DrawMap", 2 },
			{ "LightGatherer", "LightGather", 3 },
			{ "batch flush before floor", "FloorBatchFlush", 3 },
			{ "chunk cache: ground+borders", "ChunkRender", 3 },
			{ "chunk bake", "ChunkBake", 4 },
			{ "deferred ground (CPU)", "DeferredGround", 3 },
			{ "deferred borders (CPU)", "DeferredBorders", 3 },
			{ "chunk cache: contents", "ChunkRenderContents", 3 },
			{ "deferred contents (CPU)", "DeferredContents", 3 },
			{ "tile passes, cache off (CPU)", "CpuTilePasses", 3 },
			{ "ShadeDrawer", "Shade", 3 },
			{ "PreviewDrawer", "Preview", 3 },
			{ "FloorDrawer", "FloorDrawer", 3 },
			{ "map batch flush", "MapFlush", 2 },
			{ "GPU wait: map (glFinish)", "GpuWaitMap", 2 },
			{ "LightDrawer", "Light", 2 },
			{ "post-process", "PostProcess", 2 },
			{ "GL overlays", "GlOverlays", 2 },
			{ "brush overlay", "BrushOverlay", 3 },
			{ "spawn overlay", "SpawnOverlay", 3 },
			{ "grid", "Grid", 3 },
			{ "Lua overlay", "LuaOverlay", 3 },
			{ "NanoVG overlays", "NanoVG", 1 },
			{ "creature names", "NvgCreatureNames", 2 },
			{ "tooltips", "NvgTooltips", 2 },
			{ "hook indicators", "NvgHooks", 2 },
			{ "light indicators", "NvgLightIndicators", 2 },
			{ "door indicators", "NvgDoors", 2 },
			{ "item indicators", "NvgItemIndicators", 2 },
			{ "mountain overlay", "NvgMountain", 2 },
			{ "pathing overlay", "NvgPathing", 2 },
			{ "wall borders", "NvgWallBorders", 2 },
			{ "stair directions", "NvgStairs", 2 },
			{ "spawn labels", "NvgSpawnLabels", 2 },
			{ "light zone labels", "NvgZoneLabels", 2 },
			{ "solid instance zones", "NvgSolidZones", 2 },
			{ "instance/sound zone labels", "NvgPaintedZones", 2 },
			{ "world boss labels", "NvgWorldBoss", 2 },
			{ "BR loot zones", "NvgBRLoot", 2 },
			{ "Lua UI", "NvgLuaUI", 2 },
			{ "HUD, toasts, radial", "NvgHud", 2 },
			{ "this profiler", "NvgProfilerHud", 2 },
			{ "texture GC", "TextureGC", 1 },
			{ "GPU wait: frame (glFinish)", "GpuWaitFrame", 1 },
			{ "SwapBuffers", "SwapBuffers", 1 },
			{ "FPS limiter sleep", "FpsLimiter", 0 },
			{ "sync sprite loads (already in above)", "SyncSpriteLoad", 0 },
		};
		static_assert(std::size(kSectionInfo) == kSectionCount, "kSectionInfo tem de ter uma linha por RenderProfiler::Section, na mesma ordem");

		// per_second: eventos (invalidacao, eviction, carga sincrona) dizem mais
		// somados na janela do que em media por frame.
		struct CounterInfo {
			const char* log_name;
			bool per_second;
		};

		constexpr CounterInfo kCounterInfo[] = {
			{ "floors", false },
			{ "chunks", false },
			{ "quads", false },
			{ "baked", false },
			{ "bakePending", false },
			{ "invAtlas", true },
			{ "invOptions", true },
			{ "invTracker", true },
			{ "dirtyChunks", true },
			{ "defGround", false },
			{ "defBorders", false },
			{ "defContents", false },
			{ "cpuTiles", false },
			{ "whyAnimated", false },
			{ "whyLight", false },
			{ "whyTechnical", false },
			{ "whyIndicator", false },
			{ "whySelected", false },
			{ "whyMarker", false },
			{ "whyOther", false },
			{ "sprites", false },
			{ "drawCalls", false },
			{ "atlasEvict", true },
			{ "syncLoads", true },
			{ "preloadUp", true },
		};
		static_assert(std::size(kCounterInfo) == kCounterCount, "kCounterInfo tem de ter uma linha por RenderProfiler::Counter, na mesma ordem");

		constexpr auto kWindow = std::chrono::seconds(1);

		struct Window {
			int frames = 0;
			Clock::time_point start {};
			std::array<double, kSectionCount> sum_ms {};
			std::array<int64_t, kCounterCount> sum_counters {};
			// O pior frame e guardado inteiro: o maximo de cada secao viria de frames
			// diferentes e nao somaria nada que tenha acontecido de verdade.
			double worst_frame_ms = -1.0;
			std::array<double, kSectionCount> worst_ms {};
			std::array<int64_t, kCounterCount> worst_counters {};
		};

		struct Report {
			bool valid = false;
			int frames = 0;
			double window_s = 0.0;
			float zoom = 1.0f;
			int floor = 0;
			std::array<double, kSectionCount> avg_ms {};
			std::array<double, kSectionCount> worst_ms {};
			// Media por frame, ou por segundo quando o contador e per_second.
			std::array<double, kCounterCount> avg_counters {};
			std::array<int64_t, kCounterCount> worst_counters {};
			std::string options_summary;
			std::string options_short;
		};

		Clock::time_point frame_start;
		bool frame_open = false;
		bool frame_marked = false;
		Window frame_window;
		Report report;
		std::string last_logged_options;

		constexpr size_t slot(Section section) noexcept {
			return static_cast<size_t>(section);
		}

		constexpr size_t slot(Counter counter) noexcept {
			return static_cast<size_t>(counter);
		}

		double millisecondsSince(Clock::time_point start) {
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		const char* onOff(bool value) {
			return value ? "on" : "off";
		}

		std::string formatCount(double value) {
			if (value >= 1'000'000.0) {
				return std::format("{:.2f}M", value / 1'000'000.0);
			}
			if (value >= 10'000.0) {
				return std::format("{:.1f}k", value / 1'000.0);
			}
			if (value >= 100.0) {
				return std::format("{:.0f}", value);
			}
			return std::format("{:.1f}", value);
		}

		// Tudo que muda o custo do frame em vista afastada. Vai inteiro para o log
		// (so quando muda) para que duas medicoes possam ser comparadas.
		std::string describeOptions(const DrawingOptions& o) {
			return std::format(
				"cache={} order={} anim={} lights={} shader={} aa={} allFloors={} ghostFloors={} shade={} grid={} | "
				"indicators: pickup={} move={} tech={} hooks={} doors={} lightStr={} | "
				"overlays: tooltips={} creatures={} names={} spawns={} houses={} mountain={} pathing={} walls={} stairs={} "
				"instanceZones={} soundZones={} worldBoss={} invalidZones={} | hideItems={} below {:.0f}%",
				o.use_chunk_cache, RenderOrder::profileName(o.render_order), o.show_preview, o.show_lights, o.screen_shader_name.empty() ? "none" : o.screen_shader_name,
				o.anti_aliasing, o.show_all_floors, o.ghost_floors_enabled, o.show_shade, o.show_grid,
				o.show_pickupables, o.show_moveables, o.show_tech_items, o.show_hooks, o.highlight_locked_doors, o.show_light_str,
				o.show_tooltips, o.show_creatures, o.show_creature_names, o.show_spawns, o.show_houses, o.show_mountain_overlay,
				o.show_blocking, o.show_wall_borders, o.show_stair_direction, o.show_instance_zones, o.show_sound_zones,
				o.show_worldboss_zones, o.show_invalid_zones, o.hide_items_when_zoomed,
				o.hide_items_zoom > 0.0f ? 100.0f / o.hide_items_zoom : 0.0f
			);
		}

		std::string describeOptionsShort(const DrawingOptions& o) {
			return std::format(
				"cache {} | animation {} | lights {} | shader {} | all floors {}",
				onOff(o.use_chunk_cache), onOff(o.show_preview), onOff(o.show_lights),
				o.screen_shader_name.empty() ? "none" : o.screen_shader_name, onOff(o.show_all_floors)
			);
		}

		void logReport(const Report& r) {
			if (r.options_summary != last_logged_options) {
				spdlog::info("[RenderProfiler] options: {}", r.options_summary);
				last_logged_options = r.options_summary;
			}

			std::string line = std::format(
				"[RenderProfiler] zoom {:.0f}% ({:.2f}) floor {} | {} frames in {:.1f}s | ms avg/worst:",
				r.zoom > 0.0f ? 100.0f / r.zoom : 0.0f, r.zoom, r.floor, r.frames, r.window_s
			);
			for (size_t i = 0; i < kSectionCount; ++i) {
				if (i == slot(Section::Frame) || r.avg_ms[i] >= 0.05 || r.worst_ms[i] >= 0.5) {
					std::format_to(std::back_inserter(line), " {}={:.2f}/{:.2f}", kSectionInfo[i].log_name, r.avg_ms[i], r.worst_ms[i]);
				}
			}

			line += " | counters:";
			for (size_t i = 0; i < kCounterCount; ++i) {
				if (r.avg_counters[i] == 0.0 && r.worst_counters[i] == 0) {
					continue;
				}
				if (kCounterInfo[i].per_second) {
					std::format_to(std::back_inserter(line), " {}={:.1f}/s", kCounterInfo[i].log_name, r.avg_counters[i]);
				} else {
					std::format_to(std::back_inserter(line), " {}={:.1f}(worst {})", kCounterInfo[i].log_name, r.avg_counters[i], r.worst_counters[i]);
				}
			}

			spdlog::info("{}", line);
		}

		void publish(const DrawingOptions& options, int floor, Clock::time_point now) {
			Report r;
			r.valid = true;
			r.frames = frame_window.frames;
			r.window_s = std::chrono::duration<double>(now - frame_window.start).count();
			r.zoom = options.zoom;
			r.floor = floor;

			const double frames = static_cast<double>(std::max(1, frame_window.frames));
			const double seconds = std::max(r.window_s, 0.001);
			for (size_t i = 0; i < kSectionCount; ++i) {
				r.avg_ms[i] = frame_window.sum_ms[i] / frames;
			}
			r.worst_ms = frame_window.worst_ms;
			for (size_t i = 0; i < kCounterCount; ++i) {
				const double sum = static_cast<double>(frame_window.sum_counters[i]);
				r.avg_counters[i] = kCounterInfo[i].per_second ? sum / seconds : sum / frames;
			}
			r.worst_counters = frame_window.worst_counters;
			r.options_summary = describeOptions(options);
			r.options_short = describeOptionsShort(options);

			report = std::move(r);
			logReport(report);
		}

		std::vector<std::string> counterLines(const Report& r) {
			auto avg = [&r](Counter counter) {
				return r.avg_counters[slot(counter)];
			};
			auto worst = [&r](Counter counter) {
				return r.worst_counters[slot(counter)];
			};

			std::vector<std::string> lines;
			lines.push_back(std::format(
				"floors {:.0f} | chunks visited {} | cached quads {}",
				avg(Counter::FloorsDrawn), formatCount(avg(Counter::ChunksVisited)), formatCount(avg(Counter::ChunkInstances))
			));
			lines.push_back(std::format(
				"chunk bakes/frame {} (worst frame: {}) | re-bakes waiting sprite {}",
				formatCount(avg(Counter::ChunksBaked)), worst(Counter::ChunksBaked), formatCount(avg(Counter::ChunkBakesPending))
			));
			lines.push_back(std::format(
				"cache invalidateAll/s: atlas {:.1f}  options {:.1f}  tracker {:.1f} | dirty chunks/s {}",
				avg(Counter::InvalidateAtlas), avg(Counter::InvalidateOptions), avg(Counter::InvalidateTracker),
				formatCount(avg(Counter::DirtyChunks))
			));
			lines.push_back(std::format(
				"tiles on CPU/frame: ground {}  borders {}  contents {} | cache off {}",
				formatCount(avg(Counter::DeferredGround)), formatCount(avg(Counter::DeferredBorders)),
				formatCount(avg(Counter::DeferredContents)), formatCount(avg(Counter::CpuTiles))
			));
			lines.push_back(std::format(
				"why on CPU: animated {} | light {} | technical {} | indicators {} | selected {} | markers {} | other {}",
				formatCount(avg(Counter::DeferAnimated)), formatCount(avg(Counter::DeferLight)), formatCount(avg(Counter::DeferTechnical)),
				formatCount(avg(Counter::DeferIndicator)), formatCount(avg(Counter::DeferSelected)), formatCount(avg(Counter::DeferMarker)),
				formatCount(avg(Counter::DeferOther))
			));
			lines.push_back(std::format(
				"sprites {} | draws {} | atlas evictions/s {} | sync loads/s {} | preload/s {}",
				formatCount(avg(Counter::Sprites)), formatCount(avg(Counter::DrawCalls)), formatCount(avg(Counter::AtlasEvictions)),
				formatCount(avg(Counter::SyncSpriteLoads)), formatCount(avg(Counter::PreloadUploads))
			));
			return lines;
		}

		NVGcolor rowColor(uint8_t depth, double share) {
			if (depth == 0) {
				return nvgRGBA(255, 255, 255, 245);
			}
			if (share >= 0.25) {
				return nvgRGBA(255, 120, 100, 245);
			}
			if (share >= 0.10) {
				return nvgRGBA(255, 200, 110, 245);
			}
			return nvgRGBA(205, 205, 210, 235);
		}
	} // namespace

	void Toggle() {
		detail::enabled = !detail::enabled;
		frame_window = Window {};
		report = Report {};
		frame_open = false;
		last_logged_options.clear();
		spdlog::info("[RenderProfiler] {}", detail::enabled ? "on -- F9 turns it off" : "off");
	}

	void BeginFrame() {
		if (!detail::enabled) {
			frame_open = false;
			return;
		}
		detail::frame_ms.fill(0.0);
		detail::frame_counters.fill(0);
		frame_start = Clock::now();
		frame_open = true;
		frame_marked = false;
	}

	void MarkFrameEnd() {
		if (!detail::enabled || !frame_open) {
			return;
		}
		detail::frame_ms[slot(Section::Frame)] = millisecondsSince(frame_start);
		frame_marked = true;
	}

	void EndFrame(const DrawingOptions& options, int floor) {
		if (!detail::enabled || !frame_open) {
			return;
		}
		frame_open = false;

		const auto now = Clock::now();
		if (!frame_marked) {
			detail::frame_ms[slot(Section::Frame)] = std::chrono::duration<double, std::milli>(now - frame_start).count();
		}

		if (frame_window.frames == 0) {
			frame_window.start = frame_start;
		}
		++frame_window.frames;
		for (size_t i = 0; i < kSectionCount; ++i) {
			frame_window.sum_ms[i] += detail::frame_ms[i];
		}
		for (size_t i = 0; i < kCounterCount; ++i) {
			frame_window.sum_counters[i] += detail::frame_counters[i];
		}

		const double frame_ms = detail::frame_ms[slot(Section::Frame)];
		if (frame_ms > frame_window.worst_frame_ms) {
			frame_window.worst_frame_ms = frame_ms;
			frame_window.worst_ms = detail::frame_ms;
			frame_window.worst_counters = detail::frame_counters;
		}

		if (now - frame_window.start >= kWindow) {
			publish(options, floor, now);
			frame_window = Window {};
		}
	}

	void GpuSync(Section section) {
		if (!detail::enabled) {
			return;
		}
		const auto start = Clock::now();
		glFinish();
		AddTime(section, millisecondsSince(start));
	}

	void DrawHud(NVGcontext* vg, int canvas_width, int canvas_height) {
		if (!detail::enabled || !vg) {
			return;
		}
		const Scope hud_scope(Section::NvgProfilerHud);

		constexpr float kFontSize = 12.0f;
		constexpr float kLineHeight = 15.0f;
		constexpr float kPad = 8.0f;
		constexpr float kIndent = 12.0f;
		constexpr float kPanelWidth = 490.0f;
		// Colunas, relativas ao canto esquerdo do painel.
		constexpr float kAvgRight = 318.0f;
		constexpr float kWorstRight = 378.0f;
		constexpr float kShareRight = 418.0f;
		constexpr float kBarLeft = 426.0f;
		constexpr float kBarWidth = 54.0f;

		const float panel_x = 10.0f;
		const float panel_y = 10.0f;
		const float text_x = panel_x + kPad;

		std::vector<std::string> header;
		std::vector<size_t> rows;
		std::vector<std::string> footer;

		if (!report.valid) {
			header.push_back("Render profiler (F9): measuring... move or zoom the map");
		} else {
			const double frame_avg = report.avg_ms[slot(Section::Frame)];
			header.push_back(std::format(
				"Render profiler (F9)   zoom {:.0f}% ({:.2f})   floor {}",
				report.zoom > 0.0f ? 100.0f / report.zoom : 0.0f, report.zoom, report.floor
			));
			header.push_back(std::format(
				"{} frames in {:.1f}s   frame avg {:.2f} ms (~{:.0f} fps)   worst {:.2f} ms",
				report.frames, report.window_s, frame_avg, frame_avg > 0.0 ? 1000.0 / frame_avg : 0.0,
				report.worst_ms[slot(Section::Frame)]
			));
			header.push_back(report.options_short);

			for (size_t i = 0; i < kSectionCount; ++i) {
				if (i == slot(Section::Frame) || report.avg_ms[i] >= 0.05 || report.worst_ms[i] >= 0.5) {
					rows.push_back(i);
				}
			}
			footer = counterLines(report);
		}

		const size_t line_count = header.size() + (rows.empty() ? 0 : rows.size() + 1) + (footer.empty() ? 0 : footer.size());
		const float panel_height = std::min(
			kPad * 2.0f + static_cast<float>(line_count) * kLineHeight + (footer.empty() ? 0.0f : kPad),
			static_cast<float>(std::max(canvas_height - 20, 40))
		);
		const float panel_width = std::min(kPanelWidth, static_cast<float>(std::max(canvas_width - 20, 40)));

		nvgSave(vg);
		nvgScissor(vg, panel_x, panel_y, panel_width, panel_height);

		nvgBeginPath(vg);
		nvgRoundedRect(vg, panel_x, panel_y, panel_width, panel_height, 5.0f);
		nvgFillColor(vg, nvgRGBA(12, 12, 16, 220));
		nvgFill(vg);

		nvgFontFace(vg, "sans");
		nvgFontSize(vg, kFontSize);

		float y = panel_y + kPad;
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		for (size_t i = 0; i < header.size(); ++i) {
			nvgFillColor(vg, i == 0 ? nvgRGBA(120, 200, 255, 250) : nvgRGBA(235, 235, 240, 240));
			nvgText(vg, text_x, y, header[i].c_str(), nullptr);
			y += kLineHeight;
		}

		if (!rows.empty()) {
			nvgFillColor(vg, nvgRGBA(140, 140, 150, 240));
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgText(vg, text_x, y, "section", nullptr);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			nvgText(vg, panel_x + kAvgRight, y, "avg ms", nullptr);
			nvgText(vg, panel_x + kWorstRight, y, "worst", nullptr);
			nvgText(vg, panel_x + kShareRight, y, "%", nullptr);
			y += kLineHeight;

			const double frame_avg = std::max(report.avg_ms[slot(Section::Frame)], 0.001);
			for (size_t i : rows) {
				const SectionInfo& info = kSectionInfo[i];
				const double share = report.avg_ms[i] / frame_avg;

				nvgFillColor(vg, rowColor(info.depth, share));
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				nvgText(vg, text_x + static_cast<float>(info.depth) * kIndent, y, info.label, nullptr);

				nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
				const std::string avg_text = std::format("{:.2f}", report.avg_ms[i]);
				const std::string worst_text = std::format("{:.2f}", report.worst_ms[i]);
				const std::string share_text = std::format("{:.0f}", share * 100.0);
				nvgText(vg, panel_x + kAvgRight, y, avg_text.c_str(), nullptr);
				nvgText(vg, panel_x + kWorstRight, y, worst_text.c_str(), nullptr);
				nvgText(vg, panel_x + kShareRight, y, share_text.c_str(), nullptr);

				if (i != slot(Section::Frame)) {
					const float bar = kBarWidth * static_cast<float>(std::clamp(share, 0.0, 1.0));
					if (bar >= 1.0f) {
						nvgBeginPath(vg);
						nvgRect(vg, panel_x + kBarLeft, y + 3.0f, bar, kLineHeight - 6.0f);
						nvgFillColor(vg, share >= 0.25 ? nvgRGBA(230, 90, 70, 220) : nvgRGBA(90, 160, 230, 200));
						nvgFill(vg);
					}
				}
				y += kLineHeight;
			}
		}

		if (!footer.empty()) {
			y += kPad;
			nvgFillColor(vg, nvgRGBA(185, 225, 185, 240));
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			for (const std::string& line : footer) {
				nvgText(vg, text_x, y, line.c_str(), nullptr);
				y += kLineHeight;
			}
		}

		nvgResetScissor(vg);
		nvgRestore(vg);
	}

} // namespace RenderProfiler

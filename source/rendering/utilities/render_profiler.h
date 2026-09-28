//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_UTILITIES_RENDER_PROFILER_H_
#define RME_RENDERING_UTILITIES_RENDER_PROFILER_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

struct NVGcontext;
struct DrawingOptions;

// Profiler de frame do canvas do mapa. F9 liga e desliga.
//
// Mede o tempo de CPU de cada etapa do MapCanvas::OnPaint e conta o que o frame
// fez: chunks visitados e re-bakeados, tiles que voltaram para a CPU, motivo de
// cada invalidacao do chunk cache, evictions do atlas, sprites carregados de forma
// sincrona no meio do desenho. A cada segundo fecha a media e o pior frame da
// janela, desenha a tabela no canto do canvas e grava o resumo no rme_debug.log.
//
// Ligado, tambem faz glFinish() depois do mapa e antes do SwapBuffers: sem isso o
// tempo de GPU some dentro do SwapBuffers e nao da para separar gargalo de CPU de
// gargalo de GPU. O fps cai um pouco enquanto mede -- o que interessa e a divisao.
//
// Desligado, cada escopo custa a leitura de um bool.
namespace RenderProfiler {

	// Ordem = ordem de exibicao. A profundidade de cada uma esta em
	// render_profiler.cpp (kSectionInfo); uma secao filha sempre vem logo depois
	// da mae e o tempo dela ja esta incluso no da mae.
	enum class Section : uint8_t {
		Frame,
		PreloadDrain,
		OptionsUpdate,
		SetupVars,
		SetupGL,
		Draw,
		ChunkBookkeeping,
		TooltipCollect,
		DrawMap,
		LightGather,
		FloorBatchFlush,
		ChunkRender,
		ChunkBake,
		DeferredGround,
		DeferredBorders,
		ChunkRenderContents,
		DeferredContents,
		CpuTilePasses,
		Shade,
		Preview,
		FloorDrawer,
		MapFlush,
		GpuWaitMap,
		Light,
		PostProcess,
		GlOverlays,
		BrushOverlay,
		SpawnOverlay,
		Grid,
		LuaOverlay,
		NanoVG,
		NvgCreatureNames,
		NvgTooltips,
		NvgHooks,
		NvgLightIndicators,
		NvgDoors,
		NvgItemIndicators,
		NvgMountain,
		NvgPathing,
		NvgWallBorders,
		NvgStairs,
		NvgSpawnLabels,
		NvgZoneLabels,
		NvgSolidZones,
		NvgPaintedZones,
		NvgWorldBoss,
		NvgBRLoot,
		NvgLuaUI,
		NvgHud,
		NvgProfilerHud,
		TextureGC,
		GpuWaitFrame,
		SwapBuffers,
		FpsLimiter,
		// Transversal: acontece dentro de outras secoes (bake, DrawTile, preview...)
		// e ja esta somada nelas. Fica separada para mostrar quanto do frame foi
		// ler/decodificar sprite no thread principal.
		SyncSpriteLoad,
		Count
	};

	enum class Counter : uint8_t {
		FloorsDrawn,
		ChunksVisited,
		ChunkInstances,
		ChunksBaked,
		ChunkBakesPending,
		InvalidateAtlas,
		InvalidateOptions,
		InvalidateTracker,
		DirtyChunks,
		DeferredGround,
		DeferredBorders,
		DeferredContents,
		CpuTiles,
		// Fatias de tile que o bake recusou, por motivo (ChunkDeferReason, mesma
		// ordem). Somadas sobre os chunks visitados no frame.
		DeferAnimated,
		DeferLight,
		DeferTechnical,
		DeferIndicator,
		DeferSelected,
		DeferMarker,
		DeferOther,
		Sprites,
		DrawCalls,
		AtlasEvictions,
		SyncSpriteLoads,
		PreloadUploads,
		Count
	};

	constexpr size_t kSectionCount = static_cast<size_t>(Section::Count);
	constexpr size_t kCounterCount = static_cast<size_t>(Counter::Count);

	namespace detail {
		extern bool enabled;
		extern std::array<double, kSectionCount> frame_ms;
		extern std::array<int64_t, kCounterCount> frame_counters;
	}

	[[nodiscard]] inline bool IsEnabled() noexcept {
		return detail::enabled;
	}

	inline void AddTime(Section section, double ms) noexcept {
		if (detail::enabled) {
			detail::frame_ms[static_cast<size_t>(section)] += ms;
		}
	}

	// Inline de proposito: e chamado por tile em laco quente, e o build nao usa LTCG.
	inline void Count(Counter counter, int64_t amount = 1) noexcept {
		if (detail::enabled) {
			detail::frame_counters[static_cast<size_t>(counter)] += amount;
		}
	}

	inline void SetCounter(Counter counter, int64_t value) noexcept {
		if (detail::enabled) {
			detail::frame_counters[static_cast<size_t>(counter)] = value;
		}
	}

	void Toggle();

	// Inicio do OnPaint: zera os acumuladores do frame.
	void BeginFrame();
	// Logo depois do SwapBuffers: fecha o tempo do frame (Section::Frame), que
	// assim nao inclui o sono do limitador de fps.
	void MarkFrameEnd();
	// Fim do OnPaint, depois do limitador de fps. Fecha o frame na janela de 1s e,
	// quando a janela vence, publica o relatorio (HUD + log).
	void EndFrame(const DrawingOptions& options, int floor);

	// glFinish() cronometrado. So roda com o profiler ligado.
	void GpuSync(Section section);

	// Tabela do ultimo relatorio, no canto superior esquerdo do canvas.
	void DrawHud(NVGcontext* vg, int canvas_width, int canvas_height);

	class Scope {
	public:
		explicit Scope(Section section) noexcept :
			Scope(section, true) {
		}

		// Mede so quando `condition` vale -- para um trecho que as vezes e o caso
		// interessante e as vezes nao (carga de sprite sincrona x vinda do preloader).
		Scope(Section section, bool condition) noexcept :
			section_(section), active_(condition && detail::enabled) {
			if (active_) {
				start_ = std::chrono::steady_clock::now();
			}
		}

		~Scope() {
			if (active_) {
				const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start_;
				AddTime(section_, elapsed.count());
			}
		}

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		Section section_;
		bool active_;
		std::chrono::steady_clock::time_point start_;
	};

} // namespace RenderProfiler

#define RENDER_PROFILE_CONCAT_INNER(a, b) a##b
#define RENDER_PROFILE_CONCAT(a, b) RENDER_PROFILE_CONCAT_INNER(a, b)
#define RENDER_PROFILE_SCOPE(section) \
	const RenderProfiler::Scope RENDER_PROFILE_CONCAT(render_profile_scope_, __LINE__)(RenderProfiler::Section::section)

#endif

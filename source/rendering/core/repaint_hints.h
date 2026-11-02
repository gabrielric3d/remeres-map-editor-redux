//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_REPAINT_HINTS_H_
#define RME_RENDERING_CORE_REPAINT_HINTS_H_

#include <algorithm>

// Por que o canvas do mapa precisa de outro frame sem ninguém mexer nele.
//
// Antes o AnimationTimer redesenhava a 60 Hz sempre que "Show animation" estava
// ligado, mesmo com o mapa parado e nada animado na tela (upstream #1057 mediu
// ~0,3 núcleo de CPU só nisso). Agora cada frame anota o que ainda está em
// movimento e o MapCanvas agenda o próximo repaint no ritmo mais curto pedido,
// ou nenhum. Quem desenha algo que muda com o relógio TEM de anotar aqui, senão
// congela até o próximo input.
//
// Só a thread principal mexe nisto (é o OnPaint do MapCanvas que abre e fecha o
// frame).
struct RepaintHints {
	// Frame começando. animation_running: o relógio das animações está andando
	// ("Show animation" ligado e zoom perto o bastante para valer a pena).
	static void BeginFrame(bool animation_running) {
		animation_running_ = animation_running;
		animation_step_ms_ = 0;
		pending_work_ = false;
		continuous_ = false;
	}

	// Sprite animado visível; step_ms = duração do frame mais curto dele.
	static void NoteAnimation(int step_ms) {
		if (animation_running_ && step_ms > 0 && (animation_step_ms_ == 0 || step_ms < animation_step_ms_)) {
			animation_step_ms_ = step_ms;
		}
	}

	// Trabalho que só termina se outro frame for desenhado: chunk esperando sprite
	// do preloader, folha ainda decodificando.
	static void NotePendingWork() {
		pending_work_ = true;
	}

	// Algo que muda a cada frame (pulso da casa, Nevasca, luz animada).
	static void NoteContinuous() {
		continuous_ = true;
	}

	// Atraso em ms até o próximo repaint, ou -1 se o mapa pode ficar parado.
	static int NextRepaintDelayMs() {
		int delay = -1;
		auto take = [&](int ms) {
			delay = delay < 0 ? ms : std::min(delay, ms);
		};
		if (continuous_) {
			take(CONTINUOUS_MS);
		}
		if (animation_step_ms_ > 0) {
			take(std::clamp(animation_step_ms_, CONTINUOUS_MS, MAX_ANIMATION_STEP_MS));
		}
		if (pending_work_) {
			take(PENDING_WORK_MS);
		}
		return delay;
	}

private:
	static constexpr int CONTINUOUS_MS = 16; // ~60 fps, o mesmo de antes
	// Animação de frame longo (água: 500 ms) repinta a cada passo, mas não espera
	// mais que isto: o frame trocado aparece com no máximo esse atraso.
	static constexpr int MAX_ANIMATION_STEP_MS = 250;
	// O chunk esperando sprite é re-assado no máximo a cada 100 ms
	// (ChunkCacheManager::WAITING_REBAKE_INTERVAL_MS); o preloader entrega em lotes.
	static constexpr int PENDING_WORK_MS = 50;

	static inline bool animation_running_ = false;
	static inline int animation_step_ms_ = 0;
	static inline bool pending_work_ = false;
	static inline bool continuous_ = false;
};

#endif

//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_UTILITIES_FRAME_PACER_H_
#define RME_RENDERING_UTILITIES_FRAME_PACER_H_

#include "rendering/utilities/fps_counter.h"

class FramePacer {
public:
	FramePacer();
	~FramePacer();

	// sprite_count/draw_calls entram no texto do contador. Sem eles nao da para
	// saber se um frame lento esta emitindo 150 mil ou 1,4 milhao de sprites -- que
	// e a primeira pergunta ao investigar queda de fps em vista afastada.
	void UpdateAndLimit(int limit, bool show_counter, int sprite_count = -1, int draw_calls = -1);

private:
	FPSCounter fps_counter;
};

#endif

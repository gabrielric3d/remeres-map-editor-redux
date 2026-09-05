//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/utilities/frame_pacer.h"
#include "rendering/ui/map_status_updater.h"

FramePacer::FramePacer() {
}

FramePacer::~FramePacer() {
}

void FramePacer::UpdateAndLimit(int limit, bool show_counter, int sprite_count, int draw_calls) {
	fps_counter.LimitFPS(limit);
	fps_counter.Update();

	if (show_counter && fps_counter.HasChanged()) {
		wxString status = fps_counter.GetStatusString();
		if (sprite_count >= 0) {
			status << wxString::Format(" | sprites: %d", sprite_count);
		}
		if (draw_calls >= 0) {
			status << wxString::Format(" | draws: %d", draw_calls);
		}
		MapStatusUpdater::UpdateFPS(status);
	}
}

//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

#include "game/animation_timer.h"
#include "rendering/ui/map_display.h"

AnimationTimer::AnimationTimer(MapCanvas* canvas) :
	wxTimer(),
	map_canvas(canvas) {
}

AnimationTimer::~AnimationTimer() {
	wxTimer::Stop();
}

void AnimationTimer::Notify() {
	map_canvas->Refresh();
}

void AnimationTimer::Schedule(int delay_ms) {
	if (delay_ms < 0) {
		Stop();
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	const auto new_due = now + std::chrono::milliseconds(delay_ms);
	if (IsRunning() && due <= new_due) {
		return; // já vai repintar antes disso
	}
	due = new_due;
	wxTimer::StartOnce(std::max(1, delay_ms));
}

void AnimationTimer::Stop() {
	if (IsRunning()) {
		wxTimer::Stop();
	}
}

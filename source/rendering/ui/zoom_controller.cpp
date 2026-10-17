//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/ui/zoom_controller.h"
#include "rendering/ui/map_display.h"
#include "ui/map_window.h"
#include "ui/gui.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
	constexpr double MIN_ZOOM = 0.125;
	constexpr double MAX_ZOOM = 25.0;

	// Um entalhe da roda multiplica o zoom por exp(velocidade x isto). Na velocidade
	// padrao (1,4) da 1,26: o mesmo passo que o zoom somado de antes dava a 32 px por
	// casa (zoom 1,0), so que igual em qualquer zoom. Somado, a 64 px por casa do
	// battle royale (zoom 0,5) um entalhe tirava metade do zoom, e afastado quase
	// nao mexia.
	constexpr double WHEEL_LOG_STEP_PER_SPEED = 0.1665;

	// A cada passo o zoom anda 1 - e^(-dt/TAU) do que falta, em escala logaritmica
	// (cada metade do caminho parece igual, de perto ou de longe). ~95% em 3 x TAU.
	constexpr double ANIMATION_TAU_SECONDS = 0.055;
	// Perto assim do alvo (em log), encosta nele e para.
	constexpr double ANIMATION_SNAP_LOG = 0.002;
	constexpr int ANIMATION_INTERVAL_MS = 10;

	double clampZoom(double value) {
		return std::clamp(value, MIN_ZOOM, MAX_ZOOM);
	}

	MapWindow* windowOf(MapCanvas* canvas) {
		return static_cast<MapWindow*>(canvas->GetParent());
	}

	// Aplica um zoom novo deixando parado o ponto da ancora: o mundo sob ela esta
	// em rolagem + ancora x zoom (pixels fisicos), entao a rolagem anda
	// -ancora x (zoom novo - zoom velho). A parte fracionaria fica guardada no
	// canvas: num passo pequeno de animacao ela e quase tudo, e trunca-la a cada
	// passo faria o ponto sob o cursor escorregar.
	void applyAnchoredZoom(MapCanvas* canvas, double new_zoom, bool final_step) {
		const double delta = new_zoom - canvas->zoom;
		if (delta != 0.0) {
			canvas->zoom = new_zoom;
			const double scale = canvas->GetContentScaleFactor();
			canvas->zoom_scroll_rest_x -= static_cast<double>(canvas->zoom_anchor_x) * delta * scale;
			canvas->zoom_scroll_rest_y -= static_cast<double>(canvas->zoom_anchor_y) * delta * scale;
		}
		const int step_x = static_cast<int>(canvas->zoom_scroll_rest_x);
		const int step_y = static_cast<int>(canvas->zoom_scroll_rest_y);
		canvas->zoom_scroll_rest_x -= step_x;
		canvas->zoom_scroll_rest_y -= step_y;

		MapWindow* window = windowOf(canvas);
		window->ResumeMinimapTrackingToCurrentView();
		// O minimapa so e redesenhado de imediato no ultimo passo.
		window->ScrollRelative(step_x, step_y, final_step);
		ZoomController::UpdateStatus(canvas);
		if (final_step) {
			canvas->UpdatePositionStatus();
		}
		canvas->Refresh();
	}
}

double ZoomController::WheelStepFactor() {
	return std::exp(g_settings.getFloat(Config::ZOOM_SPEED) * WHEEL_LOG_STEP_PER_SPEED);
}

void ZoomController::OnWheel(MapCanvas* canvas, wxMouseEvent& event) {
	if (!g_settings.getBoolean(Config::SMOOTH_ZOOM)) {
		double diff = -event.GetWheelRotation() * g_settings.getFloat(Config::ZOOM_SPEED) / 640.0;
		ApplyRelativeZoom(canvas, diff);
		return;
	}

	// Entalhes, nao a rotacao crua: touchpads mandam fracoes de entalhe, e o
	// fator exponencial lida com elas do mesmo jeito.
	const int wheel_delta = event.GetWheelDelta() > 0 ? event.GetWheelDelta() : 120;
	const double notches = -static_cast<double>(event.GetWheelRotation()) / static_cast<double>(wheel_delta);
	const double factor = std::exp(notches * g_settings.getFloat(Config::ZOOM_SPEED) * WHEEL_LOG_STEP_PER_SPEED);
	ZoomBy(canvas, factor, canvas->cursor_x, canvas->cursor_y);
}

void ZoomController::ZoomBy(MapCanvas* canvas, double factor, int anchor_x, int anchor_y) {
	const double base = canvas->zoom_anim_active ? canvas->zoom_target : canvas->zoom;
	ZoomTo(canvas, base * factor, anchor_x, anchor_y);
}

void ZoomController::ZoomTo(MapCanvas* canvas, double target, int anchor_x, int anchor_y) {
	if (anchor_x < 0 || anchor_y < 0) {
		const wxSize size = canvas->GetClientSize();
		anchor_x = size.GetWidth() / 2;
		anchor_y = size.GetHeight() / 2;
	}
	// Trocar a ancora no meio do caminho e seguro: o resto fracionario guardado e
	// rolagem ja devida, nao depende de qual ponto estava parado.
	canvas->zoom_anchor_x = anchor_x;
	canvas->zoom_anchor_y = anchor_y;
	canvas->zoom_target = clampZoom(target);

	const auto now = std::chrono::steady_clock::now();
	if (!canvas->zoom_anim_active) {
		canvas->zoom_anim_active = true;
		canvas->zoom_scroll_rest_x = 0.0;
		canvas->zoom_scroll_rest_y = 0.0;
		// Um passo de atraso ficticio: o primeiro tick, logo abaixo, ja mexe o zoom
		// neste mesmo evento -- a roda responde na hora, e a animacao completa o resto.
		canvas->zoom_anim_last_tick = now - std::chrono::milliseconds(16);
		canvas->zoom_anim_timer.Start(ANIMATION_INTERVAL_MS);
	}
	OnAnimationTick(canvas);
}

void ZoomController::OnAnimationTick(MapCanvas* canvas) {
	if (!canvas->zoom_anim_active) {
		canvas->zoom_anim_timer.Stop();
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	const double dt = std::max(0.0, std::chrono::duration<double>(now - canvas->zoom_anim_last_tick).count());
	canvas->zoom_anim_last_tick = now;

	const double target = canvas->zoom_target;
	const double current_log = std::log(std::max(canvas->zoom, MIN_ZOOM));
	const double target_log = std::log(target);
	const double remaining = target_log - current_log;

	double next = target;
	bool done = std::abs(remaining) <= ANIMATION_SNAP_LOG;
	if (!done) {
		const double t = 1.0 - std::exp(-dt / ANIMATION_TAU_SECONDS);
		const double next_log = current_log + remaining * t;
		if (std::abs(target_log - next_log) <= ANIMATION_SNAP_LOG) {
			done = true;
		} else {
			next = std::exp(next_log);
		}
	}

	applyAnchoredZoom(canvas, clampZoom(next), done);
	if (done) {
		StopAnimation(canvas);
	}
}

void ZoomController::StopAnimation(MapCanvas* canvas) {
	canvas->zoom_anim_active = false;
	canvas->zoom_anim_timer.Stop();
	canvas->zoom_scroll_rest_x = 0.0;
	canvas->zoom_scroll_rest_y = 0.0;
}

void ZoomController::SetZoom(MapCanvas* canvas, double value) {
	StopAnimation(canvas);

	if (value < 0.125) {
		value = 0.125;
	}
	if (value > 25.00) {
		value = 25.0;
	}

	if (canvas->zoom != value) {
		int center_x, center_y;
		canvas->GetScreenCenter(&center_x, &center_y);

		canvas->zoom = value;
		static_cast<MapWindow*>(canvas->GetParent())->SetScreenCenterPosition(Position(center_x, center_y, canvas->floor));
		static_cast<MapWindow*>(canvas->GetParent())->ResumeMinimapTrackingToCurrentView();

		canvas->UpdatePositionStatus();
		UpdateStatus(canvas);
		g_gui.UpdateMinimap(true);
		canvas->Refresh();
	}
}

void ZoomController::ApplyRelativeZoom(MapCanvas* canvas, double diff) {
	StopAnimation(canvas);

	double oldzoom = canvas->zoom;
	canvas->zoom += diff;

	if (canvas->zoom < 0.125) {
		diff = 0.125 - oldzoom;
		canvas->zoom = 0.125;
	}
	if (canvas->zoom > 25.00) {
		diff = 25.00 - oldzoom;
		canvas->zoom = 25.0;
	}

	UpdateStatus(canvas);

	int screensize_x, screensize_y;
	static_cast<MapWindow*>(canvas->GetParent())->GetViewSize(&screensize_x, &screensize_y);

	// This took a day to figure out!
	// Using cursor position from canvas
	int scroll_x = static_cast<int>(screensize_x * diff * (std::max(canvas->cursor_x, 1) / double(screensize_x))) * canvas->GetContentScaleFactor();
	int scroll_y = static_cast<int>(screensize_y * diff * (std::max(canvas->cursor_y, 1) / double(screensize_y))) * canvas->GetContentScaleFactor();

	static_cast<MapWindow*>(canvas->GetParent())->ResumeMinimapTrackingToCurrentView();
	static_cast<MapWindow*>(canvas->GetParent())->ScrollRelative(-scroll_x, -scroll_y, true);
	canvas->Refresh();
}

void ZoomController::UpdateStatus(MapCanvas* canvas) {
	int percentage = static_cast<int>(std::lround(g_gui.GetZoomPercent(canvas->zoom)));
	wxString ss;
	ss << "zoom: " << percentage << "%";
	g_gui.root->SetStatusText(ss, 3);
}

//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ANIMATION_TIMER_H_
#define RME_ANIMATION_TIMER_H_

#include <wx/wx.h>
#include <wx/timer.h>

#include <chrono>

class MapCanvas;

// Repaint do canvas sem input: animação na tela, sprite chegando do preloader,
// efeito contínuo. Cada frame diz quando quer o próximo (RepaintHints) e o timer
// dispara uma vez nesse prazo -- não é mais um relógio fixo de 60 Hz.
class AnimationTimer : public wxTimer {
public:
	AnimationTimer(MapCanvas* canvas);
	~AnimationTimer();

	void Notify() override;

	// Repinta daqui a delay_ms. Um prazo mais cedo já agendado vale; delay_ms < 0
	// cancela o que estiver agendado.
	void Schedule(int delay_ms);
	void Stop();

private:
	MapCanvas* map_canvas;
	std::chrono::steady_clock::time_point due {};
};

#endif

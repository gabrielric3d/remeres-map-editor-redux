//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ZOOM_CONTROLLER_H
#define RME_ZOOM_CONTROLLER_H

class MapCanvas;
class wxMouseEvent;

class ZoomController {
public:
	static void OnWheel(MapCanvas* canvas, wxMouseEvent& event);
	// Zoom imediato, centrado na tela. E o que os caminhos de camera, o Claude e
	// os comandos usam; interrompe a animacao do zoom suave.
	static void SetZoom(MapCanvas* canvas, double value);
	// Zoom imediato somando diff, com o ponto sob o cursor parado (o comportamento
	// de antes do zoom suave); interrompe a animacao.
	static void ApplyRelativeZoom(MapCanvas* canvas, double diff);

	// Zoom suave (Preferences > Interface > "Smooth zoom"): o zoom anda ate o alvo
	// em ~150 ms, com o ponto (anchor_x, anchor_y) da tela -- em pixels logicos do
	// canvas -- parado. Ancora negativa = centro da tela. ZoomBy encadeia a partir
	// do alvo em andamento, entao entalhes rapidos da roda se somam.
	static void ZoomBy(MapCanvas* canvas, double factor, int anchor_x, int anchor_y);
	static void ZoomTo(MapCanvas* canvas, double target, int anchor_x, int anchor_y);
	// Um passo da animacao (timer do canvas).
	static void OnAnimationTick(MapCanvas* canvas);
	static void StopAnimation(MapCanvas* canvas);

	// Fator de um entalhe da roda na velocidade configurada (Zoom speed).
	[[nodiscard]] static double WheelStepFactor();

	static void UpdateStatus(MapCanvas* canvas);
};

#endif

#ifndef RME_LIGHT_INDICATOR_DRAWER_H_
#define RME_LIGHT_INDICATOR_DRAWER_H_

#include <vector>
#include <cstdint>
#include "map/position.h"

struct NVGcontext;
struct RenderView;

class LightIndicatorDrawer {
public:
	LightIndicatorDrawer();
	~LightIndicatorDrawer();

	struct LightRequest {
		Position pos;
		uint16_t clientId;
	};

	void addLight(const Position& pos, uint16_t clientId);
	void clear();
	void draw(NVGcontext* vg, const RenderView& view);

private:
	std::vector<LightRequest> requests;
};

#endif

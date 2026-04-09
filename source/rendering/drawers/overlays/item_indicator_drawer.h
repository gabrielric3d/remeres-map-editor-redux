#ifndef RME_ITEM_INDICATOR_DRAWER_H_
#define RME_ITEM_INDICATOR_DRAWER_H_

#include <vector>
#include "map/position.h"

struct NVGcontext;
struct RenderView;

class ItemIndicatorDrawer {
public:
	ItemIndicatorDrawer();
	~ItemIndicatorDrawer();

	enum class IndicatorType : uint8_t {
		Pickupable,
		Moveable,
		PickupableAndMoveable,
	};

	struct IndicatorRequest {
		Position pos;
		IndicatorType type;
		bool isHouseTile;
	};

	void addIndicator(const Position& pos, IndicatorType type, bool isHouseTile);
	void clear();
	void draw(NVGcontext* vg, const RenderView& view);

private:
	std::vector<IndicatorRequest> requests;
};

#endif

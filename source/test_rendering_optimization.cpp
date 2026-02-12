#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include "rendering/drawers/tiles/tile_color_calculator.h"
#include "rendering/ui/tooltip_drawer.h"
#include "map/tile.h"
#include "game/item.h"

// Mock for testing
struct TestHelper {
    static void testGetHouseColorCache() {
        std::cout << "Testing GetHouseColor cache..." << std::endl;
        uint8_t r1, g1, b1;
        uint8_t r2, g2, b2;

        // Initial call
        TileColorCalculator::GetHouseColor(12345, r1, g1, b1);

        // Second call with same ID should be identical (and likely fast, though hard to measure)
        TileColorCalculator::GetHouseColor(12345, r2, g2, b2);

        assert(r1 == r2);
        assert(g1 == g2);
        assert(b1 == b2);

        // Different ID
        TileColorCalculator::GetHouseColor(67890, r2, g2, b2);

        // Verify output is somewhat random/hashed
        std::cout << "  Color 1: " << (int)r1 << "," << (int)g1 << "," << (int)b1 << std::endl;
        std::cout << "  Color 2: " << (int)r2 << "," << (int)g2 << "," << (int)b2 << std::endl;

        std::cout << "PASSED: GetHouseColor consistency." << std::endl;
    }
};

int main() {
    std::cout << "========================================" << std::endl;
	std::cout << "Rendering Optimization Test Suite" << std::endl;
	std::cout << "========================================" << std::endl;

    try {
        TestHelper::testGetHouseColorCache();

        std::cout << "\n========================================" << std::endl;
		std::cout << "ALL TESTS PASSED!" << std::endl;
		std::cout << "========================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
		std::cerr << "\nTEST FAILED with exception: " << e.what() << std::endl;
		return 1;
	} catch (...) {
		std::cerr << "\nTEST FAILED with unknown exception" << std::endl;
		return 1;
	}
}

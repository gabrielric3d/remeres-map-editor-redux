//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Wall Scan classifier - shape-based kNN wall-segment classifier.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "brushes/wall/wall_classifier.h"
#include "brushes/brush.h"
#include "brushes/brush_enums.h"
#include "brushes/wall/wall_brush.h"
#include "item_definitions/core/item_definition_store.h"
#include "ui/gui.h"
#include "rendering/core/game_sprite.h"
#include "rendering/core/normal_image.h"

#include <algorithm>
#include <cmath>
#include <format>

const std::array<std::string, WallClassifier::SEGMENT_COUNT> WallClassifier::SEGMENT_NAMES = {
	"horizontal", "vertical", "corner", "pole",
	"northeast diagonal", "southwest diagonal", "southeast diagonal",
	"north T", "south T", "east T", "west T", "intersection",
	"north end", "south end", "east end", "west end"
};

namespace {

// Canvas the silhouette is composited into: 2x2 tiles, so a 32x64 wall keeps its
// true proportions instead of being squashed into a single tile.
constexpr int CANVAS_DIM = 64;
constexpr int CANVAS_PIXEL_COUNT = CANVAS_DIM * CANVAS_DIM;
constexpr int GRID_DIM = 8;                       // 8x8 occupancy grid...
constexpr int GRID_CELL = CANVAS_DIM / GRID_DIM;  // ...of 8x8 pixel cells
constexpr int GRID_CELL_PIXELS = GRID_CELL * GRID_CELL;
constexpr uint8_t ALPHA_THRESHOLD = 20;
constexpr int KNN_K = 5;
constexpr float KNN_EPSILON = 1e-4f;

// The engine alignment slot backing each authored segment, in SEGMENT_NAMES order.
// These are the 16 entries of WallBrush::full_border_types, one per N/W/E/S connection
// bitmask; "untouchable" is deliberately absent (it is a flag, not a shape).
// "corner" and "northwest diagonal" share WALL_NORTHWEST_DIAGONAL in the engine, so a
// brush that only declares the latter still trains the corner bucket.
constexpr int SEGMENT_ALIGNMENTS[WallClassifier::SEGMENT_COUNT] = {
	WALL_HORIZONTAL, WALL_VERTICAL, WALL_NORTHWEST_DIAGONAL, WALL_POLE,
	WALL_NORTHEAST_DIAGONAL, WALL_SOUTHWEST_DIAGONAL, WALL_SOUTHEAST_DIAGONAL,
	WALL_NORTH_T, WALL_SOUTH_T, WALL_EAST_T, WALL_WEST_T, WALL_INTERSECTION,
	WALL_NORTH_END, WALL_SOUTH_END, WALL_EAST_END, WALL_WEST_END
};

// Composites all layers/parts of a GameSprite into a CANVAS_DIM^2 straight-alpha RGBA
// buffer over a FULLY TRANSPARENT background, frame 0, pattern 0,0,0.
//
// Parts are laid out right-to-left / bottom-to-top from the anchor tile (the bottom-right
// corner of the canvas), the same arrangement the map renderer uses. Deliberately NOT
// NvgUtils::CreateCompositeRGBA: that helper flattens the sprite onto an opaque
// background, which destroys the alpha silhouette this classifier depends on.
bool getItemRGBA64(uint16_t itemId, std::array<uint8_t, CANVAS_PIXEL_COUNT * 4>& out,
				   WallScanResult::Status& failure) {
	const auto def = g_item_definitions.get(itemId);
	if (!def) {
		failure = WallScanResult::Status::NoSprite;
		return false;
	}

	GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(def.clientId()));
	if (!gs) {
		failure = WallScanResult::Status::NoSprite;
		return false;
	}

	if (gs->width == 0 || gs->height == 0 || gs->width > 2 || gs->height > 2) {
		failure = WallScanResult::Status::TooLarge;
		return false;
	}

	out.fill(0);

	for (int layer = 0; layer < gs->layers; ++layer) {
		for (int part_w = 0; part_w < gs->width; ++part_w) {
			for (int part_h = 0; part_h < gs->height; ++part_h) {
				const size_t idx = gs->getIndex(part_w, part_h, layer, 0, 0, 0, 0);
				if (idx >= gs->spriteList.size()) {
					continue;
				}

				auto data = gs->spriteList[idx]->getRGBAData();
				if (!data) {
					continue;
				}

				// Part 0 is the anchor tile: bottom-right of the canvas.
				const int destX = CANVAS_DIM - 32 * (part_w + 1);
				const int destY = CANVAS_DIM - 32 * (part_h + 1);

				// Straight-alpha src-over blend, preserving alpha.
				for (int sy = 0; sy < 32; ++sy) {
					for (int sx = 0; sx < 32; ++sx) {
						const int src = (sy * 32 + sx) * 4;
						const uint8_t srcA = data[src + 3];
						if (srcA == 0) {
							continue;
						}
						const int dst = ((destY + sy) * CANVAS_DIM + (destX + sx)) * 4;
						if (srcA == 255) {
							out[dst + 0] = data[src + 0];
							out[dst + 1] = data[src + 1];
							out[dst + 2] = data[src + 2];
							out[dst + 3] = 255;
						} else {
							const float a = srcA / 255.0f;
							const float inv = 1.0f - a;
							out[dst + 0] = static_cast<uint8_t>(data[src + 0] * a + out[dst + 0] * inv);
							out[dst + 1] = static_cast<uint8_t>(data[src + 1] * a + out[dst + 1] * inv);
							out[dst + 2] = static_cast<uint8_t>(data[src + 2] * a + out[dst + 2] * inv);
							out[dst + 3] = static_cast<uint8_t>(srcA + out[dst + 3] * inv);
						}
					}
				}
			}
		}
	}

	return true;
}

// Result of a kNN vote: winner + runner-up segment indices and their vote shares.
struct KnnVote {
	int bestSegment = -1;
	float bestConfidence = 0.0f; // 0..100
	int secondSegment = -1;
	float secondConfidence = 0.0f; // 0..100
};

} // namespace

WallClassifier& WallClassifier::Get() {
	static WallClassifier instance;
	return instance;
}

size_t WallClassifier::sampleCount() const {
	return m_samples.size();
}

size_t WallClassifier::groupCount() const {
	return m_groupCount;
}

bool WallClassifier::extractFeatures(uint16_t itemId, std::array<float, FEATURE_COUNT>& out,
									 WallScanResult::Status& failure) {
	std::array<uint8_t, CANVAS_PIXEL_COUNT * 4> rgba;
	if (!getItemRGBA64(itemId, rgba, failure)) {
		return false;
	}

	// Single pass: per-cell opaque counts, centroid sums, quadrant counts, bounding box
	// and the global count.
	std::array<int, GRID_DIM * GRID_DIM> cellCounts {};
	std::array<int, 4> quadCounts {}; // NW, NE, SW, SE
	int opaqueCount = 0;
	float sumX = 0.0f;
	float sumY = 0.0f;
	int minX = CANVAS_DIM;
	int maxX = -1;
	int minY = CANVAS_DIM;
	int maxY = -1;

	for (int py = 0; py < CANVAS_DIM; ++py) {
		for (int px = 0; px < CANVAS_DIM; ++px) {
			const uint8_t alpha = rgba[(py * CANVAS_DIM + px) * 4 + 3];
			if (alpha <= ALPHA_THRESHOLD) {
				continue;
			}
			++opaqueCount;
			sumX += static_cast<float>(px);
			sumY += static_cast<float>(py);

			minX = std::min(minX, px);
			maxX = std::max(maxX, px);
			minY = std::min(minY, py);
			maxY = std::max(maxY, py);

			++cellCounts[(py / GRID_CELL) * GRID_DIM + (px / GRID_CELL)];

			const int quad = (py < CANVAS_DIM / 2 ? 0 : 2) + (px < CANVAS_DIM / 2 ? 0 : 1);
			++quadCounts[quad];
		}
	}

	if (opaqueCount == 0) {
		failure = WallScanResult::Status::NoSprite;
		return false;
	}

	// [0..63] 8x8 alpha-occupancy grid (fraction of opaque pixels per cell, row-major).
	for (int c = 0; c < GRID_DIM * GRID_DIM; ++c) {
		out[c] = static_cast<float>(cellCounts[c]) / static_cast<float>(GRID_CELL_PIXELS);
	}

	// [64..65] opaque-pixel centroid normalized to [0,1], stored x2.0 (extra weight).
	const float cx = (sumX / static_cast<float>(opaqueCount)) / static_cast<float>(CANVAS_DIM - 1);
	const float cy = (sumY / static_cast<float>(opaqueCount)) / static_cast<float>(CANVAS_DIM - 1);
	out[64] = cx * 2.0f;
	out[65] = cy * 2.0f;

	// [66] global density.
	out[66] = static_cast<float>(opaqueCount) / static_cast<float>(CANVAS_PIXEL_COUNT);

	// [67..70] quadrant densities (NW, NE, SW, SE), each quadrant = 32x32 pixels.
	const float quadPixels = static_cast<float>((CANVAS_DIM / 2) * (CANVAS_DIM / 2));
	for (int q = 0; q < 4; ++q) {
		out[67 + q] = static_cast<float>(quadCounts[q]) / quadPixels;
	}

	// [71..72] bounding-box extents, stored x1.5. A tall/narrow box is the clearest
	// vertical-vs-horizontal cue there is, so it outweighs raw density.
	out[71] = (static_cast<float>(maxX - minX + 1) / static_cast<float>(CANVAS_DIM)) * 1.5f;
	out[72] = (static_cast<float>(maxY - minY + 1) / static_cast<float>(CANVAS_DIM)) * 1.5f;

	return true;
}

bool WallClassifier::findOwningWall(uint16_t itemId, std::string& outBrushName, std::string& outSegment) {
	for (const auto& [name, brush] : g_brushes.getMap()) {
		if (!brush) {
			continue;
		}
		const WallBrush* wall = brush->as<WallBrush>();
		if (!wall) {
			continue;
		}

		// Prefer reporting one of the four authored segments...
		for (size_t s = 0; s < SEGMENT_COUNT; ++s) {
			if (wall->items.hasWall(itemId, SEGMENT_ALIGNMENTS[s])) {
				outBrushName = brush->getName();
				outSegment = SEGMENT_NAMES[s];
				return true;
			}
		}
		// ...but still report ownership when the item only appears in an unmodeled
		// alignment (diagonals, T pieces, ends, ...).
		for (int alignment = 0; alignment < WallBrushItems::WALL_ALIGNMENT_COUNT; ++alignment) {
			if (wall->items.hasWall(itemId, alignment)) {
				outBrushName = brush->getName();
				outSegment.clear();
				return true;
			}
		}
	}
	return false;
}

bool WallClassifier::ensureTrained() {
	const auto& brushes = g_brushes.getMap();
	if (m_trained && m_trainedBrushMapSize == brushes.size()) {
		return !m_samples.empty();
	}

	m_samples.clear();
	m_groupCount = 0;

	for (const auto& [name, brush] : brushes) {
		if (!brush) {
			continue;
		}
		const WallBrush* wall = brush->as<WallBrush>();
		if (!wall) {
			continue;
		}

		bool groupContributed = false;
		for (size_t s = 0; s < SEGMENT_COUNT; ++s) {
			// Every variant is a training sample, not just the first.
			for (const WallBrushItems::WallItem& variant : wall->items.getWallNode(SEGMENT_ALIGNMENTS[s]).items) {
				if (variant.id == 0) {
					continue;
				}
				Sample sample;
				WallScanResult::Status failure;
				if (!extractFeatures(variant.id, sample.features, failure)) {
					continue;
				}
				sample.segmentIndex = static_cast<uint8_t>(s);
				sample.itemId = variant.id;
				m_samples.push_back(sample);
				groupContributed = true;
			}
		}
		if (groupContributed) {
			++m_groupCount;
		}
	}

	m_trainedBrushMapSize = brushes.size();
	m_trained = true;
	return !m_samples.empty();
}

namespace {

struct Neighbor {
	float distSq = 0.0f;
	uint8_t segmentIndex = 0;
};

// Brute-force kNN over the sample set. Samples whose itemId equals skipItemId never vote
// (skip-self rule). Vote weight = 1 / (d^2 + epsilon); confidence is the winning segment
// share of the total vote weight, as a percentage.
// Template on the container so the private WallClassifier::Sample type is never named
// here (members are reached through deduced `auto`).
template <typename SampleVec>
KnnVote runKnnImpl(const SampleVec& samples,
				   const std::array<float, WallClassifier::FEATURE_COUNT>& query,
				   uint16_t skipItemId) {
	std::array<Neighbor, KNN_K> nearest;
	int nearestCount = 0;

	for (const auto& sample : samples) {
		if (sample.itemId == skipItemId) {
			continue; // skip-self rule
		}

		float distSq = 0.0f;
		for (size_t f = 0; f < WallClassifier::FEATURE_COUNT; ++f) {
			const float d = query[f] - sample.features[f];
			distSq += d * d;
		}

		if (nearestCount < KNN_K) {
			nearest[nearestCount++] = Neighbor { distSq, sample.segmentIndex };
			// Keep the worst neighbor at the back via simple sort (K is tiny).
			std::sort(nearest.begin(), nearest.begin() + nearestCount,
					  [](const Neighbor& a, const Neighbor& b) { return a.distSq < b.distSq; });
		} else if (distSq < nearest[KNN_K - 1].distSq) {
			nearest[KNN_K - 1] = Neighbor { distSq, sample.segmentIndex };
			std::sort(nearest.begin(), nearest.end(),
					  [](const Neighbor& a, const Neighbor& b) { return a.distSq < b.distSq; });
		}
	}

	KnnVote vote;
	if (nearestCount == 0) {
		return vote;
	}

	std::array<float, WallClassifier::SEGMENT_COUNT> weights {};
	float totalWeight = 0.0f;
	for (int n = 0; n < nearestCount; ++n) {
		const float w = 1.0f / (nearest[n].distSq + KNN_EPSILON);
		weights[nearest[n].segmentIndex] += w;
		totalWeight += w;
	}

	if (totalWeight <= 0.0f) {
		return vote;
	}

	for (size_t s = 0; s < WallClassifier::SEGMENT_COUNT; ++s) {
		if (weights[s] <= 0.0f) {
			continue;
		}
		const float share = (weights[s] / totalWeight) * 100.0f;
		if (vote.bestSegment < 0 || share > vote.bestConfidence) {
			vote.secondSegment = vote.bestSegment;
			vote.secondConfidence = vote.bestConfidence;
			vote.bestSegment = static_cast<int>(s);
			vote.bestConfidence = share;
		} else if (vote.secondSegment < 0 || share > vote.secondConfidence) {
			vote.secondSegment = static_cast<int>(s);
			vote.secondConfidence = share;
		}
	}

	return vote;
}

} // namespace

WallScanResult WallClassifier::classifyOne(uint16_t itemId) const {
	WallScanResult result;
	result.itemId = itemId;

	std::array<float, FEATURE_COUNT> features;
	WallScanResult::Status failure = WallScanResult::Status::NoSprite;
	if (!extractFeatures(itemId, features, failure)) {
		result.status = failure;
		result.confidence = 0.0f;
		return result;
	}

	const KnnVote vote = runKnnImpl(m_samples, features, itemId);
	if (vote.bestSegment >= 0) {
		result.segment = SEGMENT_NAMES[vote.bestSegment];
		result.confidence = vote.bestConfidence;
		if (vote.secondSegment >= 0) {
			result.secondSegment = SEGMENT_NAMES[vote.secondSegment];
			result.secondConfidence = vote.secondConfidence;
		}
	}
	// If no neighbor voted: segment stays empty, confidence 0, status Classified
	// (the dialog maps an empty segment to Pending).
	result.status = WallScanResult::Status::Classified;

	std::string owningWall;
	std::string owningSegment;
	if (findOwningWall(itemId, owningWall, owningSegment)) {
		result.status = WallScanResult::Status::AlreadyInWall;
		result.existingWallName = owningWall;
		result.existingWallSegment = owningSegment;
	}

	return result;
}

std::vector<WallScanResult> WallClassifier::classify(const std::vector<uint16_t>& candidates) {
	ensureTrained();

	std::vector<WallScanResult> results;
	results.reserve(candidates.size());
	for (uint16_t itemId : candidates) {
		results.push_back(classifyOne(itemId));
	}
	return results;
}

std::string WallClassifier::validateLeaveOneOut() {
	if (!ensureTrained()) {
		return "No training data: no wall brushes are loaded.";
	}

	std::array<int, SEGMENT_COUNT> correct {};
	std::array<int, SEGMENT_COUNT> total {};

	for (const Sample& sample : m_samples) {
		// Skip-self by itemId excludes this sample (and identical-item duplicates).
		const KnnVote vote = runKnnImpl(m_samples, sample.features, sample.itemId);
		++total[sample.segmentIndex];
		if (vote.bestSegment == static_cast<int>(sample.segmentIndex)) {
			++correct[sample.segmentIndex];
		}
	}

	int correctSum = 0;
	int totalSum = 0;
	std::string report = std::format(
		"Leave-one-out validation ({} samples, {} wall brushes)\n\n", m_samples.size(), m_groupCount);
	for (size_t s = 0; s < SEGMENT_COUNT; ++s) {
		correctSum += correct[s];
		totalSum += total[s];
		const float pct = total[s] > 0
			? (static_cast<float>(correct[s]) / static_cast<float>(total[s])) * 100.0f
			: 0.0f;
		report += std::format("{:>18}: {:>4}/{:<4} ({:.1f}%)\n", SEGMENT_NAMES[s], correct[s], total[s], pct);
	}
	const float overall = totalSum > 0
		? (static_cast<float>(correctSum) / static_cast<float>(totalSum)) * 100.0f
		: 0.0f;
	report += std::format("\nOverall: {}/{} ({:.1f}%)", correctSum, totalSum, overall);
	return report;
}

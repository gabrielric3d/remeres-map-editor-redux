//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Border Scan classifier - shape-based kNN edge classifier.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "brushes/ground/border_classifier.h"
#include "brushes/brush.h"
#include "brushes/ground/auto_border.h"
#include "item_definitions/core/item_definition_store.h"
#include "ui/gui.h"
#include "rendering/core/game_sprite.h"
#include "rendering/core/normal_image.h"

#include <algorithm>
#include <cmath>
#include <format>

const std::array<std::string, BorderClassifier::EDGE_COUNT> BorderClassifier::EDGE_NAMES = {
	"n", "e", "s", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dse", "dsw"
};

namespace {

constexpr int SPRITE_DIM = 32;
constexpr int SPRITE_PIXEL_COUNT = SPRITE_DIM * SPRITE_DIM;
constexpr uint8_t ALPHA_THRESHOLD = 20;
constexpr int KNN_K = 5;
constexpr float KNN_EPSILON = 1e-4f;

// Composites all layers of a 1x1 GameSprite into a 32x32 straight-alpha RGBA
// buffer over a FULLY TRANSPARENT background (alpha = 0), frame 0, pattern 0,0,0.
//
// Deliberately NOT NvgUtils::CreateCompositeRGBA: that helper flattens the
// sprite onto an opaque ICON_BACKGROUND fill (alpha = 255) and picks a
// non-zero pattern_x for some sprites - both destroy the alpha silhouette
// this classifier depends on.
bool getItemRGBA32(uint16_t itemId, std::array<uint8_t, SPRITE_PIXEL_COUNT * 4>& out,
				   BorderScanResult::Status& failure) {
	const auto def = g_item_definitions.get(itemId);
	if (!def) {
		failure = BorderScanResult::Status::NoSprite;
		return false;
	}

	GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(def.clientId()));
	if (!gs) {
		failure = BorderScanResult::Status::NoSprite;
		return false;
	}

	if (gs->width > 1 || gs->height > 1) {
		failure = BorderScanResult::Status::MultiTile;
		return false;
	}

	out.fill(0);

	for (int layer = 0; layer < gs->layers; ++layer) {
		size_t idx = gs->getIndex(0, 0, layer, 0, 0, 0, 0);
		if (idx >= gs->spriteList.size()) {
			continue;
		}

		auto data = gs->spriteList[idx]->getRGBAData();
		if (!data) {
			continue;
		}

		// Straight-alpha src-over blend, preserving alpha.
		for (int p = 0; p < SPRITE_PIXEL_COUNT; ++p) {
			const int o = p * 4;
			const uint8_t srcA = data[o + 3];
			if (srcA == 0) {
				continue;
			}
			if (srcA == 255) {
				out[o + 0] = data[o + 0];
				out[o + 1] = data[o + 1];
				out[o + 2] = data[o + 2];
				out[o + 3] = 255;
			} else {
				const float a = srcA / 255.0f;
				const float inv = 1.0f - a;
				out[o + 0] = static_cast<uint8_t>(data[o + 0] * a + out[o + 0] * inv);
				out[o + 1] = static_cast<uint8_t>(data[o + 1] * a + out[o + 1] * inv);
				out[o + 2] = static_cast<uint8_t>(data[o + 2] * a + out[o + 2] * inv);
				out[o + 3] = static_cast<uint8_t>(srcA + out[o + 3] * inv);
			}
		}
	}

	return true;
}

// Result of a kNN vote: winner + runner-up edge indices and their vote shares.
struct KnnVote {
	int bestEdge = -1;
	float bestConfidence = 0.0f; // 0..100
	int secondEdge = -1;
	float secondConfidence = 0.0f; // 0..100
};

} // namespace

BorderClassifier& BorderClassifier::Get() {
	static BorderClassifier instance;
	return instance;
}

size_t BorderClassifier::sampleCount() const {
	return m_samples.size();
}

size_t BorderClassifier::groupCount() const {
	return m_groupCount;
}

bool BorderClassifier::extractFeatures(uint16_t itemId, std::array<float, FEATURE_COUNT>& out,
									   BorderScanResult::Status& failure) {
	std::array<uint8_t, SPRITE_PIXEL_COUNT * 4> rgba;
	if (!getItemRGBA32(itemId, rgba, failure)) {
		return false;
	}

	// Single pass: per-cell opaque counts (8x8 grid of 4x4 cells), centroid sums,
	// quadrant counts and global count.
	std::array<int, 64> cellCounts {};
	std::array<int, 4> quadCounts {}; // NW, NE, SW, SE
	int opaqueCount = 0;
	float sumX = 0.0f;
	float sumY = 0.0f;

	for (int py = 0; py < SPRITE_DIM; ++py) {
		for (int px = 0; px < SPRITE_DIM; ++px) {
			const uint8_t alpha = rgba[(py * SPRITE_DIM + px) * 4 + 3];
			if (alpha <= ALPHA_THRESHOLD) {
				continue;
			}
			++opaqueCount;
			sumX += static_cast<float>(px);
			sumY += static_cast<float>(py);

			const int gx = px / 4;
			const int gy = py / 4;
			++cellCounts[gy * 8 + gx];

			const int quad = (py < 16 ? 0 : 2) + (px < 16 ? 0 : 1); // 0=NW 1=NE 2=SW 3=SE
			++quadCounts[quad];
		}
	}

	if (opaqueCount == 0) {
		failure = BorderScanResult::Status::NoSprite;
		return false;
	}

	// [0..63] 8x8 alpha-occupancy grid (fraction of opaque pixels per 4x4 cell, row-major).
	for (int c = 0; c < 64; ++c) {
		out[c] = static_cast<float>(cellCounts[c]) / 16.0f;
	}

	// [64..65] opaque-pixel centroid normalized to [0,1], stored x2.0 (extra weight).
	const float cx = (sumX / static_cast<float>(opaqueCount)) / static_cast<float>(SPRITE_DIM - 1);
	const float cy = (sumY / static_cast<float>(opaqueCount)) / static_cast<float>(SPRITE_DIM - 1);
	out[64] = cx * 2.0f;
	out[65] = cy * 2.0f;

	// [66] global density.
	out[66] = static_cast<float>(opaqueCount) / static_cast<float>(SPRITE_PIXEL_COUNT);

	// [67..70] quadrant densities (NW, NE, SW, SE), each quadrant = 16x16 pixels.
	for (int q = 0; q < 4; ++q) {
		out[67 + q] = static_cast<float>(quadCounts[q]) / 256.0f;
	}

	return true;
}

bool BorderClassifier::ensureTrained() {
	const auto& borders = g_brushes.getBorders();
	if (m_trained && m_trainedBorderMapSize == borders.size()) {
		return !m_samples.empty();
	}

	m_samples.clear();
	m_groupCount = 0;

	for (const auto& [borderId, border] : borders) {
		if (!border) {
			continue;
		}
		bool groupContributed = false;
		// Slot 0 = BORDER_NONE (unused); edges live in slots 1..12.
		for (size_t slot = 1; slot <= EDGE_COUNT; ++slot) {
			// Every variant is a training sample, not just the first.
			for (const BorderItemChance& variant : border->tiles[slot]) {
				if (variant.id == 0) {
					continue;
				}
				Sample sample;
				BorderScanResult::Status failure;
				if (!extractFeatures(variant.id, sample.features, failure)) {
					continue;
				}
				sample.edgeIndex = static_cast<uint8_t>(slot - 1);
				sample.itemId = variant.id;
				sample.borderId = border->id;
				m_samples.push_back(sample);
				groupContributed = true;
			}
		}
		if (groupContributed) {
			++m_groupCount;
		}
	}

	m_trainedBorderMapSize = borders.size();
	m_trained = true;
	return !m_samples.empty();
}

namespace {

struct Neighbor {
	float distSq = 0.0f;
	uint8_t edgeIndex = 0;
};

// Brute-force kNN over the sample set. Samples whose itemId equals skipItemId
// never vote (skip-self rule). Vote weight = 1 / (d^2 + epsilon); confidence is
// the winning edge's share of the total vote weight, as a percentage.
// Template on the container so the private BorderClassifier::Sample type is
// never named here (members are reached through deduced `auto`).
template <typename SampleVec>
KnnVote runKnnImpl(const SampleVec& samples,
				   const std::array<float, BorderClassifier::FEATURE_COUNT>& query,
				   uint16_t skipItemId) {
	// Collect the K nearest neighbors (smallest squared L2 distance).
	std::array<Neighbor, KNN_K> nearest;
	int nearestCount = 0;

	for (const auto& sample : samples) {
		if (sample.itemId == skipItemId) {
			continue; // skip-self rule
		}

		float distSq = 0.0f;
		for (size_t f = 0; f < BorderClassifier::FEATURE_COUNT; ++f) {
			const float d = query[f] - sample.features[f];
			distSq += d * d;
		}

		if (nearestCount < KNN_K) {
			nearest[nearestCount++] = Neighbor { distSq, sample.edgeIndex };
			// Keep the worst neighbor at the back via simple sort (K is tiny).
			std::sort(nearest.begin(), nearest.begin() + nearestCount,
					  [](const Neighbor& a, const Neighbor& b) { return a.distSq < b.distSq; });
		} else if (distSq < nearest[KNN_K - 1].distSq) {
			nearest[KNN_K - 1] = Neighbor { distSq, sample.edgeIndex };
			std::sort(nearest.begin(), nearest.end(),
					  [](const Neighbor& a, const Neighbor& b) { return a.distSq < b.distSq; });
		}
	}

	KnnVote vote;
	if (nearestCount == 0) {
		return vote;
	}

	std::array<float, BorderClassifier::EDGE_COUNT> weights {};
	float totalWeight = 0.0f;
	for (int n = 0; n < nearestCount; ++n) {
		const float w = 1.0f / (nearest[n].distSq + KNN_EPSILON);
		weights[nearest[n].edgeIndex] += w;
		totalWeight += w;
	}

	if (totalWeight <= 0.0f) {
		return vote;
	}

	for (size_t e = 0; e < BorderClassifier::EDGE_COUNT; ++e) {
		if (weights[e] <= 0.0f) {
			continue;
		}
		const float share = (weights[e] / totalWeight) * 100.0f;
		if (vote.bestEdge < 0 || share > vote.bestConfidence) {
			vote.secondEdge = vote.bestEdge;
			vote.secondConfidence = vote.bestConfidence;
			vote.bestEdge = static_cast<int>(e);
			vote.bestConfidence = share;
		} else if (vote.secondEdge < 0 || share > vote.secondConfidence) {
			vote.secondEdge = static_cast<int>(e);
			vote.secondConfidence = share;
		}
	}

	return vote;
}

} // namespace

BorderScanResult BorderClassifier::classifyOne(uint16_t itemId) const {
	BorderScanResult result;
	result.itemId = itemId;

	std::array<float, FEATURE_COUNT> features;
	BorderScanResult::Status failure = BorderScanResult::Status::NoSprite;
	if (!extractFeatures(itemId, features, failure)) {
		result.status = failure;
		result.confidence = 0.0f;
		return result;
	}

	const KnnVote vote = runKnnImpl(m_samples, features, itemId);
	if (vote.bestEdge >= 0) {
		result.edge = EDGE_NAMES[vote.bestEdge];
		result.confidence = vote.bestConfidence;
		if (vote.secondEdge >= 0) {
			result.secondEdge = EDGE_NAMES[vote.secondEdge];
			result.secondConfidence = vote.secondConfidence;
		}
	}
	// If no neighbor voted: edge stays empty, confidence 0, status Classified
	// (the dialog maps an empty edge to Pending).
	result.status = BorderScanResult::Status::Classified;

	const auto matches = g_brushes.findAutoBordersByBorderItem(itemId);
	if (!matches.empty()) {
		result.status = BorderScanResult::Status::AlreadyInBorder;
		result.existingBorderId = matches.front()->id; // sorted ascending by border id
	}

	return result;
}

std::vector<BorderScanResult> BorderClassifier::classify(const std::vector<uint16_t>& candidates) {
	ensureTrained();

	std::vector<BorderScanResult> results;
	results.reserve(candidates.size());
	for (uint16_t itemId : candidates) {
		results.push_back(classifyOne(itemId));
	}
	return results;
}

std::string BorderClassifier::validateLeaveOneOut() {
	if (!ensureTrained()) {
		return "No training data: no borders are loaded.";
	}

	std::array<int, EDGE_COUNT> correct {};
	std::array<int, EDGE_COUNT> total {};

	for (const Sample& sample : m_samples) {
		// Skip-self by itemId excludes this sample (and identical-item duplicates).
		const KnnVote vote = runKnnImpl(m_samples, sample.features, sample.itemId);
		++total[sample.edgeIndex];
		if (vote.bestEdge == static_cast<int>(sample.edgeIndex)) {
			++correct[sample.edgeIndex];
		}
	}

	int correctSum = 0;
	int totalSum = 0;
	std::string report = std::format(
		"Leave-one-out validation ({} samples, {} border groups)\n\n", m_samples.size(), m_groupCount);
	for (size_t e = 0; e < EDGE_COUNT; ++e) {
		correctSum += correct[e];
		totalSum += total[e];
		const float pct = total[e] > 0
			? (static_cast<float>(correct[e]) / static_cast<float>(total[e])) * 100.0f
			: 0.0f;
		report += std::format("{:>4}: {:>4}/{:<4} ({:.1f}%)\n", EDGE_NAMES[e], correct[e], total[e], pct);
	}
	const float overall = totalSum > 0
		? (static_cast<float>(correctSum) / static_cast<float>(totalSum)) * 100.0f
		: 0.0f;
	report += std::format("\nOverall: {}/{} ({:.1f}%)", correctSum, totalSum, overall);
	return report;
}

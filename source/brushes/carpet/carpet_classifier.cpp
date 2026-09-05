//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Carpet Scan classifier - kNN carpet-alignment classifier on batch-relative
// colour/alpha deviation features. See carpet_classifier.h for the idea.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "brushes/carpet/carpet_classifier.h"
#include "brushes/brush.h"
#include "brushes/brush_enums.h"
#include "brushes/carpet/carpet_brush.h"
#include "item_definitions/core/item_definition_store.h"
#include "ui/gui.h"
#include "rendering/core/game_sprite.h"
#include "rendering/core/normal_image.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>

const std::array<std::string, CarpetClassifier::ALIGN_COUNT> CarpetClassifier::ALIGN_NAMES = {
	"n", "e", "s", "w",
	"cnw", "cne", "cse", "csw",
	"dnw", "dne", "dse", "dsw",
	"center"
};

namespace {

constexpr int TILE_DIM = 32;
constexpr int GRID_DIM = 8;                      // 8x8 cells...
constexpr int GRID_CELL = TILE_DIM / GRID_DIM;   // ...of 4x4 pixels
constexpr int GRID_CELL_PIXELS = GRID_CELL * GRID_CELL;
constexpr int GRID_CELLS = GRID_DIM * GRID_DIM;
constexpr uint8_t ALPHA_THRESHOLD = 20;
constexpr int KNN_K = 5;
constexpr float KNN_EPSILON = 1e-4f;

// Feature layout.
constexpr size_t F_COLOUR = 0;    // [0..63]   relative colour deviation per cell
constexpr size_t F_ALPHA = 64;    // [64..127] relative alpha deviation per cell (signed)
constexpr size_t F_SUMS = 128;    // [128..135] side + corner sums of the colour deviation
constexpr float COLOUR_WEIGHT = 2.0f;
constexpr float ALPHA_WEIGHT = 1.0f;
constexpr float SUMS_WEIGHT = 3.0f;

// The engine slot backing each ALIGN_NAMES entry.
constexpr BorderType ALIGN_SLOTS[CarpetClassifier::ALIGN_COUNT] = {
	NORTH_HORIZONTAL, EAST_HORIZONTAL, SOUTH_HORIZONTAL, WEST_HORIZONTAL,
	NORTHWEST_CORNER, NORTHEAST_CORNER, SOUTHEAST_CORNER, SOUTHWEST_CORNER,
	NORTHWEST_DIAGONAL, NORTHEAST_DIAGONAL, SOUTHEAST_DIAGONAL, SOUTHWEST_DIAGONAL,
	CARPET_CENTER
};

int alignIndexOfSlot(int slot) {
	for (size_t i = 0; i < CarpetClassifier::ALIGN_COUNT; ++i) {
		if (ALIGN_SLOTS[i] == slot) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// Straight-alpha RGBA of a 1x1 sprite (frame 0, pattern 0,0,0), all layers composited
// over a fully transparent background so sparse carpets keep their alpha.
bool getItemRGBA32(uint16_t itemId, std::array<uint8_t, TILE_DIM * TILE_DIM * 4>& out,
				   CarpetScanResult::Status& failure) {
	const auto def = g_item_definitions.get(itemId);
	if (!def) {
		failure = CarpetScanResult::Status::NoSprite;
		return false;
	}

	GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(def.clientId()));
	if (!gs) {
		failure = CarpetScanResult::Status::NoSprite;
		return false;
	}

	if (gs->width != 1 || gs->height != 1) {
		failure = CarpetScanResult::Status::TooLarge;
		return false;
	}

	out.fill(0);

	for (int layer = 0; layer < gs->layers; ++layer) {
		const size_t idx = gs->getIndex(0, 0, layer, 0, 0, 0, 0);
		if (idx >= gs->spriteList.size()) {
			continue;
		}
		auto data = gs->spriteList[idx]->getRGBAData();
		if (!data) {
			continue;
		}

		for (int p = 0; p < TILE_DIM * TILE_DIM; ++p) {
			const int src = p * 4;
			const uint8_t srcA = data[src + 3];
			if (srcA == 0) {
				continue;
			}
			if (srcA == 255) {
				out[src + 0] = data[src + 0];
				out[src + 1] = data[src + 1];
				out[src + 2] = data[src + 2];
				out[src + 3] = 255;
			} else {
				const float a = srcA / 255.0f;
				const float inv = 1.0f - a;
				out[src + 0] = static_cast<uint8_t>(data[src + 0] * a + out[src + 0] * inv);
				out[src + 1] = static_cast<uint8_t>(data[src + 1] * a + out[src + 1] * inv);
				out[src + 2] = static_cast<uint8_t>(data[src + 2] * a + out[src + 2] * inv);
				out[src + 3] = static_cast<uint8_t>(srcA + out[src + 3] * inv);
			}
		}
	}

	return true;
}

// Result of a kNN vote: winner + runner-up slot indices and their vote shares.
struct KnnVote {
	int bestAlign = -1;
	float bestConfidence = 0.0f; // 0..100
	int secondAlign = -1;
	float secondConfidence = 0.0f; // 0..100
};

struct Neighbor {
	float distSq = 0.0f;
	uint8_t alignIndex = 0;
};

// Brute-force kNN over the sample set. Samples whose itemId equals skipItemId never vote
// (skip-self rule). Vote weight = 1 / (d^2 + epsilon); confidence is the winning slot's
// share of the total vote weight, as a percentage.
template <typename SampleVec>
KnnVote runKnnImpl(const SampleVec& samples,
				   const std::array<float, CarpetClassifier::FEATURE_COUNT>& query,
				   uint16_t skipItemId) {
	std::array<Neighbor, KNN_K> nearest;
	int nearestCount = 0;

	for (const auto& sample : samples) {
		if (sample.itemId == skipItemId) {
			continue;
		}

		float distSq = 0.0f;
		for (size_t f = 0; f < CarpetClassifier::FEATURE_COUNT; ++f) {
			const float d = query[f] - sample.features[f];
			distSq += d * d;
		}

		if (nearestCount < KNN_K) {
			nearest[nearestCount++] = Neighbor { distSq, sample.alignIndex };
			std::sort(nearest.begin(), nearest.begin() + nearestCount,
					  [](const Neighbor& a, const Neighbor& b) { return a.distSq < b.distSq; });
		} else if (distSq < nearest[KNN_K - 1].distSq) {
			nearest[KNN_K - 1] = Neighbor { distSq, sample.alignIndex };
			std::sort(nearest.begin(), nearest.end(),
					  [](const Neighbor& a, const Neighbor& b) { return a.distSq < b.distSq; });
		}
	}

	KnnVote vote;
	if (nearestCount == 0) {
		return vote;
	}

	std::array<float, CarpetClassifier::ALIGN_COUNT> weights {};
	float totalWeight = 0.0f;
	for (int n = 0; n < nearestCount; ++n) {
		const float w = 1.0f / (nearest[n].distSq + KNN_EPSILON);
		weights[nearest[n].alignIndex] += w;
		totalWeight += w;
	}
	if (totalWeight <= 0.0f) {
		return vote;
	}

	for (size_t s = 0; s < CarpetClassifier::ALIGN_COUNT; ++s) {
		if (weights[s] <= 0.0f) {
			continue;
		}
		const float share = (weights[s] / totalWeight) * 100.0f;
		if (vote.bestAlign < 0 || share > vote.bestConfidence) {
			vote.secondAlign = vote.bestAlign;
			vote.secondConfidence = vote.bestConfidence;
			vote.bestAlign = static_cast<int>(s);
			vote.bestConfidence = share;
		} else if (vote.secondAlign < 0 || share > vote.secondConfidence) {
			vote.secondAlign = static_cast<int>(s);
			vote.secondConfidence = share;
		}
	}
	return vote;
}

float medianOf(std::vector<float>& values) {
	if (values.empty()) {
		return 0.0f;
	}
	const size_t mid = values.size() / 2;
	std::nth_element(values.begin(), values.begin() + mid, values.end());
	return values[mid];
}

} // namespace

CarpetClassifier& CarpetClassifier::Get() {
	static CarpetClassifier instance;
	return instance;
}

size_t CarpetClassifier::sampleCount() const {
	return m_samples.size();
}

size_t CarpetClassifier::groupCount() const {
	return m_groupCount;
}

bool CarpetClassifier::measureTile(uint16_t itemId, TileStats& out, CarpetScanResult::Status& failure) {
	std::array<uint8_t, TILE_DIM * TILE_DIM * 4> rgba;
	if (!getItemRGBA32(itemId, rgba, failure)) {
		return false;
	}

	std::array<int, GRID_CELLS> counts {};
	std::array<float, GRID_CELLS * 3> sums {};
	int opaqueTotal = 0;

	for (int py = 0; py < TILE_DIM; ++py) {
		for (int px = 0; px < TILE_DIM; ++px) {
			const int p = (py * TILE_DIM + px) * 4;
			if (rgba[p + 3] <= ALPHA_THRESHOLD) {
				continue;
			}
			const int cell = (py / GRID_CELL) * GRID_DIM + (px / GRID_CELL);
			++counts[cell];
			++opaqueTotal;
			sums[cell * 3 + 0] += rgba[p + 0] / 255.0f;
			sums[cell * 3 + 1] += rgba[p + 1] / 255.0f;
			sums[cell * 3 + 2] += rgba[p + 2] / 255.0f;
		}
	}

	if (opaqueTotal == 0) {
		failure = CarpetScanResult::Status::NoSprite;
		return false;
	}

	for (int c = 0; c < GRID_CELLS; ++c) {
		out.alpha[c] = static_cast<float>(counts[c]) / static_cast<float>(GRID_CELL_PIXELS);
		out.hasPixels[c] = counts[c] > 0;
		for (int k = 0; k < 3; ++k) {
			out.rgb[c * 3 + k] = counts[c] > 0 ? sums[c * 3 + k] / static_cast<float>(counts[c]) : 0.0f;
		}
	}
	return true;
}

void CarpetClassifier::buildFeatures(const std::vector<TileStats>& batch,
									 std::vector<std::array<float, FEATURE_COUNT>>& out) {
	out.assign(batch.size(), {});
	if (batch.empty()) {
		return;
	}

	// Batch median per cell: colour (of the cells that have pixels) and alpha. With
	// 13 slots and at most 4 of them touching any given cell with border texture, the
	// median is the plain carpet texture - the reference every piece is compared to.
	std::array<float, GRID_CELLS * 3> medianRgb {};
	std::array<float, GRID_CELLS> medianAlpha {};
	std::vector<float> values;
	values.reserve(batch.size());
	for (int c = 0; c < GRID_CELLS; ++c) {
		for (int k = 0; k < 3; ++k) {
			values.clear();
			for (const TileStats& tile : batch) {
				if (tile.hasPixels[c]) {
					values.push_back(tile.rgb[c * 3 + k]);
				}
			}
			medianRgb[c * 3 + k] = medianOf(values);
		}
		values.clear();
		for (const TileStats& tile : batch) {
			values.push_back(tile.alpha[c]);
		}
		medianAlpha[c] = medianOf(values);
	}

	// Raw colour deviation of every cell of every tile, and the batch-wide scale that
	// normalizes it: the mean of the top 10% deviations. Normalizing per batch (not per
	// tile) keeps a center piece near zero everywhere instead of amplifying its noise.
	std::vector<std::array<float, GRID_CELLS>> deviation(batch.size());
	std::vector<float> all;
	all.reserve(batch.size() * GRID_CELLS);
	for (size_t i = 0; i < batch.size(); ++i) {
		for (int c = 0; c < GRID_CELLS; ++c) {
			float d = 0.0f;
			if (batch[i].hasPixels[c]) {
				for (int k = 0; k < 3; ++k) {
					const float diff = batch[i].rgb[c * 3 + k] - medianRgb[c * 3 + k];
					d += diff * diff;
				}
				d = std::sqrt(d);
			}
			deviation[i][c] = d;
			all.push_back(d);
		}
	}
	std::sort(all.begin(), all.end(), std::greater<float>());
	const size_t topCount = std::max<size_t>(1, all.size() / 10);
	float scale = 0.0f;
	for (size_t i = 0; i < topCount; ++i) {
		scale += all[i];
	}
	scale /= static_cast<float>(topCount);
	if (scale < 0.02f) {
		scale = 0.02f; // a batch of near-identical tiles: keep the features tiny, not NaN
	}

	for (size_t i = 0; i < batch.size(); ++i) {
		std::array<float, FEATURE_COUNT>& f = out[i];
		for (int c = 0; c < GRID_CELLS; ++c) {
			f[F_COLOUR + c] = std::min(1.5f, deviation[i][c] / scale) * COLOUR_WEIGHT;
			f[F_ALPHA + c] = (batch[i].alpha[c] - medianAlpha[c]) * ALPHA_WEIGHT;
		}

		// Side sums (outer two rows/columns) and corner sums (2x2 blocks) of the
		// normalized colour deviation, so "top band" and "top-left notch" are explicit
		// coordinates instead of something kNN has to infer from 64 cells.
		float top = 0, bottom = 0, left = 0, right = 0;
		float nw = 0, ne = 0, sw = 0, se = 0;
		for (int y = 0; y < GRID_DIM; ++y) {
			for (int x = 0; x < GRID_DIM; ++x) {
				const float d = std::min(1.5f, deviation[i][y * GRID_DIM + x] / scale);
				if (y < 2) top += d;
				if (y >= GRID_DIM - 2) bottom += d;
				if (x < 2) left += d;
				if (x >= GRID_DIM - 2) right += d;
				if (y < 2 && x < 2) nw += d;
				if (y < 2 && x >= GRID_DIM - 2) ne += d;
				if (y >= GRID_DIM - 2 && x < 2) sw += d;
				if (y >= GRID_DIM - 2 && x >= GRID_DIM - 2) se += d;
			}
		}
		const float sideCells = 2.0f * GRID_DIM;
		const float cornerCells = 4.0f;
		f[F_SUMS + 0] = (top / sideCells) * SUMS_WEIGHT;
		f[F_SUMS + 1] = (bottom / sideCells) * SUMS_WEIGHT;
		f[F_SUMS + 2] = (left / sideCells) * SUMS_WEIGHT;
		f[F_SUMS + 3] = (right / sideCells) * SUMS_WEIGHT;
		f[F_SUMS + 4] = (nw / cornerCells) * SUMS_WEIGHT;
		f[F_SUMS + 5] = (ne / cornerCells) * SUMS_WEIGHT;
		f[F_SUMS + 6] = (sw / cornerCells) * SUMS_WEIGHT;
		f[F_SUMS + 7] = (se / cornerCells) * SUMS_WEIGHT;
	}
}

bool CarpetClassifier::findOwningCarpet(uint16_t itemId, std::string& outBrushName, std::string& outAlign) {
	for (const auto& [name, brush] : g_brushes.getMap()) {
		if (!brush) {
			continue;
		}
		const CarpetBrush* carpet = brush->as<CarpetBrush>();
		if (!carpet) {
			continue;
		}
		const auto& groups = carpet->getItems().getGroups();
		for (size_t slot = 1; slot < groups.size(); ++slot) {
			for (const auto& entry : groups[slot].items) {
				if (entry.id != itemId) {
					continue;
				}
				outBrushName = brush->getName();
				const int alignIndex = alignIndexOfSlot(static_cast<int>(slot));
				outAlign = alignIndex >= 0 ? ALIGN_NAMES[alignIndex] : std::string();
				return true;
			}
		}
	}
	return false;
}

bool CarpetClassifier::ensureTrained() {
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
		const CarpetBrush* carpet = brush->as<CarpetBrush>();
		if (!carpet) {
			continue;
		}

		// Every item of every slot is a training sample; the brush is its batch.
		std::vector<TileStats> batch;
		std::vector<uint8_t> batchAligns;
		std::vector<uint16_t> batchIds;
		const auto& groups = carpet->getItems().getGroups();
		for (size_t slot = 1; slot < groups.size(); ++slot) {
			const int alignIndex = alignIndexOfSlot(static_cast<int>(slot));
			if (alignIndex < 0) {
				continue;
			}
			for (const auto& entry : groups[slot].items) {
				if (entry.id == 0) {
					continue;
				}
				TileStats stats;
				CarpetScanResult::Status failure;
				if (!measureTile(entry.id, stats, failure)) {
					continue;
				}
				batch.push_back(stats);
				batchAligns.push_back(static_cast<uint8_t>(alignIndex));
				batchIds.push_back(entry.id);
			}
		}

		// A brush with fewer pieces has no meaningful median to deviate from.
		if (batch.size() < 4) {
			continue;
		}

		std::vector<std::array<float, FEATURE_COUNT>> features;
		buildFeatures(batch, features);
		for (size_t i = 0; i < batch.size(); ++i) {
			Sample sample;
			sample.features = features[i];
			sample.alignIndex = batchAligns[i];
			sample.itemId = batchIds[i];
			m_samples.push_back(sample);
		}
		++m_groupCount;
	}

	m_trainedBrushMapSize = brushes.size();
	m_trained = true;
	return !m_samples.empty();
}

std::vector<CarpetScanResult> CarpetClassifier::classify(const std::vector<uint16_t>& candidates) {
	ensureTrained();

	std::vector<CarpetScanResult> results(candidates.size());
	for (size_t i = 0; i < candidates.size(); ++i) {
		results[i].itemId = candidates[i];
	}

	// Measure everything first: the batch median needs all the usable candidates.
	std::vector<TileStats> batch;
	std::vector<size_t> batchIndex; // candidate index of each batch entry
	for (size_t i = 0; i < candidates.size(); ++i) {
		TileStats stats;
		CarpetScanResult::Status failure = CarpetScanResult::Status::NoSprite;
		if (!measureTile(candidates[i], stats, failure)) {
			results[i].status = failure;
			continue;
		}
		batch.push_back(stats);
		batchIndex.push_back(i);
	}

	std::vector<std::array<float, FEATURE_COUNT>> features;
	buildFeatures(batch, features);

	for (size_t b = 0; b < batch.size(); ++b) {
		CarpetScanResult& result = results[batchIndex[b]];

		const KnnVote vote = runKnnImpl(m_samples, features[b], result.itemId);
		if (vote.bestAlign >= 0) {
			result.align = ALIGN_NAMES[vote.bestAlign];
			result.confidence = vote.bestConfidence;
			if (vote.secondAlign >= 0) {
				result.secondAlign = ALIGN_NAMES[vote.secondAlign];
				result.secondConfidence = vote.secondConfidence;
			}
		}
		// No neighbour voted: align stays empty, confidence 0, status Classified (the
		// dialog maps an empty align to Pending).
		result.status = CarpetScanResult::Status::Classified;

		std::string owningCarpet;
		std::string owningAlign;
		if (findOwningCarpet(result.itemId, owningCarpet, owningAlign)) {
			result.status = CarpetScanResult::Status::AlreadyInCarpet;
			result.existingCarpetName = owningCarpet;
			result.existingCarpetAlign = owningAlign;
		}
	}

	return results;
}

std::string CarpetClassifier::validateLeaveOneOut() {
	if (!ensureTrained()) {
		return "No training data: no carpet brushes are loaded.";
	}

	std::array<int, ALIGN_COUNT> correct {};
	std::array<int, ALIGN_COUNT> total {};

	for (const Sample& sample : m_samples) {
		const KnnVote vote = runKnnImpl(m_samples, sample.features, sample.itemId);
		++total[sample.alignIndex];
		if (vote.bestAlign == static_cast<int>(sample.alignIndex)) {
			++correct[sample.alignIndex];
		}
	}

	int correctSum = 0;
	int totalSum = 0;
	std::string report = std::format(
		"Leave-one-out validation ({} samples, {} carpet brushes)\n\n", m_samples.size(), m_groupCount);
	for (size_t s = 0; s < ALIGN_COUNT; ++s) {
		correctSum += correct[s];
		totalSum += total[s];
		const float pct = total[s] > 0
			? (static_cast<float>(correct[s]) / static_cast<float>(total[s])) * 100.0f
			: 0.0f;
		report += std::format("{:>8}: {:>4}/{:<4} ({:.1f}%)\n", ALIGN_NAMES[s], correct[s], total[s], pct);
	}
	const float overall = totalSum > 0
		? (static_cast<float>(correctSum) / static_cast<float>(totalSum)) * 100.0f
		: 0.0f;
	report += std::format("\nOverall: {}/{} ({:.1f}%)", correctSum, totalSum, overall);
	return report;
}

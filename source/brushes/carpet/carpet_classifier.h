//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Carpet Scan classifier - kNN carpet-alignment classifier.
//
// Carpet pieces are (mostly) opaque tiles, so the alpha silhouette the wall and
// border classifiers rely on says nothing about them. What does tell an edge from
// a corner from the center is how a piece DIFFERS from the rest of its family:
// the north edge has a band of "border texture" along its top, the northwest
// corner along its top and left, the inner-corner diagonals a small notch, the
// center nothing at all. So every sample is described relative to the median
// tile of its batch - the carpet brush it belongs to when training, the set of
// candidates being scanned when classifying (which is why one family should be
// scanned at a time).
//////////////////////////////////////////////////////////////////////

#ifndef RME_CARPET_CLASSIFIER_H
#define RME_CARPET_CLASSIFIER_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct CarpetScanResult {
	enum class Status { Classified, TooLarge, NoSprite, AlreadyInCarpet };
	uint16_t itemId = 0;
	std::string align;               // canonical "n".."dsw","center"; empty when not classified
	float confidence = 0.0f;         // 0..100
	std::string secondAlign;         // runner-up, for tooltips
	float secondConfidence = 0.0f;
	Status status = Status::NoSprite;
	std::string existingCarpetName;  // carpet brush already using the item, when AlreadyInCarpet
	std::string existingCarpetAlign; // slot it is used as, when known
};

class CarpetClassifier {
public:
	// 8x8 relative colour deviation + 8x8 relative alpha deviation + 8 side/corner sums.
	static constexpr size_t FEATURE_COUNT = 64 + 64 + 8;
	static constexpr size_t ALIGN_COUNT = 13;
	// Canonical slot codes, the same strings doodads.xml uses in <carpet align="...">.
	// The dialog converts them back with carpetAlignFromString() - never by index cast.
	static const std::array<std::string, ALIGN_COUNT> ALIGN_NAMES;

	static CarpetClassifier& Get(); // session singleton

	bool ensureTrained();         // lazy; true when sampleCount() > 0
	size_t sampleCount() const;
	size_t groupCount() const;    // number of carpet brushes that contributed samples

	// Classifies the candidates as ONE batch: the relative features of every
	// candidate are measured against the median of all candidates with a sprite.
	std::vector<CarpetScanResult> classify(const std::vector<uint16_t>& candidates);

	std::string validateLeaveOneOut(); // debug: per-slot accuracy report

	// Raw per-tile measurements (8x8 grid of mean RGB and alpha occupancy).
	struct TileStats {
		std::array<float, 64 * 3> rgb;   // mean colour of the opaque pixels of each cell, 0..1
		std::array<float, 64> alpha;     // opaque-pixel fraction of each cell, 0..1
		std::array<bool, 64> hasPixels;  // false when the cell is fully transparent
	};

private:
	struct Sample {
		std::array<float, FEATURE_COUNT> features;
		uint8_t alignIndex; // index into ALIGN_NAMES
		uint16_t itemId;
	};

	static bool measureTile(uint16_t itemId, TileStats& out, CarpetScanResult::Status& failure);
	// Builds the feature vectors of a batch of tiles (all relative to the batch median).
	static void buildFeatures(const std::vector<TileStats>& batch,
							  std::vector<std::array<float, FEATURE_COUNT>>& out);
	// Carpet brush (if any) that already lists this item, in any slot.
	static bool findOwningCarpet(uint16_t itemId, std::string& outBrushName, std::string& outAlign);

	std::vector<Sample> m_samples;
	size_t m_groupCount = 0;
	size_t m_trainedBrushMapSize = 0; // retrain trigger on version switch
	bool m_trained = false;
};

#endif // RME_CARPET_CLASSIFIER_H

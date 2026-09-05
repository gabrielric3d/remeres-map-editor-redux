//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Wall Scan classifier - shape-based kNN wall-segment classifier.
//
// Predicts which of the 16 authored wall segments ("horizontal",
// "vertical", the four corners, the four T pieces, "intersection", the
// four ends and "pole") an item sprite belongs to, by comparing its
// alpha silhouette against every wall item already registered in
// g_brushes (the training set).
//
// Design notes:
// - Segment identities travel as canonical name strings ONLY, matching
//   the <wall type="..."> strings in walls.xml. The classifier never
//   sees WallSegmentType (the UI enum): the dialog converts once, via
//   wallSegmentFromString().
// - Wall sprites are frequently taller than one tile (32x64) and a few
//   are 2 tiles wide, so the silhouette is composited into a 64x64
//   canvas anchored bottom-right (the tile the item sits on), exactly
//   how the map renders it. A 1x1 sprite therefore lands in the
//   bottom-right quadrant and a 1x2 wall fills the right column - that
//   placement is itself a strong horizontal/vertical cue.
// - AlreadyInWall results still carry segment/confidence; the flag is
//   informative (the UI excludes such rows from auto-assignment unless
//   the user overrides them).
// - This class is UI-free by design (no wx includes).
//////////////////////////////////////////////////////////////////////

#ifndef RME_WALL_CLASSIFIER_H
#define RME_WALL_CLASSIFIER_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct WallScanResult {
	enum class Status { Classified, TooLarge, NoSprite, AlreadyInWall };
	uint16_t itemId = 0;
	std::string segment;             // canonical "horizontal".."pole"; empty when not classified
	float confidence = 0.0f;         // 0..100
	std::string secondSegment;       // runner-up, for tooltips
	float secondConfidence = 0.0f;
	Status status = Status::NoSprite;
	std::string existingWallName;    // wall brush already using the item, when AlreadyInWall
	std::string existingWallSegment; // segment it is used as, when known
};

class WallClassifier {
public:
	static constexpr size_t FEATURE_COUNT = 73;
	static constexpr size_t SEGMENT_COUNT = 16;
	// Canonical segment names, index order matches WallSegmentType in the editor.
	// The dialog converts them back with wallSegmentFromString() - never by index cast.
	static const std::array<std::string, SEGMENT_COUNT> SEGMENT_NAMES;

	static WallClassifier& Get(); // session singleton

	bool ensureTrained();         // lazy; true when sampleCount() > 0
	size_t sampleCount() const;
	size_t groupCount() const;    // number of wall brushes that contributed samples

	std::vector<WallScanResult> classify(const std::vector<uint16_t>& candidates);

	std::string validateLeaveOneOut(); // debug: per-segment accuracy report

private:
	struct Sample {
		std::array<float, FEATURE_COUNT> features;
		uint8_t segmentIndex; // 0..3, index into SEGMENT_NAMES
		uint16_t itemId;
	};

	WallScanResult classifyOne(uint16_t itemId) const;
	static bool extractFeatures(uint16_t itemId, std::array<float, FEATURE_COUNT>& out,
								WallScanResult::Status& failure);
	// Wall brush (if any) that already lists this item, in any alignment.
	static bool findOwningWall(uint16_t itemId, std::string& outBrushName, std::string& outSegment);

	std::vector<Sample> m_samples;
	size_t m_groupCount = 0;
	size_t m_trainedBrushMapSize = 0; // retrain trigger on version switch
	bool m_trained = false;
};

#endif // RME_WALL_CLASSIFIER_H

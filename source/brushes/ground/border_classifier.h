//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Border Scan classifier - shape-based kNN edge classifier.
//
// Predicts which of the 12 border edges ("n","e","s","w","cnw","cne",
// "csw","cse","dnw","dne","dse","dsw") an item sprite belongs to, by
// comparing its alpha silhouette against every border item already
// registered in g_brushes.getBorders() (the training set).
//
// Design notes:
// - Edge identities travel as canonical name strings ONLY. This class
//   never sees BorderEdgePosition (UI enum) and the UI never sees
//   BorderType: the two enums have different corner orderings, so any
//   numeric cast between them is a silent corner-swap bug. The single
//   conversion point is the dialog's apply step via edgeStringToPosition().
// - AlreadyInBorder results still carry edge/confidence; the flag is
//   informative (the UI may exclude such rows from auto-assignment).
// - This class is UI-free by design (no wx includes) so it can later be
//   exposed to Lua without dragging UI dependencies along.
//////////////////////////////////////////////////////////////////////

#ifndef RME_BORDER_CLASSIFIER_H
#define RME_BORDER_CLASSIFIER_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct BorderScanResult {
	enum class Status { Classified, MultiTile, NoSprite, AlreadyInBorder };
	uint16_t itemId = 0;
	std::string edge;              // canonical name "n".."dsw"; empty when not classified
	float confidence = 0.0f;       // 0..100
	std::string secondEdge;        // runner-up, for tooltips
	float secondConfidence = 0.0f;
	Status status = Status::NoSprite;
	uint32_t existingBorderId = 0; // lowest matching border id when AlreadyInBorder
};

class BorderClassifier {
public:
	static constexpr size_t FEATURE_COUNT = 71;
	static constexpr size_t EDGE_COUNT = 12;
	// Canonical edge names, index = BorderType - 1 (csw before cse, dse before dsw).
	static const std::array<std::string, EDGE_COUNT> EDGE_NAMES;

	static BorderClassifier& Get(); // session singleton

	bool ensureTrained();           // lazy; true when sampleCount() > 0
	size_t sampleCount() const;
	size_t groupCount() const;

	std::vector<BorderScanResult> classify(const std::vector<uint16_t>& candidates);

	std::string validateLeaveOneOut(); // debug: per-edge accuracy report

private:
	struct Sample {
		std::array<float, FEATURE_COUNT> features;
		uint8_t edgeIndex; // 0..11 = BorderType - 1
		uint16_t itemId;
		uint32_t borderId;
	};

	BorderScanResult classifyOne(uint16_t itemId) const;
	static bool extractFeatures(uint16_t itemId, std::array<float, FEATURE_COUNT>& out,
								BorderScanResult::Status& failure);

	std::vector<Sample> m_samples;
	size_t m_groupCount = 0;
	size_t m_trainedBorderMapSize = 0; // retrain trigger on version switch
	bool m_trained = false;
};

#endif // RME_BORDER_CLASSIFIER_H

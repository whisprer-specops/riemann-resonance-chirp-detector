#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace rrchirp {

struct DetectorConfig {
    int historyRows = 96;
    int windowRows = 48;
    int windowStepRows = 12;
    int slopeCountPerDirection = 18;
    float minAbsSlopeBinsPerRow = 0.05f;
    float maxAbsSlopeBinsPerRow = 2.75f;
    int ridgeHalfWidthBins = 1;
    int maxDetections = 12;
    float minScore = 0.58f;
    float nmsIoUThreshold = 0.30f;
    float renyiAlpha = 2.0f;
    int permutationOrder = 3;
    int permutationDelay = 1;
    bool enableZetaResonance = true;
    int zetaZeroCount = 12;

    float ridgeSnrWeight = 3.25f;
    float renyiSharpnessWeight = 1.50f;
    float permutationStructureWeight = 1.00f;
    float zetaResonanceWeight = 0.90f;
    float entropyCentralityWeight = 1.10f;

    bool valid() const;
    void clampInPlace();
};

struct Detection {
    float score = 0.0f;
    int t0 = 0;
    int t1 = 0;
    int f0 = 0;
    int f1 = 0;
    float startBin = 0.0f;
    float slopeBinsPerRow = 0.0f;
    float ridgeSnr = 0.0f;
    float renyiSharpness = 0.0f;
    float permutationStructure = 0.0f;
    float zetaResonance = 0.0f;
    float entropyCentrality = 0.0f;
    std::string label;
};

class RiemannChirpDetector {
public:
    explicit RiemannChirpDetector(DetectorConfig config = DetectorConfig{});

    void setConfig(const DetectorConfig& config);
    const DetectorConfig& config() const;

    void reset();
    void pushFftRow(const float* row, int width);
    [[nodiscard]] int width() const;
    [[nodiscard]] int rowCount() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::vector<Detection> detect() const;

private:
    struct Candidate {
        float score = 0.0f;
        int t0 = 0;
        int t1 = 0;
        float startBin = 0.0f;
        float slope = 0.0f;
        float ridgeSnr = 0.0f;
        float renyiSharpness = 0.0f;
        float permutationStructure = 0.0f;
        float zetaResonance = 0.0f;
        float entropyCentrality = 0.0f;
    };

    DetectorConfig cfg_;
    int width_ = 0;
    std::vector<std::vector<float>> ring_;
    std::size_t nextRow_ = 0;
    bool filled_ = false;

    [[nodiscard]] std::vector<float> buildSlopes() const;
    [[nodiscard]] std::vector<float> historyOldestToNewest() const;
    [[nodiscard]] std::vector<float> normalizedHistory() const;
    [[nodiscard]] std::vector<Candidate> scanCandidates(const std::vector<float>& norm, int rows, int cols, int keepPerWindow) const;
    [[nodiscard]] bool bestForSlope(const std::vector<float>& window, int winRows, int cols, int globalT0, float slope, Candidate& out) const;
    [[nodiscard]] Detection candidateToDetection(const Candidate& candidate, int rows, int cols) const;
};

} // namespace rrchirp

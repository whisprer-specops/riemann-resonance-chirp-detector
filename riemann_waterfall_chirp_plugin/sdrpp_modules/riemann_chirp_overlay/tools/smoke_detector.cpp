#include "../src/detector.h"

#include <cmath>
#include <iostream>
#include <random>
#include <vector>

int main() {
    rrchirp::DetectorConfig cfg;
    cfg.historyRows = 96;
    cfg.windowRows = 48;
    cfg.minScore = 0.45f;
    rrchirp::RiemannChirpDetector detector(cfg);

    constexpr int rows = 110;
    constexpr int cols = 256;
    std::mt19937 rng(1337);
    std::normal_distribution<float> noise(-95.0f, 4.0f);

    for (int t = 0; t < rows; ++t) {
        std::vector<float> row(cols);
        for (int f = 0; f < cols; ++f) { row[f] = noise(rng); }
        if (t >= 22 && t < 78) {
            int f = static_cast<int>(std::round(38.0f + 1.45f * static_cast<float>(t - 22)));
            for (int df = -1; df <= 1; ++df) {
                int ff = f + df;
                if (ff >= 0 && ff < cols) { row[ff] += 38.0f - 8.0f * std::abs(df); }
            }
        }
        detector.pushFftRow(row.data(), cols);
    }

    auto detections = detector.detect();
    std::cout << "detections=" << detections.size() << "\n";
    for (const auto& d : detections) {
        std::cout << d.label << " score=" << d.score << " t=[" << d.t0 << "," << d.t1
                  << "] f=[" << d.f0 << "," << d.f1 << "] slope=" << d.slopeBinsPerRow << "\n";
    }
    return detections.empty() ? 1 : 0;
}

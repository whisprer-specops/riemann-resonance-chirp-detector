#include "detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace rrchirp {
namespace {

constexpr float EPS = 1.0e-9f;

float clamp01(float v) {
    if (!std::isfinite(v)) { return 0.0f; }
    return std::max(0.0f, std::min(1.0f, v));
}

float safeAt(const std::vector<float>& matrix, int rows, int cols, int r, int c) {
    if (r < 0 || r >= rows || c < 0 || c >= cols) { return 0.0f; }
    return matrix[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)];
}

float percentile(std::vector<float> values, float q) {
    if (values.empty()) { return 0.0f; }
    q = std::max(0.0f, std::min(1.0f, q));
    const std::size_t idx = static_cast<std::size_t>(q * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<long>(idx), values.end());
    return values[idx];
}

float median(std::vector<float> values) {
    return percentile(std::move(values), 0.5f);
}

float mean(const std::vector<float>& values) {
    if (values.empty()) { return 0.0f; }
    double acc = 0.0;
    for (float v : values) { acc += v; }
    return static_cast<float>(acc / static_cast<double>(values.size()));
}

float stddev(const std::vector<float>& values, float mu) {
    if (values.size() < 2) { return 0.0f; }
    double acc = 0.0;
    for (float v : values) {
        const double d = static_cast<double>(v - mu);
        acc += d * d;
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(values.size() - 1)));
}

float weightedScore(std::initializer_list<std::pair<float, float>> values) {
    double weightSum = 0.0;
    double acc = 0.0;
    for (const auto& [wRaw, vRaw] : values) {
        const float w = std::max(0.0f, wRaw);
        const float v = clamp01(vRaw);
        weightSum += static_cast<double>(w);
        acc += static_cast<double>(w) * static_cast<double>(v);
    }
    if (weightSum <= EPS) { return 0.0f; }
    return clamp01(static_cast<float>(acc / weightSum));
}

float rowInterpolated(const std::vector<float>& matrix, int rows, int cols, int row, float x) {
    if (row < 0 || row >= rows || x < 0.0f || x >= static_cast<float>(cols - 1)) { return 0.0f; }
    const int left = static_cast<int>(std::floor(x));
    const float frac = x - static_cast<float>(left);
    const float a = safeAt(matrix, rows, cols, row, left);
    const float b = safeAt(matrix, rows, cols, row, left + 1);
    return a * (1.0f - frac) + b * frac;
}

float renyiSharpness(const std::vector<float>& values, float alpha) {
    if (values.size() < 2) { return 0.0f; }
    alpha = std::max(1.01f, alpha);
    std::vector<float> positive;
    positive.reserve(values.size());
    float minV = *std::min_element(values.begin(), values.end());
    double sum = 0.0;
    for (float v : values) {
        float p = std::max(0.0f, v - minV) + EPS;
        positive.push_back(p);
        sum += p;
    }
    if (sum <= EPS) { return 0.0f; }
    double moment = 0.0;
    for (float p : positive) {
        const double prob = static_cast<double>(p) / sum;
        moment += std::pow(prob, static_cast<double>(alpha));
    }
    if (moment <= 0.0) { return 0.0f; }
    const double h = std::log(moment) / (1.0 - static_cast<double>(alpha));
    const double hMax = std::log(static_cast<double>(values.size()));
    if (hMax <= 0.0) { return 0.0f; }
    return clamp01(static_cast<float>(1.0 - (h / hMax)));
}

int factorial(int n) {
    int out = 1;
    for (int i = 2; i <= n; ++i) { out *= i; }
    return out;
}

std::uint32_t ordinalPatternId(const std::vector<float>& values, int offset, int order, int delay) {
    std::vector<int> idx(static_cast<std::size_t>(order));
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        return values[static_cast<std::size_t>(offset + a * delay)] < values[static_cast<std::size_t>(offset + b * delay)];
    });

    std::uint32_t id = 0;
    for (int i = 0; i < order; ++i) {
        id = id * static_cast<std::uint32_t>(order) + static_cast<std::uint32_t>(idx[static_cast<std::size_t>(i)]);
    }
    return id;
}

float permutationStructureScore(const std::vector<float>& values, int order, int delay) {
    order = std::max(2, std::min(5, order));
    delay = std::max(1, delay);
    const int span = (order - 1) * delay + 1;
    if (static_cast<int>(values.size()) < span + 2) { return 0.0f; }

    std::unordered_map<std::uint32_t, int> counts;
    int total = 0;
    for (int i = 0; i + span <= static_cast<int>(values.size()); ++i) {
        counts[ordinalPatternId(values, i, order, delay)]++;
        ++total;
    }
    if (total <= 0) { return 0.0f; }

    double h = 0.0;
    for (const auto& [_, count] : counts) {
        const double p = static_cast<double>(count) / static_cast<double>(total);
        h -= p * std::log(p + EPS);
    }
    const double hMax = std::log(static_cast<double>(factorial(order)));
    if (hMax <= 0.0) { return 0.0f; }
    return clamp01(static_cast<float>(1.0 - (h / hMax)));
}

float zetaResonanceScore(const std::vector<float>& values, int zeroCount) {
    static constexpr std::array<double, 24> zeros = {
        14.134725141734693, 21.022039638771554, 25.010857580145688,
        30.424876125859513, 32.935061587739190, 37.586178158825671,
        40.918719012147495, 43.327073280914999, 48.005150881167159,
        49.773832477672302, 52.970321477714460, 56.446247697063394,
        59.347044002602353, 60.831778524609809, 65.112544048081607,
        67.079810529494173, 69.546401711173979, 72.067157674481907,
        75.704690699083933, 77.144840068874805, 79.337375020249367,
        82.910380854086030, 84.735492981329459, 87.425274613125229
    };
    if (values.size() < 8) { return 0.0f; }
    zeroCount = std::max(1, std::min(zeroCount, static_cast<int>(zeros.size())));

    const float mu = mean(values);
    const float sd = std::max(stddev(values, mu), 1.0e-5f);
    std::vector<double> x;
    x.reserve(values.size());
    for (float v : values) { x.push_back(static_cast<double>((v - mu) / sd)); }

    const std::array<double, 5> scales = { 1.0, 1.75, 2.75, 4.25, 6.5 };
    double best = 0.0;
    for (int zi = 0; zi < zeroCount; ++zi) {
        const double gamma = zeros[static_cast<std::size_t>(zi)];
        for (double scale : scales) {
            double c = 0.0;
            double s = 0.0;
            double norm = 0.0;
            for (std::size_t i = 0; i < x.size(); ++i) {
                const double phase = gamma * std::log1p(static_cast<double>(i + 1) / scale);
                c += x[i] * std::cos(phase);
                s += x[i] * std::sin(phase);
                norm += x[i] * x[i];
            }
            if (norm <= EPS) { continue; }
            const double mag = std::sqrt(c * c + s * s) / std::sqrt(norm * static_cast<double>(x.size()));
            best = std::max(best, mag);
        }
    }
    return clamp01(static_cast<float>(best * 1.85));
}

float entropyCentralityProxy(const std::vector<float>& window, int rows, int cols, const std::vector<int>& ridgeFreqs, int halfWidth) {
    if (window.empty() || ridgeFreqs.empty()) { return 0.0f; }

    std::vector<float> ridge;
    std::vector<float> background;
    ridge.reserve(ridgeFreqs.size() * static_cast<std::size_t>(2 * halfWidth + 1));
    background.reserve(window.size());

    for (int r = 0; r < rows; ++r) {
        const int f = ridgeFreqs[static_cast<std::size_t>(std::min(r, static_cast<int>(ridgeFreqs.size()) - 1))];
        const int lo = std::max(0, f - halfWidth - 1);
        const int hi = std::min(cols - 1, f + halfWidth + 1);
        for (int c = 0; c < cols; ++c) {
            const float v = safeAt(window, rows, cols, r, c);
            if (c >= lo && c <= hi) { ridge.push_back(v); }
            else { background.push_back(v); }
        }
    }

    const float ridgeSharp = renyiSharpness(ridge, 2.0f);
    const float bgSharp = renyiSharpness(background, 2.0f);
    const float ridgeMean = mean(ridge);
    const float bgMean = mean(background);
    return clamp01(0.55f * ridgeSharp + 0.25f * std::max(0.0f, ridgeSharp - bgSharp) + 0.20f * std::max(0.0f, ridgeMean - bgMean));
}

float iou(const Detection& a, const Detection& b) {
    const int ix0 = std::max(a.f0, b.f0);
    const int iy0 = std::max(a.t0, b.t0);
    const int ix1 = std::min(a.f1, b.f1);
    const int iy1 = std::min(a.t1, b.t1);
    const int iw = std::max(0, ix1 - ix0);
    const int ih = std::max(0, iy1 - iy0);
    const int inter = iw * ih;
    const int areaA = std::max(0, a.f1 - a.f0) * std::max(0, a.t1 - a.t0);
    const int areaB = std::max(0, b.f1 - b.f0) * std::max(0, b.t1 - b.t0);
    const int denom = areaA + areaB - inter;
    if (denom <= 0) { return 0.0f; }
    return static_cast<float>(inter) / static_cast<float>(denom);
}

} // namespace

bool DetectorConfig::valid() const {
    return historyRows >= 16 && windowRows >= 8 && windowRows <= historyRows && windowStepRows >= 1 &&
           slopeCountPerDirection >= 1 && minAbsSlopeBinsPerRow >= 0.0f &&
           maxAbsSlopeBinsPerRow >= minAbsSlopeBinsPerRow && ridgeHalfWidthBins >= 0 &&
           maxDetections >= 1 && minScore >= 0.0f && minScore <= 1.0f;
}

void DetectorConfig::clampInPlace() {
    historyRows = std::max(16, std::min(512, historyRows));
    windowRows = std::max(8, std::min(historyRows, windowRows));
    windowStepRows = std::max(1, std::min(windowRows, windowStepRows));
    slopeCountPerDirection = std::max(1, std::min(96, slopeCountPerDirection));
    minAbsSlopeBinsPerRow = std::max(0.0f, minAbsSlopeBinsPerRow);
    maxAbsSlopeBinsPerRow = std::max(minAbsSlopeBinsPerRow + 0.001f, maxAbsSlopeBinsPerRow);
    ridgeHalfWidthBins = std::max(0, std::min(12, ridgeHalfWidthBins));
    maxDetections = std::max(1, std::min(128, maxDetections));
    minScore = clamp01(minScore);
    nmsIoUThreshold = clamp01(nmsIoUThreshold);
    renyiAlpha = std::max(1.01f, std::min(8.0f, renyiAlpha));
    permutationOrder = std::max(2, std::min(5, permutationOrder));
    permutationDelay = std::max(1, std::min(16, permutationDelay));
    zetaZeroCount = std::max(1, std::min(24, zetaZeroCount));
}

RiemannChirpDetector::RiemannChirpDetector(DetectorConfig config) : cfg_(config) {
    cfg_.clampInPlace();
}

void RiemannChirpDetector::setConfig(const DetectorConfig& config) {
    cfg_ = config;
    cfg_.clampInPlace();
    if (static_cast<int>(ring_.size()) != cfg_.historyRows) {
        reset();
    }
}

const DetectorConfig& RiemannChirpDetector::config() const {
    return cfg_;
}

void RiemannChirpDetector::reset() {
    width_ = 0;
    ring_.clear();
    nextRow_ = 0;
    filled_ = false;
}

void RiemannChirpDetector::pushFftRow(const float* row, int width) {
    if (row == nullptr || width <= 4) { return; }
    if (width_ != width || ring_.empty() || static_cast<int>(ring_.size()) != cfg_.historyRows) {
        width_ = width;
        ring_.assign(static_cast<std::size_t>(cfg_.historyRows), std::vector<float>(static_cast<std::size_t>(width_), 0.0f));
        nextRow_ = 0;
        filled_ = false;
    }

    std::vector<float> cleaned(static_cast<std::size_t>(width_));
    for (int i = 0; i < width_; ++i) {
        float v = row[i];
        if (!std::isfinite(v)) { v = -180.0f; }
        cleaned[static_cast<std::size_t>(i)] = v;
    }

    ring_[nextRow_] = std::move(cleaned);
    nextRow_ = (nextRow_ + 1U) % ring_.size();
    if (nextRow_ == 0U) { filled_ = true; }
}

int RiemannChirpDetector::width() const {
    return width_;
}

int RiemannChirpDetector::rowCount() const {
    if (ring_.empty()) { return 0; }
    return filled_ ? static_cast<int>(ring_.size()) : static_cast<int>(nextRow_);
}

bool RiemannChirpDetector::ready() const {
    return width_ > 4 && rowCount() >= cfg_.windowRows;
}

std::vector<float> RiemannChirpDetector::buildSlopes() const {
    std::vector<float> slopes;
    slopes.reserve(static_cast<std::size_t>(cfg_.slopeCountPerDirection * 2));
    if (cfg_.slopeCountPerDirection == 1) {
        slopes.push_back(-cfg_.maxAbsSlopeBinsPerRow);
        slopes.push_back(cfg_.maxAbsSlopeBinsPerRow);
        return slopes;
    }
    for (int i = cfg_.slopeCountPerDirection - 1; i >= 0; --i) {
        const float u = static_cast<float>(i) / static_cast<float>(cfg_.slopeCountPerDirection - 1);
        slopes.push_back(-(cfg_.minAbsSlopeBinsPerRow + (cfg_.maxAbsSlopeBinsPerRow - cfg_.minAbsSlopeBinsPerRow) * u));
    }
    for (int i = 0; i < cfg_.slopeCountPerDirection; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(cfg_.slopeCountPerDirection - 1);
        slopes.push_back(cfg_.minAbsSlopeBinsPerRow + (cfg_.maxAbsSlopeBinsPerRow - cfg_.minAbsSlopeBinsPerRow) * u);
    }
    return slopes;
}

std::vector<float> RiemannChirpDetector::historyOldestToNewest() const {
    const int rows = rowCount();
    std::vector<float> out(static_cast<std::size_t>(rows) * static_cast<std::size_t>(width_), 0.0f);
    if (rows <= 0 || width_ <= 0) { return out; }

    const std::size_t start = filled_ ? nextRow_ : 0U;
    for (int r = 0; r < rows; ++r) {
        const std::size_t src = (start + static_cast<std::size_t>(r)) % ring_.size();
        std::copy(ring_[src].begin(), ring_[src].end(), out.begin() + static_cast<long>(r * width_));
    }
    return out;
}

std::vector<float> RiemannChirpDetector::normalizedHistory() const {
    const int rows = rowCount();
    const int cols = width_;
    std::vector<float> hist = historyOldestToNewest();
    if (rows <= 0 || cols <= 0) { return hist; }

    // Per-frequency high-pass: remove the median level of each FFT bin across recent rows.
    for (int c = 0; c < cols; ++c) {
        std::vector<float> colVals;
        colVals.reserve(static_cast<std::size_t>(rows));
        for (int r = 0; r < rows; ++r) {
            colVals.push_back(hist[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)]);
        }
        const float med = median(std::move(colVals));
        for (int r = 0; r < rows; ++r) {
            hist[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(c)] -= med;
        }
    }

    std::vector<float> all = hist;
    const float med = median(all);
    for (float& v : all) { v = std::fabs(v - med); }
    const float mad = std::max(median(all), 1.0e-4f);
    const float sigma = std::max(1.4826f * mad, 1.0e-4f);

    for (float& v : hist) {
        const float z = (v - med) / (3.25f * sigma);
        v = clamp01(0.5f + 0.5f * z);
        // Bias toward energy above local background while preserving weak traces.
        v = std::pow(v, 1.35f);
    }
    return hist;
}

std::vector<Detection> RiemannChirpDetector::detect() const {
    if (!ready()) { return {}; }
    const int rows = rowCount();
    const int cols = width_;
    std::vector<float> norm = normalizedHistory();
    std::vector<Candidate> candidates = scanCandidates(norm, rows, cols, 5);

    std::vector<Detection> detections;
    detections.reserve(candidates.size());
    for (const Candidate& c : candidates) {
        Detection d = candidateToDetection(c, rows, cols);
        if (d.score >= cfg_.minScore) { detections.push_back(std::move(d)); }
    }

    std::sort(detections.begin(), detections.end(), [](const Detection& a, const Detection& b) {
        return a.score > b.score;
    });

    std::vector<Detection> kept;
    kept.reserve(static_cast<std::size_t>(cfg_.maxDetections));
    for (const Detection& d : detections) {
        bool overlaps = false;
        for (const Detection& old : kept) {
            if (iou(d, old) > cfg_.nmsIoUThreshold) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) { kept.push_back(d); }
        if (static_cast<int>(kept.size()) >= cfg_.maxDetections) { break; }
    }
    return kept;
}

std::vector<RiemannChirpDetector::Candidate> RiemannChirpDetector::scanCandidates(const std::vector<float>& norm, int rows, int cols, int keepPerWindow) const {
    std::vector<Candidate> all;
    const int win = std::min(cfg_.windowRows, rows);
    std::vector<int> starts;
    if (rows <= win) {
        starts.push_back(0);
    }
    else {
        for (int t0 = 0; t0 <= rows - win; t0 += cfg_.windowStepRows) { starts.push_back(t0); }
        if (starts.empty() || starts.back() != rows - win) { starts.push_back(rows - win); }
    }

    const std::vector<float> slopes = buildSlopes();
    for (int t0 : starts) {
        std::vector<float> window(static_cast<std::size_t>(win) * static_cast<std::size_t>(cols));
        for (int r = 0; r < win; ++r) {
            const auto src = norm.begin() + static_cast<long>((t0 + r) * cols);
            std::copy(src, src + cols, window.begin() + static_cast<long>(r * cols));
        }

        std::vector<Candidate> local;
        local.reserve(slopes.size());
        for (float slope : slopes) {
            Candidate c;
            if (bestForSlope(window, win, cols, t0, slope, c)) { local.push_back(c); }
        }
        std::sort(local.begin(), local.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
        const int keep = std::min(keepPerWindow, static_cast<int>(local.size()));
        for (int i = 0; i < keep; ++i) { all.push_back(local[static_cast<std::size_t>(i)]); }
    }

    std::sort(all.begin(), all.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    const int cap = std::max(cfg_.maxDetections * 8, cfg_.maxDetections);
    if (static_cast<int>(all.size()) > cap) { all.resize(static_cast<std::size_t>(cap)); }
    return all;
}

bool RiemannChirpDetector::bestForSlope(const std::vector<float>& window, int winRows, int cols, int globalT0, float slope, Candidate& out) const {
    if (winRows <= 0 || cols <= 2) { return false; }
    std::vector<float> accum(static_cast<std::size_t>(cols), 0.0f);
    std::vector<int> counts(static_cast<std::size_t>(cols), 0);

    for (int t = 0; t < winRows; ++t) {
        for (int start = 0; start < cols; ++start) {
            const float pos = static_cast<float>(start) + slope * static_cast<float>(t);
            if (pos < 0.0f || pos >= static_cast<float>(cols - 1)) { continue; }
            accum[static_cast<std::size_t>(start)] += rowInterpolated(window, winRows, cols, t, pos);
            counts[static_cast<std::size_t>(start)] += 1;
        }
    }

    const int minCount = std::max(8, static_cast<int>(std::round(static_cast<float>(winRows) * 0.70f)));
    int bestStart = -1;
    float bestMean = -std::numeric_limits<float>::infinity();
    for (int start = 0; start < cols; ++start) {
        if (counts[static_cast<std::size_t>(start)] < minCount) { continue; }
        const float v = accum[static_cast<std::size_t>(start)] / static_cast<float>(counts[static_cast<std::size_t>(start)]);
        if (v > bestMean) {
            bestMean = v;
            bestStart = start;
        }
    }
    if (bestStart < 0 || bestMean <= EPS) { return false; }

    std::vector<float> ridgeVals;
    std::vector<int> ridgeFreqs;
    ridgeVals.reserve(static_cast<std::size_t>(winRows));
    ridgeFreqs.reserve(static_cast<std::size_t>(winRows));
    const int half = std::max(0, cfg_.ridgeHalfWidthBins);
    for (int t = 0; t < winRows; ++t) {
        const int center = static_cast<int>(std::round(static_cast<float>(bestStart) + slope * static_cast<float>(t)));
        if (center < 0 || center >= cols) { continue; }
        const int lo = std::max(0, center - half);
        const int hi = std::min(cols - 1, center + half);
        float localMax = -std::numeric_limits<float>::infinity();
        for (int f = lo; f <= hi; ++f) { localMax = std::max(localMax, safeAt(window, winRows, cols, t, f)); }
        ridgeVals.push_back(localMax);
        ridgeFreqs.push_back(center);
    }
    if (ridgeVals.size() < 8) { return false; }

    std::vector<float> background;
    background.reserve(window.size());
    const int bgHalf = std::max(half + 2, 3);
    for (int t = 0; t < winRows; ++t) {
        const int center = ridgeFreqs[static_cast<std::size_t>(std::min(t, static_cast<int>(ridgeFreqs.size()) - 1))];
        const int lo = std::max(0, center - bgHalf);
        const int hi = std::min(cols - 1, center + bgHalf);
        for (int f = 0; f < cols; ++f) {
            if (f < lo || f > hi) { background.push_back(safeAt(window, winRows, cols, t, f)); }
        }
    }

    const float bgMed = background.empty() ? 0.0f : median(background);
    std::vector<float> absdev;
    absdev.reserve(background.size());
    for (float v : background) { absdev.push_back(std::fabs(v - bgMed)); }
    const float bgMad = background.empty() ? 0.0f : median(absdev);
    const float bgSigma = std::max(1.4826f * bgMad, std::max(stddev(background, bgMed), 1.0e-4f));
    const float ridgeMean = mean(ridgeVals);
    const float ridgePeak = *std::max_element(ridgeVals.begin(), ridgeVals.end());
    const float rawSnr = (ridgeMean - bgMed) / bgSigma;
    const float peakLift = (ridgePeak - bgMed) / bgSigma;
    const float ridgeSnr = clamp01(0.72f * (rawSnr / 3.0f) + 0.28f * (peakLift / 6.0f));

    std::vector<float> patch;
    patch.reserve(ridgeVals.size() * static_cast<std::size_t>(2 * half + 5));
    const int patchHalf = std::max(half + 1, 2);
    for (int t = 0; t < static_cast<int>(ridgeFreqs.size()); ++t) {
        const int center = ridgeFreqs[static_cast<std::size_t>(t)];
        const int lo = std::max(0, center - patchHalf);
        const int hi = std::min(cols - 1, center + patchHalf);
        for (int f = lo; f <= hi; ++f) { patch.push_back(safeAt(window, winRows, cols, t, f)); }
    }

    const float sharp = renyiSharpness(patch, cfg_.renyiAlpha);
    const float perm = permutationStructureScore(ridgeVals, cfg_.permutationOrder, cfg_.permutationDelay);
    const float zeta = cfg_.enableZetaResonance ? zetaResonanceScore(ridgeVals, cfg_.zetaZeroCount) : 0.0f;
    const float centrality = entropyCentralityProxy(window, winRows, cols, ridgeFreqs, half);

    const float rawScore = weightedScore({
        { cfg_.ridgeSnrWeight, ridgeSnr },
        { cfg_.renyiSharpnessWeight, sharp },
        { cfg_.permutationStructureWeight, perm },
        { cfg_.zetaResonanceWeight, zeta },
        { cfg_.entropyCentralityWeight, centrality },
    });
    const float calibrated = clamp01(1.0f - std::exp(-2.45f * rawScore));

    out.score = calibrated;
    out.t0 = globalT0;
    out.t1 = globalT0 + winRows;
    out.startBin = static_cast<float>(bestStart);
    out.slope = slope;
    out.ridgeSnr = ridgeSnr;
    out.renyiSharpness = sharp;
    out.permutationStructure = perm;
    out.zetaResonance = zeta;
    out.entropyCentrality = centrality;
    return true;
}

Detection RiemannChirpDetector::candidateToDetection(const Candidate& candidate, int rows, int cols) const {
    Detection det;
    det.score = candidate.score;
    det.t0 = std::max(0, candidate.t0);
    det.t1 = std::min(rows, candidate.t1);
    det.startBin = candidate.startBin;
    det.slopeBinsPerRow = candidate.slope;
    det.ridgeSnr = candidate.ridgeSnr;
    det.renyiSharpness = candidate.renyiSharpness;
    det.permutationStructure = candidate.permutationStructure;
    det.zetaResonance = candidate.zetaResonance;
    det.entropyCentrality = candidate.entropyCentrality;
    det.label = candidate.zetaResonance >= 0.25f ? "riemann_resonant_chirp" : "entropy_chirp";

    float minF = std::numeric_limits<float>::infinity();
    float maxF = -std::numeric_limits<float>::infinity();
    for (int t = det.t0; t < det.t1; ++t) {
        const float localT = static_cast<float>(t - candidate.t0);
        const float f = candidate.startBin + candidate.slope * localT;
        if (f >= 0.0f && f < static_cast<float>(cols)) {
            minF = std::min(minF, f);
            maxF = std::max(maxF, f);
        }
    }
    if (!std::isfinite(minF) || !std::isfinite(maxF)) {
        minF = candidate.startBin;
        maxF = candidate.startBin;
    }
    const int pad = std::max(cfg_.ridgeHalfWidthBins + 3, 4);
    det.f0 = std::max(0, static_cast<int>(std::floor(minF)) - pad);
    det.f1 = std::min(cols, static_cast<int>(std::ceil(maxF)) + pad + 1);
    return det;
}

} // namespace rrchirp

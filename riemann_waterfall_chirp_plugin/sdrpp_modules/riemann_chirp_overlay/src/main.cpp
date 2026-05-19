#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include <core.h>
#include <gui/gui.h>
#include <gui/widgets/waterfall.h>
#include <imgui.h>
#include <module.h>
#include <utils/flog.h>

#include "detector.h"

SDRPP_MOD_INFO{
    /* Name: */ "riemann_chirp_overlay",
    /* Description: */ "Entropy/Riemann-resonance chirp highlighter for the SDR++ waterfall",
    /* Author: */ "Wolfy Tom + ChatGPT",
    /* Version: */ 0, 1, 0,
    /* Max instances */ 1
};

namespace {

using Clock = std::chrono::steady_clock;

float clampFloat(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

int clampInt(int value, int lo, int hi) {
    return std::max(lo, std::min(hi, value));
}

ImU32 scoreColor(float score, float alphaScale) {
    score = clampFloat(score, 0.0f, 1.0f);
    const int alpha = clampInt(static_cast<int>(std::round(55.0f + 165.0f * score * alphaScale)), 30, 230);
    const int red = clampInt(static_cast<int>(std::round(255.0f * score)), 120, 255);
    const int green = clampInt(static_cast<int>(std::round(120.0f + 120.0f * (1.0f - std::fabs(score - 0.65f)))), 80, 255);
    const int blue = clampInt(static_cast<int>(std::round(40.0f + 90.0f * (1.0f - score))), 20, 160);
    return IM_COL32(red, green, blue, alpha);
}

class RiemannChirpOverlayModule : public ModuleManager::Instance {
public:
    explicit RiemannChirpOverlayModule(std::string instanceName) : name(std::move(instanceName)), detector(config) {
        fftRedrawHandler.ctx = this;
        fftRedrawHandler.handler = fftRedraw;
        gui::waterfall.onFFTRedraw.bindHandler(&fftRedrawHandler);
        gui::menu.registerEntry(name, menuHandler, this, NULL);
        flog::info("riemann_chirp_overlay: module instance created");
    }

    ~RiemannChirpOverlayModule() override {
        gui::menu.removeEntry(name);
        gui::waterfall.onFFTRedraw.unbindHandler(&fftRedrawHandler);
        flog::info("riemann_chirp_overlay: module instance destroyed");
    }

    void postInit() override {}

    void enable() override {
        enabled = true;
    }

    void disable() override {
        enabled = false;
    }

    bool isEnabled() override {
        return enabled;
    }

private:
    static void menuHandler(void* ctx) {
        auto* self = static_cast<RiemannChirpOverlayModule*>(ctx);
        const float menuWidth = ImGui::GetContentRegionAvail().x;

        ImGui::Checkbox(("Enabled##rrchirp_enabled_" + self->name).c_str(), &self->enabled);
        ImGui::Checkbox(("Draw boxes##rrchirp_boxes_" + self->name).c_str(), &self->drawBoxes);
        ImGui::Checkbox(("Draw ridge lines##rrchirp_lines_" + self->name).c_str(), &self->drawLines);
        ImGui::Checkbox(("Newest waterfall row at top##rrchirp_newest_top_" + self->name).c_str(), &self->newestAtTop);

        if (ImGui::Button(("Clear detector history##rrchirp_clear_" + self->name).c_str(), ImVec2(menuWidth, 0))) {
            std::lock_guard<std::mutex> lock(self->mtx);
            self->detector.reset();
            self->latestDetections.clear();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Detection");
        bool changed = false;
        changed |= ImGui::SliderFloat(("Min score##rrchirp_min_score_" + self->name).c_str(), &self->config.minScore, 0.10f, 0.95f, "%.2f");
        changed |= ImGui::SliderInt(("History rows##rrchirp_history_" + self->name).c_str(), &self->config.historyRows, 32, 256);
        changed |= ImGui::SliderInt(("Window rows##rrchirp_window_" + self->name).c_str(), &self->config.windowRows, 16, 160);
        changed |= ImGui::SliderInt(("Window step##rrchirp_step_" + self->name).c_str(), &self->config.windowStepRows, 2, 48);
        changed |= ImGui::SliderInt(("Slopes / direction##rrchirp_slopes_" + self->name).c_str(), &self->config.slopeCountPerDirection, 4, 64);
        changed |= ImGui::SliderFloat(("Min |slope| bins/row##rrchirp_min_slope_" + self->name).c_str(), &self->config.minAbsSlopeBinsPerRow, 0.00f, 1.00f, "%.3f");
        changed |= ImGui::SliderFloat(("Max |slope| bins/row##rrchirp_max_slope_" + self->name).c_str(), &self->config.maxAbsSlopeBinsPerRow, 0.10f, 8.00f, "%.2f");
        changed |= ImGui::SliderInt(("Ridge half-width bins##rrchirp_half_width_" + self->name).c_str(), &self->config.ridgeHalfWidthBins, 0, 8);
        changed |= ImGui::SliderInt(("Max detections##rrchirp_max_det_" + self->name).c_str(), &self->config.maxDetections, 1, 48);

        ImGui::Separator();
        ImGui::TextUnformatted("Entropy / resonance weights");
        changed |= ImGui::SliderFloat(("Ridge SNR##rrchirp_w_snr_" + self->name).c_str(), &self->config.ridgeSnrWeight, 0.0f, 8.0f, "%.2f");
        changed |= ImGui::SliderFloat(("Renyi sharpness##rrchirp_w_renyi_" + self->name).c_str(), &self->config.renyiSharpnessWeight, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat(("Permutation structure##rrchirp_w_perm_" + self->name).c_str(), &self->config.permutationStructureWeight, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat(("Entropy centrality##rrchirp_w_cent_" + self->name).c_str(), &self->config.entropyCentralityWeight, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::Checkbox(("Zeta-zero resonance##rrchirp_enable_zeta_" + self->name).c_str(), &self->config.enableZetaResonance);
        changed |= ImGui::SliderFloat(("Zeta weight##rrchirp_w_zeta_" + self->name).c_str(), &self->config.zetaResonanceWeight, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::SliderInt(("Zeta zeros##rrchirp_zero_count_" + self->name).c_str(), &self->config.zetaZeroCount, 1, 24);

        ImGui::Separator();
        ImGui::Text("Rows buffered: %d", self->detector.rowCount());
        ImGui::Text("FFT width: %d", self->detector.width());
        ImGui::Text("Detections: %zu", self->latestDetections.size());
        if (!self->latestDetections.empty()) {
            const auto& d = self->latestDetections.front();
            ImGui::Text("Top: %.2f  slope %.2f  %s", d.score, d.slopeBinsPerRow, d.label.c_str());
        }

        if (changed) {
            std::lock_guard<std::mutex> lock(self->mtx);
            self->config.clampInPlace();
            self->detector.setConfig(self->config);
        }
    }

    static void fftRedraw(ImGui::WaterFall::FFTRedrawArgs args, void* ctx) {
        auto* self = static_cast<RiemannChirpOverlayModule*>(ctx);
        if (self == nullptr || !self->enabled) { return; }
        self->captureLatestFftRow();
        self->drawOverlay(args);
    }

    void captureLatestFftRow() {
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCapture).count();
        if (elapsed < capturePeriodMs) { return; }
        lastCapture = now;

        int width = 0;
        float* fft = gui::waterfall.acquireLatestFFT(width);
        if (fft == nullptr || width <= 4) {
            if (fft != nullptr) { gui::waterfall.releaseLatestFFT(); }
            return;
        }

        std::vector<float> row(static_cast<std::size_t>(width));
        std::copy(fft, fft + width, row.begin());
        gui::waterfall.releaseLatestFFT();

        std::lock_guard<std::mutex> lock(mtx);
        detector.pushFftRow(row.data(), width);
        rowsSinceDetect++;
        if (rowsSinceDetect >= detectEveryRows && detector.ready()) {
            latestDetections = detector.detect();
            rowsSinceDetect = 0;
        }
    }

    float rowToY(int row, int rows, float yMin, float yMax) const {
        if (rows <= 1) { return yMin; }
        const float h = yMax - yMin;
        const float uOldestToNewest = static_cast<float>(row) / static_cast<float>(rows - 1);
        if (newestAtTop) {
            return yMin + h * (1.0f - uOldestToNewest);
        }
        return yMin + h * uOldestToNewest;
    }

    void drawOverlay(ImGui::WaterFall::FFTRedrawArgs args) {
        std::vector<rrchirp::Detection> dets;
        int rows = 0;
        int cols = 0;
        {
            std::lock_guard<std::mutex> lock(mtx);
            dets = latestDetections;
            rows = detector.rowCount();
            cols = detector.width();
        }
        if (dets.empty() || rows <= 1 || cols <= 1 || args.window == nullptr) { return; }

        const ImVec2 wfMin = gui::waterfall.wfMin;
        const ImVec2 wfMax = gui::waterfall.wfMax;
        if (wfMax.x <= wfMin.x || wfMax.y <= wfMin.y) { return; }

        ImDrawList* drawList = args.window->DrawList;
        const float wfW = wfMax.x - wfMin.x;
        auto binToX = [&](float bin) -> float {
            const float u = clampFloat(bin / static_cast<float>(std::max(1, cols - 1)), 0.0f, 1.0f);
            return wfMin.x + wfW * u;
        };

        for (const auto& d : dets) {
            const ImU32 col = scoreColor(d.score, 1.0f);
            const ImU32 fill = scoreColor(d.score, 0.28f);
            const float x0 = binToX(static_cast<float>(d.f0));
            const float x1 = binToX(static_cast<float>(d.f1));
            float y0 = rowToY(d.t0, rows, wfMin.y, wfMax.y);
            float y1 = rowToY(d.t1, rows, wfMin.y, wfMax.y);
            if (y1 < y0) { std::swap(y0, y1); }

            if (drawBoxes) {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill, 2.0f);
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), col, 2.0f, 0, 1.6f);
            }

            if (drawLines) {
                const float localStart = 0.0f;
                const float localEnd = static_cast<float>(std::max(1, d.t1 - d.t0));
                const float fStart = d.startBin + d.slopeBinsPerRow * localStart;
                const float fEnd = d.startBin + d.slopeBinsPerRow * localEnd;
                const float lineX0 = binToX(fStart);
                const float lineX1 = binToX(fEnd);
                const float lineY0 = rowToY(d.t0, rows, wfMin.y, wfMax.y);
                const float lineY1 = rowToY(d.t1, rows, wfMin.y, wfMax.y);
                drawList->AddLine(ImVec2(lineX0, lineY0), ImVec2(lineX1, lineY1), IM_COL32(255, 255, 255, 220), 1.0f);
                drawList->AddLine(ImVec2(lineX0, lineY0), ImVec2(lineX1, lineY1), col, 2.4f);
            }

            char label[160];
            std::snprintf(label, sizeof(label), "%s %.2f", d.label.c_str(), d.score);
            drawList->AddText(ImVec2(x0 + 4.0f, std::max(wfMin.y, y0 - 16.0f)), col, label);
        }
    }

    std::string name;
    bool enabled = true;
    bool drawBoxes = true;
    bool drawLines = true;
    bool newestAtTop = true;
    int capturePeriodMs = 50;
    int detectEveryRows = 3;
    int rowsSinceDetect = 0;
    Clock::time_point lastCapture = Clock::now();
    rrchirp::DetectorConfig config;
    rrchirp::RiemannChirpDetector detector;
    std::vector<rrchirp::Detection> latestDetections;
    std::mutex mtx;
    EventHandler<ImGui::WaterFall::FFTRedrawArgs> fftRedrawHandler;
};

} // namespace

MOD_EXPORT void _INIT_() {
    flog::info("riemann_chirp_overlay: init");
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new RiemannChirpOverlayModule(std::move(name));
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete static_cast<RiemannChirpOverlayModule*>(instance);
}

MOD_EXPORT void _END_() {
    flog::info("riemann_chirp_overlay: end");
}

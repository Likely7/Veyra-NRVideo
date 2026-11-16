#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <string_view>

namespace {
constexpr float kInfinity = 1.0e9f;
constexpr std::array<int, 9> kRadii{2, 4, 8, 12, 16, 24, 32, 48, 64};
static_assert(std::is_sorted(kRadii.begin(), kRadii.end()));

struct RefineResult {
    int selected = 0;
    float cost = kInfinity;
    uint64_t previousSamples = 0;
};

using PatchCosts = std::array<std::array<float, 9>, 9>;

RefineResult refineReference(const PatchCosts& differences, float tieBias) {
    RefineResult result;
    for (int candidate = 0; candidate < 9; ++candidate) {
        float cost = 0.0f;
        for (int sample = 0; sample < 9; ++sample) {
            cost += differences[size_t(candidate)][size_t(sample)];
            ++result.previousSamples;
        }
        cost /= 9.0f;
        if (candidate != 4) cost += tieBias;
        if (cost < result.cost) {
            result.cost = cost;
            result.selected = candidate;
        }
    }
    return result;
}

// Mirrors the centre and five-sample prefix lower bounds in FlowRefine.hlsl.
RefineResult refinePruned(const PatchCosts& differences, float tieBias) {
    RefineResult result;
    // Match the shader's centre-first evaluation. Explicitly retain the
    // original lowest-index winner when two candidates have identical costs.
    for (int order = 0; order < 9; ++order) {
        const int candidate = order == 0 ? 4 : (order <= 4 ? order - 1 : order);
        const float penalty = candidate != 4 ? tieBias : 0.0f;
        const float centre = differences[size_t(candidate)][4];
        ++result.previousSamples;
        if (centre / 9.0f + penalty >= result.cost) continue;

        float cost = 0.0f;
        for (int sample = 0; sample < 5; ++sample) {
            if (sample == 4) {
                cost += centre;
            } else {
                cost += differences[size_t(candidate)][size_t(sample)];
                ++result.previousSamples;
            }
        }
        if (cost / 9.0f + penalty >= result.cost) continue;
        for (int sample = 5; sample < 9; ++sample) {
            cost += differences[size_t(candidate)][size_t(sample)];
            ++result.previousSamples;
        }
        cost /= 9.0f;
        cost += penalty;
        if (cost < result.cost || (cost == result.cost && candidate < result.selected)) {
            result.cost = cost;
            result.selected = candidate;
        }
    }
    return result;
}

struct FillCandidate {
    bool confident = false;
    float colourDistance = 0.0f;
};

struct FillResult {
    int selected = -1;
    float score = kInfinity;
    uint64_t candidatesRead = 0;
    uint64_t coloursRead = 0;
};

using FillCandidates = std::array<FillCandidate, 72>;

FillResult fillReference(const FillCandidates& candidates, float distanceWeight) {
    FillResult result;
    for (int radius = 0; radius < 9; ++radius) {
        const float penalty = distanceWeight * float(kRadii[size_t(radius)]);
        for (int direction = 0; direction < 8; ++direction) {
            const int index = radius * 8 + direction;
            ++result.candidatesRead;
            const auto& candidate = candidates[size_t(index)];
            if (!candidate.confident) continue;
            ++result.coloursRead;
            const float score = candidate.colourDistance + penalty;
            if (score < result.score) {
                result.score = score;
                result.selected = index;
            }
        }
    }
    return result;
}

// Mirrors FlowFill.hlsl's lower bound. A negative distance weight is not
// monotonic, so it deliberately falls back to the exhaustive search.
FillResult fillPruned(const FillCandidates& candidates, float distanceWeight) {
    FillResult result;
    for (int radius = 0; radius < 9; ++radius) {
        const float penalty = distanceWeight * float(kRadii[size_t(radius)]);
        if (distanceWeight >= 0.0f && result.score <= penalty) break;
        for (int direction = 0; direction < 8; ++direction) {
            const int index = radius * 8 + direction;
            ++result.candidatesRead;
            const auto& candidate = candidates[size_t(index)];
            if (!candidate.confident) continue;
            ++result.coloursRead;
            const float score = candidate.colourDistance + penalty;
            if (score < result.score) {
                result.score = score;
                result.selected = index;
            }
        }
        if (distanceWeight >= 0.0f && result.score <= penalty) break;
    }
    return result;
}

bool same(const RefineResult& a, const RefineResult& b) {
    return a.selected == b.selected && a.cost == b.cost;
}

bool same(const FillResult& a, const FillResult& b) {
    return a.selected == b.selected && a.score == b.score;
}

bool check(bool condition, std::string_view message) {
    if (!condition) std::cerr << "FAIL " << message << '\n';
    return condition;
}

bool testRefine() {
    std::mt19937 rng(0x56455952u);
    std::uniform_real_distribution<float> difference(0.0f, 1.0f);
    constexpr std::array<float, 5> biases{0.0f, 0.0001f, 0.001f, 0.02f, -0.002f};
    uint64_t referenceSamples = 0;
    uint64_t prunedSamples = 0;
    constexpr int casesPerBias = 10000;

    for (const float bias : biases) {
        for (int test = 0; test < casesPerBias; ++test) {
            PatchCosts costs{};
            for (auto& candidate : costs)
                for (float& sample : candidate) sample = difference(rng);
            if (test % 11 == 0) {
                for (float& sample : costs[4]) sample *= 0.01f;
            }
            const auto reference = refineReference(costs, bias);
            const auto pruned = refinePruned(costs, bias);
            if (!check(same(reference, pruned), "FlowRefine pruned search must preserve the exact winner and cost"))
                return false;
            referenceSamples += reference.previousSamples;
            prunedSamples += pruned.previousSamples;
        }
    }

    PatchCosts boundary{};
    for (auto& candidate : boundary) candidate.fill(0.8f);
    for (float& sample : boundary[4]) sample = 0.001f;
    const auto reference = refineReference(boundary, 0.001f);
    const auto pruned = refinePruned(boundary, 0.001f);
    if (!check(same(reference, pruned), "FlowRefine motion-boundary fixture preserves its winner")) return false;
    if (!check(pruned.previousSamples < reference.previousSamples,
               "FlowRefine lower bounds reduce previous-frame texture samples")) return false;

    PatchCosts equalCosts{};
    for (auto& candidate : equalCosts) candidate.fill(0.25f);
    if (!check(same(refineReference(equalCosts, 0.0f), refinePruned(equalCosts, 0.0f)),
               "FlowRefine preserves original lowest-index tie selection")) return false;
    if (!check(same(refineReference(equalCosts, 0.001f), refinePruned(equalCosts, 0.001f)),
               "FlowRefine preserves centre tie bias")) return false;

    std::cout << "FlowRefine: " << casesPerBias * int(biases.size())
              << " randomized cases; previous-frame samples "
              << prunedSamples << "/" << referenceSamples
              << " (randomized corpus), boundary fixture "
              << pruned.previousSamples << "/" << reference.previousSamples << '\n';
    return true;
}

bool testFill() {
    std::mt19937 rng(0x46494c4cu);
    std::uniform_real_distribution<float> colour(0.0f, 0.5f);
    std::bernoulli_distribution confident(0.35);
    constexpr std::array<float, 3> weights{0.002f, 0.0f, -0.001f};
    constexpr int casesPerWeight = 20000;
    uint64_t referenceCandidates = 0;
    uint64_t prunedCandidates = 0;

    for (const float weight : weights) {
        for (int test = 0; test < casesPerWeight; ++test) {
            FillCandidates candidates{};
            for (auto& candidate : candidates) {
                candidate.confident = confident(rng);
                candidate.colourDistance = colour(rng);
            }
            if (test % 13 == 0) {
                candidates[0].confident = true;
                candidates[0].colourDistance = 0.0f;
            }
            const auto reference = fillReference(candidates, weight);
            const auto pruned = fillPruned(candidates, weight);
            if (!check(same(reference, pruned), "FlowFill pruned search must preserve the exact winner and score"))
                return false;
            referenceCandidates += reference.candidatesRead;
            prunedCandidates += pruned.candidatesRead;
        }
    }

    FillCandidates boundary{};
    for (auto& candidate : boundary) {
        candidate.confident = true;
        candidate.colourDistance = 0.0f;
    }
    const auto reference = fillReference(boundary, 0.002f);
    const auto pruned = fillPruned(boundary, 0.002f);
    if (!check(same(reference, pruned), "FlowFill perfect-match fixture preserves its winner")) return false;
    if (!check(pruned.candidatesRead < reference.candidatesRead,
               "FlowFill radius lower bound reduces candidate reads")) return false;

    std::cout << "FlowFill: " << casesPerWeight * int(weights.size())
              << " randomized cases; candidate reads "
              << prunedCandidates << "/" << referenceCandidates
              << " (randomized corpus), perfect-match fixture "
              << pruned.candidatesRead << "/" << reference.candidatesRead << '\n';
    return true;
}
} // namespace

int main() {
    if (!testRefine() || !testFill()) return 1;
    std::cout << "PASS FlowSearchPruningTests\n";
    return 0;
}

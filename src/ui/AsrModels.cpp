// SPDX-License-Identifier: GPL-3.0-only
#include "AsrModels.h"

#include <QDir>
#include <algorithm>

namespace veyra::ui::asr {
namespace {
constexpr ModelFile runtimeFiles[]{
    {"sherpa-onnx-c-api.dll", "6ec3f171edabd8778c698befc11e44b21e7c93651ad4e83733f742044564da88", 3257344},
    {"onnxruntime.dll", "9ab1e925c610b42410b1ebacc61306436282522221bf3b3c6ea5950d000d44be", 14203904},
    {"onnxruntime_providers_shared.dll", "3ef52275f70976fd0ac83f6a4e516cad045622851d5d4ce692f0926333cc8062", 10752},
};
constexpr ModelSource runtimeSources[]{
    {"https://gh-proxy.com/https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.2/sherpa-onnx-v2.0.2-win-x64-shared-MD-MinSizeRel-no-tts-lib.tar.bz2"},
};
constexpr ModelAsset runtime{
    "runtime-2.0.2",
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.2/sherpa-onnx-v2.0.2-win-x64-shared-MD-MinSizeRel-no-tts-lib.tar.bz2",
    6071299, "fb3ef6cf2e65d03e44d3ff3ac5a8a1f3a1e2710416359f86d59525a1309bf8ce",
    runtimeFiles, "sherpa-onnx-v2.0.2-win-x64-shared-MD-MinSizeRel-no-tts-lib/lib/", 2, true, runtimeSources
};
// This pinned upstream Silero V5 is also supported by sherpa-onnx's CPU VAD.
constexpr ModelSource vadSources[]{
    {"https://raw.githubusercontent.com/snakers4/silero-vad/5cd2ba54db059f961e7545d4f211b44f005a6dfb/files/silero_vad.onnx",
     false, 2313101, "6b99cbfd39246b6706f98ec13c7c50c6b299181f2474fa05cbc8046acc274396"},
    {"https://cdn.jsdelivr.net/gh/snakers4/silero-vad@5cd2ba54db059f961e7545d4f211b44f005a6dfb/files/silero_vad.onnx",
     false, 2313101, "6b99cbfd39246b6706f98ec13c7c50c6b299181f2474fa05cbc8046acc274396"},
};
constexpr ModelAsset vad{
    "silero_vad.onnx",
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx",
    643854, "9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6",
    {}, "", 0, true, vadSources
};
constexpr ModelFile parakeetFiles[]{
    {"encoder.int8.onnx", "92a10bd9c23936ea0566c29f963bc57c2a5df4261b6a8921c39abea66557ca8b", 652282309},
    {"decoder.int8.onnx", "4d097bae3bac5c96018d5293ba39f1b657296102b142affda40378f38b020f41", 7257753},
    {"joiner.int8.onnx", "77a29f3e5ac7d37074e0af465391b80f314015d5b97f0f90c6c7767f1d56f428", 1739080},
    {"tokens.txt", "ec182b70dd42113aff6c5372c75cac58c952443eb22322f57bbd7f53977d497d", 9384},
};
constexpr ModelFile whisperFiles[]{
    {"turbo-encoder.int8.onnx", "b02dcdf54f348741e93fe732b67d933c8dcb6735655f710640143081db38878b", 674716297},
    {"turbo-decoder.int8.onnx", "20accd02388482eb3a46bd615631adfdc85e1eb2c7db9ea3f02a40ffe6b81547", 361080764},
    {"turbo-tokens.txt", "b34b360dbb493e781e479794586d661700670d65564001f23024971d1f2fa126", 816730},
};
constexpr ModelSource parakeetSources[]{
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8/resolve/84b478d541cdbf0c296a7ab5f1e20a6f8f998114/", true},
    {"https://hf-mirror.com/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8/resolve/84b478d541cdbf0c296a7ab5f1e20a6f8f998114/", true},
};
constexpr ModelSource whisperSources[]{
    {"https://huggingface.co/csukuangfj/sherpa-onnx-whisper-turbo/resolve/2ca6ff69fc878651b770880507669577ac41c2ff/", true},
    {"https://hf-mirror.com/csukuangfj/sherpa-onnx-whisper-turbo/resolve/2ca6ff69fc878651b770880507669577ac41c2ff/", true},
};
constexpr ModelAsset parakeet[]{
    runtime, vad,
    {"models/parakeet-tdt-0.6b-v2-int8",
     "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8.tar.bz2",
     482440461, "6fc4426b54aa549a0019b8a75b8dea59164e71790a37ac56a045ad57f915f714",
     parakeetFiles, "sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8/", 1, false, parakeetSources}
};
constexpr ModelAsset whisper[]{
    runtime, vad,
    {"models/whisper-large-v3-turbo-int8",
     "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-turbo.tar.bz2",
     563790207, "b11acbbcd660b44a8e0df33724feb5aaa709cf65668f2823d59f656312544f22",
     whisperFiles, "sherpa-onnx-whisper-turbo/", 1, false, whisperSources}
};
}

QString root(const QString& dataDirectory) {
    return QDir(dataDirectory).absoluteFilePath(QStringLiteral("live-asr"));
}

std::span<const ModelAsset> assets(int model) {
    switch (model) {
    case Parakeet: return parakeet;
    case Whisper: return whisper;
    default: return {};
    }
}

bool verify(const QString& folder, int model, bool contents) {
    const auto bundle = assets(model);
    return !bundle.empty() && std::ranges::all_of(bundle, [&](const ModelAsset& asset) {
        return ModelAssets::verify(folder, asset, contents);
    });
}

QString whisperLanguage(const QString& language) {
    return language.section(QLatin1Char('-'), 0, 0).toLower();
}
}

// SPDX-License-Identifier: GPL-3.0-only
#include "../QmlPlayerBridge_p.h"
#include "../TranslationAssets.h"
#include "veyra/ngx/AnrProvider.h"

namespace veyra::ui {
namespace {
// Apex Compute's B20 Windows Addon. Import only the provider's directory;
// the OptiScaler/game installers in the archive are never extracted.
constexpr ModelFile anrFiles[]{
    {"adapters/pack.nrx", "8b1c812310f519cd4c6f60079f214f009117cfc35e1f6ffd9835c213c43222fe", 35318},
    {"kernels/pack.nrx", "15b864a39495d8d9a0a92cb3799928055e6b9e5d1ad4136233f818570a2d2876", 396634},
    {"kernels-portable/pack.nrx", "d41b9a87806d8529f18c898a83c4e4a83e720b3e1cafbece7daf06175fa622a2", 521592},
    {"nr_fg.ini", "68bc047729ecd40558a328b7a56510569ff71d6259eb932f6f18f513226f93d3", 603},
    {"nvngx_dlssg.dll", "138abe36a55003847dcf2b18c5e208138fb8e542b263cfeeb36613f362cee6f6", 3684136},
    {"path_table.json", "82992baaf5d2f0af85cec8f8ced8cbfd5d5d7626b99c1a29f3799964f2fb4452", 9514},
    {"programs/fg-lite-3840x2160-coopmat-nvidia-blackwell-pq10/pack.nrx", "83f05fdebe59467e0ed4e85b4777b0bd646232623ca03b8057ba8527a2fd9671", 45332},
    {"programs/fg-lite-3840x2160-coopmat-nvidia-blackwell-scrgb16/pack.nrx", "8843b1cc01f231c08e57c39396b2ed392e48ccffcc2ec92b2648dd8fb36ad7d6", 41914},
    {"programs/fg-lite-3840x2160-coopmat-nvidia-blackwell-srgb8/pack.nrx", "4bae89c91e8f372da1432f0aa0b42b88608c2cc616db06eaf256a5acbb407ba8", 42991},
    {"programs/fg-lite-any-coopmat-nvidia-blackwell-pq10/pack.nrx", "5fd83071cb83c596dd51db675521dc5ae667237a7d479660014bed8ca54e7a4c", 94351},
    {"programs/fg-lite-any-coopmat-nvidia-blackwell-scrgb16/pack.nrx", "5b0cab75f259f9c1177f29a7d4ea880584d134682110039d6e0ece208ad20074", 90633},
    {"programs/fg-lite-any-coopmat-nvidia-blackwell-srgb8/pack.nrx", "a202e4aeb646ae3b2b7bbef4a2a25f4fb3830049b6f4dd2e4a1464641ae3714c", 92111},
    {"programs/fg-lite-any-coopmat-pq10/pack.nrx", "db074df27e3468471f22c8bf895e08d42cc094bc317e9f25fb85ccddfdb999a2", 93900},
    {"programs/fg-lite-any-coopmat-scrgb16/pack.nrx", "bfa0c124366eb77b70a12578859774326136ea38d6f46731e12232cd38ff98aa", 90581},
    {"programs/fg-lite-any-coopmat-srgb8/pack.nrx", "cb2a0980c6eeb4428b00942a3f37a6f691be3bd5f353f97e144c27094f005def", 91677},
    {"programs/fg-lite-any-dp4a-pq10/pack.nrx", "64d3d6376456811b6bdd9774bfd7863af8b43ff4336b13a50c6fc03de916dc7e", 81428},
    {"programs/fg-lite-any-dp4a-scrgb16/pack.nrx", "ac13a9c8a7622e3d0c036b4e04b0fb3dcd4fde5aeca3baae0a9aa592ab21053e", 77842},
    {"programs/fg-lite-any-dp4a-srgb8/pack.nrx", "2b9a3cd500246c40cf1a4604ab0f6c19d666c09ed59ebbec29ea9d8f3838aea7", 79020},
    {"programs/fg-lite-any-packed-pq10/pack.nrx", "ee3fcc13a2862ed55fe2993c1d9e06cd4d13495f7a5ecf9d66b6ff071ed886db", 64591},
    {"programs/fg-lite-any-packed-scrgb16/pack.nrx", "471da1067a25e58e4cc97f5fd0fb4e45222ee4ce22db5fb86cdb7c5e36ac400a", 61115},
    {"programs/fg-lite-any-packed-srgb8/pack.nrx", "8f80cadf7c46adab11d3523d31b5db69f78b32d0f42ab2ce701c6ec73580855f", 62374},
    {"programs/fg-lite-any-portable-linalg-pq10/pack.nrx", "f3a4af5bc2e84805e3112705cc57096181e8eaf8bd394b7a1e397c4ead5ae7fd", 85260},
    {"programs/fg-lite-any-portable-linalg-scrgb16/pack.nrx", "b3eea11f2c6fb8be6d435af02e719036daf238ba5d7769535b264735cf320517", 81798},
    {"programs/fg-lite-any-portable-linalg-srgb8/pack.nrx", "8f4bfb45f64b3ea4e262620aef60782f6204da6930d05b5b0c36e4c5c97db1ad", 82908},
    {"programs/fg-lite-any-portable-pq10/pack.nrx", "aed2004ca34c33ee28c5f91e83569dd5aa246284d363f08cb7ba12700cc36891", 66743},
    {"programs/fg-lite-any-portable-scrgb16/pack.nrx", "fe481c40690099298f7a63d3c24ed5dea832db37142f5a08c9a37536de916ea0", 63436},
    {"programs/fg-lite-any-portable-srgb8/pack.nrx", "4957dac1fa83cadb96743e05646a8e1e08cfc08b3e2ad27d1640a588f9e5d43d", 64512},
    {"programs/fg-lite-any-portable-xmx-pq10/pack.nrx", "f5f3a3afc8a6a0a4acfe2943511444eaeca1b1ae68b96d85e7a8d38ed9f30cea", 90682},
    {"programs/fg-lite-any-portable-xmx-srgb8/pack.nrx", "f744f66f6785c633657a2c66e8c09d9fde739f4199e401469543549b2e9e520b", 88268},
    {"size_table.json", "7c602f6709e14bbf972250d47b8b3dc55ce4dafd803723561120097e644e5478", 958},
};
constexpr ModelAsset anrAsset{
    .destination = ngx::AnrProvider::downloadVersion,
    .url = "https://github.com/apex-compute/ANR/releases/download/FG-V.B20/anr-fg-V.B20-addon.zip",
    .bytes = 4130422,
    .sha256 = "750885d982b0e58e815004da698999f3b85f20c84a28c33cc15ae54e555f70ae",
    .members = anrFiles,
    .archivePrefix = "Apex Neural Rendering FG/dlssg/",
    .stripComponents = 2,
    .verifyExisting = true,
    // The provider creates a Vulkan bridge and logs beside its input packs.
    .allowAdditionalFiles = true,
};
}

QVariantMap QmlPlayerBridge::anrDownload() const {
    const auto* installer = impl_->anrInstaller;
    if (!installer) return {};
    return {
        {"active", installer->downloading()},
        {"percent", int(installer->received() * 100 / anrAsset.bytes)},
        {"status", installer->status()},
    };
}

void QmlPlayerBridge::downloadAnr() {
    const auto cap = currentEffectCapabilities(true).value(QStringLiteral("anr")).toMap();
    const bool update = cap.value("updateAvailable").toBool();
    if (cap.value("available").toBool() && !update) {
        addEffect(QStringLiteral("anr-fg"));
        return;
    }
    if (!cap.value("downloadable").toBool() && !update) {
        emit notice(cap.value("reason").toString(), true);
        return;
    }

    auto& installer = impl_->anrInstaller;
    if (installer && installer->downloading()) return;
    if (installer) installer->deleteLater();
    installer = new ModelAssets(QString::fromStdWString(ngx::AnrProvider::downloadRoot().wstring()),
        std::span(&anrAsset, 1));
    installer->setParent(this);
    impl_->anrInstallCancelled = false;
    const auto backend = settings().frameGenerationBackend;
    const auto enabled = fgEnabled();
    const auto mode = impl_->chain.mode;
    connect(installer, &ModelAssets::changed, this, [this, job = installer, backend, enabled, mode, update] {
        if (job != impl_->anrInstaller) return;
        emit anrDownloadChanged();
        if (job->downloading()) return;
        if (!job->installed()) {
            emit notice(QStringLiteral("ANR FG · ") + job->status(), !impl_->anrInstallCancelled);
            return;
        }
        // The periodic capability probe can still hold the pre-install
        // result. Refresh it before the activation checks that same map.
        (void)currentEffectCapabilities(true);
        emit effectCapabilitiesChanged();
        refreshComponentList();
        // An update keeps the current effect and is picked up on the next provider open.
        // For a first install, a later backend, switch or editor choice takes precedence.
        if (!update && settings().frameGenerationBackend == backend && fgEnabled() == enabled && impl_->chain.mode == mode &&
            addEffect(QStringLiteral("anr-fg")) < 0) return;
        emit notice(tr("ANR FG 组件已就绪"), false);
    });
    installer->download();
}

void QmlPlayerBridge::cancelAnrDownload() {
    if (auto* installer = impl_->anrInstaller; installer && installer->downloading()) {
        impl_->anrInstallCancelled = true;
        installer->cancel();
    }
}
}

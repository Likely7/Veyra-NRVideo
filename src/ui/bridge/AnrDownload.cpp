// SPDX-License-Identifier: GPL-3.0-only
#include "../QmlPlayerBridge_p.h"
#include "../TranslationAssets.h"
#include "veyra/ngx/AnrProvider.h"

namespace veyra::ui {
namespace {
// Apex Compute's B15 Windows Addon. Import only the provider's directory;
// the OptiScaler/game installers in the archive are never extracted.
constexpr ModelFile anrFiles[]{
    {"nr_fg.ini", "68bc047729ecd40558a328b7a56510569ff71d6259eb932f6f18f513226f93d3", 603},
    {"nvngx_dlssg.dll", "ae7424b87d13a0e3a9a99dd02d1ea35b9de96c2caa9cd4300d73bac5bb0cf4e8", 3706880},
    {"path_table.json", "6f6443928936103c311e48c0b2faa9627b71f19ac41c678bb3e495a91e48b4f4", 8283},
    {"size_table.json", "7c602f6709e14bbf972250d47b8b3dc55ce4dafd803723561120097e644e5478", 958},
    {"adapters/pack.nrx", "136ce0356c78f05020afebe8dcaeda4c076c2dab72174f88d2b75fa61cf0bb9e", 35316},
    {"kernels/pack.nrx", "a65583fb682cd4109e3b0dfe5d8d07b71c99186f031189e51abc1825e55bea2c", 452790},
    {"kernels-portable/pack.nrx", "f5642e9a2d3c0b3203a540506175200a6946e68c56f7c5b810f8564b1464dc2c", 449868},
    {"programs/fg-lite-any-coopmat-pq10/pack.nrx", "96588c8922f23bdee9b43e19038e5f7b9056b3fc343c67f03e7caa06ebbf8b29", 84607},
    {"programs/fg-lite-any-coopmat-srgb8/pack.nrx", "7ff31cb45dc1d771c774399ce54411e65508005267ccdefc37f6cae2465832e4", 82299},
    {"programs/fg-lite-any-dp4a-pq10/pack.nrx", "602173add33160bfb36d6c7e215c5104c7c5282baca7c62aa68f25336d51c38d", 65883},
    {"programs/fg-lite-any-dp4a-srgb8/pack.nrx", "2e8b5a996753a5bb2f0ced680d6f68e296ba725c7bd633be8bb0ce7f101d8629", 63441},
    {"programs/fg-lite-any-packed-pq10/pack.nrx", "6d6983070aa0c4e78598dafe3453e9287a1992a05971ab6925998726722a646b", 58120},
    {"programs/fg-lite-any-packed-srgb8/pack.nrx", "36c3cad7d4f7dada76c818344d5f8a9cd6150ce76a112a0203004a5fce3fff83", 55932},
    {"programs/fg-lite-any-portable-linalg-pq10/pack.nrx", "42e8269e03e6476f541918e0176dfe28032d87f332c2b6bd3d6b231a3b3f8cf6", 81298},
    {"programs/fg-lite-any-portable-linalg-srgb8/pack.nrx", "6562aa1c1bdebcdbb83e683e2266fdb35de6778d78418097209d8cd3a2665ab3", 78861},
    {"programs/fg-lite-any-portable-pq10/pack.nrx", "36726f37da1da8d73f045855191d0275cb6c841517217bf590360ddc2759a01e", 60727},
    {"programs/fg-lite-any-portable-srgb8/pack.nrx", "77939d7a2f9320bf31cd326a03c1d36f878a3dc7ae45c3eac616171f8d61cc73", 58543},
    {"programs/fg-lite-any-portable-xmx-pq10/pack.nrx", "ecce6efb5c81bc30675d9ebb249c6e6b4b5c0118583ef18c337096c6b3efcf8a", 81253},
    {"programs/fg-lite-any-portable-xmx-srgb8/pack.nrx", "94fb8bf5715aeeacecad7eaf685b2115182c12dc7c7e69fd746aa48a5d1c2bdd", 78995},
};
constexpr ModelAsset anrAsset{
    .destination = ngx::AnrProvider::downloadVersion,
    .url = "https://github.com/apex-compute/ANR/releases/download/FG-V.B15/anr-fg-V.B15-addon.zip",
    .bytes = 3231158,
    .sha256 = "717e93b3e63cb08dbaf6240cd585bae94d498c2749d8603d800d17ad97ef9e41",
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
    if (cap.value("available").toBool()) {
        addEffect(QStringLiteral("anr-fg"));
        return;
    }
    if (!cap.value("downloadable").toBool()) {
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
    connect(installer, &ModelAssets::changed, this, [this, job = installer, backend, enabled, mode] {
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
        // A later backend, switch or editor choice supersedes the original
        // click. The downloaded provider stays available in either mode.
        if (settings().frameGenerationBackend == backend && fgEnabled() == enabled && impl_->chain.mode == mode &&
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

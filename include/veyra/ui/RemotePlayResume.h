// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QVariantMap>
#include <algorithm>

namespace veyra::ui {
// Store the transport and destination in the same atomic last-source record.
// Only nonsecret, whitelisted connection preferences belong here.
inline QVariantMap remotePlayResumeForm(const QVariantMap& form, const QString& accountId = {}) {
    QVariantMap saved;
    for (const auto* key : {"connection", "psnDuid", "quality", "codec", "bitrate", "decode", "sampling", "viewOnly"})
        saved[QLatin1String(key)] = form.value(QLatin1String(key));
    if (form.value("connection") == QLatin1String("psn")) saved["accountId"] = accountId;
    return saved;
}

inline bool applyRemotePlayResume(const QVariantMap& saved, QVariantMap& form, const QString& accountId = {}) {
    const auto mode = saved.value("connection").toString();
    if (mode != QLatin1String("local") && mode != QLatin1String("psn")) return false;
    QVariantMap restored = form;
    restored["connection"] = mode;
    if (mode == QLatin1String("psn")) {
        // The primary-PS4 DUID is all zeros for every account, not a unique console.
        if (accountId.isEmpty() || saved.value("accountId").toString() != accountId) return false;
        const auto id = saved.value("psnDuid").toString().toLower();
        if (id.size() != 64 || !std::all_of(id.begin(), id.end(), [](QChar c) {
                return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
            })) return false;
        restored["psnDuid"] = id;
        struct Range { const char* key; int maximum; };
        for (const auto range : {Range{"quality", 3}, Range{"codec", 2}, Range{"bitrate", 7}, Range{"decode", 2}, Range{"sampling", 1}}) {
            bool ok = false;
            const int value = saved.value(QLatin1String(range.key)).toInt(&ok);
            if (!ok || value < 0 || value > range.maximum) return false;
            restored[QLatin1String(range.key)] = value;
        }
        if (saved.value("viewOnly").metaType().id() != QMetaType::Bool) return false;
        restored["viewOnly"] = saved.value("viewOnly");
    }
    form = std::move(restored);
    return true;
}
}

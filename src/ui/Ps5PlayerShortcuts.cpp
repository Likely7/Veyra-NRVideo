#include "veyra/ui/Ps5KeyboardSettings.h"
#include "veyra/ui/Ps5PlayerKeys.h"
#include <QJsonDocument>
#include <QSet>

namespace veyra::ui {
namespace {
QString playerKey(const QJsonObject& store, const QString& action) {
    const QString fallback = action == "overlay" ? "F1" : "F3";
    return store.value(action + "Key").toString(fallback);
}

struct Assignment {
    QStringList table;
    QString entry;
    QString action;
    QString key;
    bool unbound;
};

// Keep the original table and gesture for each binding. One key can have
// distinct actions on foot, in a vehicle, in a layer, and in the console menu.
QList<Assignment> assignments(const QJsonObject& profile) {
    QList<Assignment> result;
    const auto outputs = profile.value("outputs").toObject();
    auto append = [&](QStringList path, const QJsonObject& table) {
        const bool hotkeys = path.first() == "context_hotkeys";
        for (auto it = table.begin(); it != table.end(); ++it) {
            const auto action = hotkeys ? it.key() : it.value().toString();
            const auto key = hotkeys ? it.value().toString() : it.key().section(':', 0, 0);
            const bool unbound = !hotkeys && path.first() != "console_menu" && !it.key().contains(":layer") &&
                outputs.value(action).toObject().value("special").toString() == "none";
            result.append({path, it.key(), action, key, unbound});
        }
    };
    for (const QString table : {"bindings", "context_hotkeys", "console_menu"})
        append({table}, profile.value(table).toObject());
    const auto situations = profile.value("bindings_per_context").toObject();
    for (auto it = situations.begin(); it != situations.end(); ++it)
        append({"bindings_per_context", it.key()}, it.value().toObject());
    const auto layers = profile.value("layers").toObject();
    for (auto it = layers.begin(); it != layers.end(); ++it)
        append({"layers", it.key(), "bindings"}, it.value().toObject().value("bindings").toObject());
    return result;
}

QVariantMap describe(const Assignment& binding, const QJsonObject& profile) {
    const auto scope = binding.table.first();
    const auto context = scope == "context_hotkeys" ? binding.entry : binding.table.value(1);
    const auto labels = scope == "layers" ? profile.value("layers").toObject().value(context).toObject().value("label")
        : profile.value("context_labels").toObject().value(context);
    const auto output = profile.value("outputs").toObject().value(binding.action).toObject();
    QVariantMap description{{"scope", scope}, {"context", context}, {"contextLabels", labels.toVariant()},
                            {"action", binding.action}, {"labels", output.value("label").toVariant()}};
    if (binding.entry.contains(":layer")) {
        const auto target = binding.entry.section('=', 1);
        const auto layer = profile.value("layers").toObject().value(target).toObject();
        description["layer"] = target;
        description["layerLabels"] = (layer.isEmpty() ? profile.value("context_labels").toObject().value(target)
                                                       : layer.value("label")).toVariant();
    }
    return description;
}

void moveAssignment(QJsonObject& object, const Assignment& binding, const QString& key, qsizetype depth = 0) {
    if (depth < binding.table.size()) {
        const auto& name = binding.table[depth];
        auto child = object.value(name).toObject();
        moveAssignment(child, binding, key, depth + 1);
        object[name] = child;
    } else if (binding.table.first() == "context_hotkeys") {
        object[binding.entry] = key;
    } else {
        const auto value = object.take(binding.entry);
        object.insert(key + binding.entry.mid(binding.key.size()), value);
    }
}
} // namespace

QString Ps5KeyboardSettings::overlayKey() const { return playerKey(store_, "overlay"); }
QString Ps5KeyboardSettings::controlKey() const { return playerKey(store_, "control"); }
QStringList Ps5KeyboardSettings::shortcutKeys() const { return ps5PlayerShortcutKeys(); }

bool Ps5KeyboardSettings::setShortcut(const QString& action, const QString& key) {
    if ((action != "overlay" && action != "control") || !shortcutKeys().contains(key))
        return fail(tr("请选择有效的播放器快捷键。"));
    auto next = store_;
    next[action + "Key"] = key;
    return stageChange(next, false, action);
}

bool Ps5KeyboardSettings::stageChange(QJsonObject next, bool editedCopy, const QString& firstAction) {
    if (hasShortcutConflict(next, editedCopy, firstAction)) return false;
    return commit(std::move(next), editedCopy);
}

bool Ps5KeyboardSettings::hasShortcutConflict(const QJsonObject& next, bool editedCopy, const QString& firstAction) {
    const auto profile = selectedProfile(next);
    const auto bindings = assignments(profile);
    const QStringList actions = firstAction == "control" ? QStringList{"control", "overlay"}
                                                         : QStringList{"overlay", "control"};
    for (const auto& action : actions) {
        const auto key = playerKey(next, action);
        const QString other = action == "overlay" ? "control" : "overlay";
        const bool shared = playerKey(next, other) == key;
        QVariantList matches;
        for (const auto& binding : bindings)
            if (binding.key == key && !binding.unbound) matches.append(describe(binding, profile));
        if (!shared && matches.isEmpty()) continue;

        QSet<QString> used{key, playerKey(next, other)};
        for (const auto& binding : bindings) used.insert(binding.key);
        QStringList free;
        for (const auto& candidate : shortcutKeys())
            if (!used.contains(candidate)) free.append(candidate);

        pendingStore_ = next;
        pendingEditedCopy_ = editedCopy;
        shortcutConflict_ = {{"action", action}, {"otherAction", shared ? other : QString{}},
                             {"key", key}, {"profile", next.value("selected").toString(profiles().first())},
                             {"assignments", matches}, {"freeKeys", free}};
        error_.clear();
        emit changed();
        return true;
    }
    return false;
}

bool Ps5KeyboardSettings::resolveShortcut(const QString& side, const QString& key) {
    if (shortcutConflict_.isEmpty()) return false;
    if (!shortcutConflict_.value("freeKeys").toStringList().contains(key))
        return fail(tr("请选择未分配的按键。"));
    const auto action = shortcutConflict_.value("action").toString();
    const auto other = shortcutConflict_.value("otherAction").toString();
    auto next = pendingStore_;
    if (side == "player" || (side == "other" && !other.isEmpty())) {
        next[(side == "player" ? action : other) + "Key"] = key;
        return stageChange(next, pendingEditedCopy_);
    }
    if (side != "game" || !other.isEmpty()) return false;

    const auto oldKey = shortcutConflict_.value("key").toString();
    auto doc = selectedProfile(next);
    const auto bindings = assignments(doc);
    for (const auto& binding : bindings)
        if (binding.key == oldKey) moveAssignment(doc, binding, key);
    if (!validate(QJsonDocument(doc).toJson(QJsonDocument::Compact))) return false;

    auto copies = next.value("copies").toObject();
    QString name = shortcutConflict_.value("profile").toString();
    if (name == profiles().first() || (name != selected() && profiles().contains(name))) {
        const auto base = tr("%1（个人副本）").arg(name);
        name = base;
        for (int suffix = 2; copies.contains(name); ++suffix)
            name = QStringLiteral("%1 (%2)").arg(base).arg(suffix);
    }
    copies[name] = doc;
    next["copies"] = copies;
    next["selected"] = name;
    return stageChange(next, true);
}

void Ps5KeyboardSettings::cancelShortcutConflict() {
    if (shortcutConflict_.isEmpty()) return;
    pendingStore_ = {};
    shortcutConflict_.clear();
    error_.clear();
    emit changed();
}
} // namespace veyra::ui

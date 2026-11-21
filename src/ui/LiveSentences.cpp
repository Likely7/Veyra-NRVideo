// SPDX-License-Identifier: GPL-3.0-only
#include "LiveSentences.h"

#include <QStringList>
#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

namespace veyra::ui {
namespace {
constexpr qsizetype maxSentence = 200;
constexpr qint64 stabilizationMs = 600;

bool endMark(QStringView text, qsizetype offset) {
    const QChar ch = text[offset];
    if (QStringView(u"?!…。？！\n").contains(ch)) return true;
    if (ch != u'.') return false;
    const bool last = offset + 1 == text.size();
    if (!last && !text[offset + 1].isSpace() && !QStringView(u"”’\"')」").contains(text[offset + 1]))
        return false; // Decimal or an internal dot of an abbreviation.
    qsizetype word = offset;
    while (word > 0 && (text[word - 1].isLetter() || text[word - 1] == u'.')) --word;
    const auto name = text.sliced(word, offset - word);
    constexpr std::array abbreviations{u"Mr", u"Mrs", u"Ms", u"Dr", u"Prof", u"St", u"Jr", u"Sr", u"vs", u"etc", u"e.g", u"i.e"};
    if (std::ranges::any_of(abbreviations, [&](QStringView abbr) {
            return abbr.compare(name, Qt::CaseInsensitive) == 0;
        })) return false;
    const bool initial = name.size() == 1 && name[0].isUpper();
    const bool acronym = name.size() > 2 && name.contains(u'.') && name.last(1)[0].isUpper() &&
        std::all_of(name.begin(), name.end(), [](QChar c) { return c == u'.' || c.isUpper(); });
    if (!initial && !acronym) return true;
    if (name == QStringView(u"I") && word > 0) {
        // "So am I." can end a sentence. "Dr. I. Brown" is an initial.
        const auto prefix = text.first(word).trimmed();
        qsizetype begin = prefix.size();
        while (begin && prefix[begin - 1].isLetter()) --begin;
        const auto previous = prefix.sliced(begin);
        constexpr std::array auxiliaries{u"am", u"was", u"do", u"did", u"have", u"had", u"will", u"would",
            u"can", u"could", u"may", u"might", u"must", u"should"};
        if (std::ranges::any_of(auxiliaries, [&](QStringView auxiliary) {
            return auxiliary.compare(previous, Qt::CaseInsensitive) == 0;
        })) return true;
    }

    auto following = text.sliced(offset + 1);
    while (!following.isEmpty() && QStringView(u"”’\"')」").contains(following[0])) following = following.sliced(1);
    if (following.startsWith(u'\n')) return true;
    following = following.trimmed();
    if (following.isEmpty()) return true;
    qsizetype length = 0;
    while (length < following.size() && following[length].isLetter()) ++length;
    const auto next = following.first(length);
    constexpr std::array starters{u"I", u"We", u"You", u"He", u"She", u"It", u"They", u"The", u"This", u"That",
        u"These", u"Those", u"Then", u"Next", u"However", u"But", u"Meanwhile"};
    return std::ranges::any_of(starters, [&](QStringView starter) { return starter == next; });
}

qsizetype splitAt(QStringView text, bool unstableEnd) {
    const qsizetype limit = std::min(maxSentence, text.size());
    for (qsizetype i = 0; i < limit; ++i) {
        if (!endMark(text, i)) continue;
        qsizetype end = i + 1;
        while (end < limit && QStringView(u".!?…。？！\"”’')」").contains(text[end])) ++end;
        if (!unstableEnd || text.sliced(end).trimmed().size() >= 6) return end;
    }
    if (text.size() <= maxSentence) return 0;
    for (qsizetype i = limit; i > maxSentence / 2; --i) {
        if (text[i - 1].isSpace() || QStringView(u",，、;；").contains(text[i - 1])) return i;
    }
    return text[limit - 1].isHighSurrogate() ? limit - 1 : limit;
}

qsizetype commonPrefix(QStringView a, QStringView b) {
    const qsizetype n = std::min(a.size(), b.size());
    qsizetype i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

// Live Captions retains a rolling set of lines. Windows may rewrite the
// trailing words or remove the oldest lines without giving events or IDs.
qsizetype retainedOffset(const QString& previous, const QString& next, qsizetype committed) {
    if (previous.isEmpty() || next.isEmpty()) return 0;
    const auto shared = commonPrefix(previous, next);
    if (shared >= committed) return committed;
    const auto probe = QStringView(next).first(std::min<qsizetype>(12, next.size()));
    qsizetype alignment = 0;
    qsizetype matched = shared;
    for (qsizetype offset = previous.indexOf(probe, 1); offset >= 0;
         offset = previous.indexOf(probe, offset + 1)) {
        const auto overlap = commonPrefix(QStringView(previous).sliced(offset), next);
        if (overlap > matched) { alignment = offset; matched = overlap; }
    }
    if (alignment) return std::clamp<qsizetype>(committed - alignment, 0, next.size());
    // A revision within a committed sentence cannot revoke that sentence.
    if (shared >= 12) return std::min(committed, next.size());
    return 0;
}
}

std::vector<LiveSentence> LiveSentences::appendFinal(const QString& text, const QString& language, qint64 endUs, qint64 nowMs) {
    std::vector<LiveSentence> sentences;
    if (!pending_.isEmpty() && language_ != language) sentences = take(true, false);
    fromSnapshot_ = false;
    if (!pending_.isEmpty()) pending_ += u' ';
    pending_ += text.trimmed();
    language_ = language;
    endUs_ = endUs;
    changedAt_ = nowMs;
    auto completed = take(false, false);
    sentences.insert(sentences.end(), std::make_move_iterator(completed.begin()), std::make_move_iterator(completed.end()));
    return sentences;
}

std::vector<LiveSentence> LiveSentences::updateSnapshot(const QString& text, const QString& language, qint64 endUs, qint64 nowMs) {
    if (text == snapshot_) return {};
    consumed_ = retainedOffset(snapshot_, text, consumed_);
    snapshot_ = text;
    pending_ = text.mid(consumed_);
    language_ = language;
    endUs_ = endUs;
    changedAt_ = nowMs;
    fromSnapshot_ = true;
    return take(false, true);
}

std::vector<LiveSentence> LiveSentences::flush(qint64 nowMs) {
    return nowMs - changedAt_ >= stabilizationMs ? take(true, false) : std::vector<LiveSentence>{};
}

void LiveSentences::reset(const QString& baseline) {
    snapshot_ = baseline;
    consumed_ = baseline.size();
    pending_.clear();
    language_.clear();
    changedAt_ = 0;
    endUs_ = 0;
    fromSnapshot_ = false;
}

std::vector<LiveSentence> LiveSentences::take(bool flushTail, bool unstableEnd) {
    std::vector<LiveSentence> result;
    while (!pending_.isEmpty()) {
        qsizetype end = splitAt(pending_, unstableEnd);
        if (!end) {
            if (!flushTail) break;
            end = pending_.size();
        }
        const QString phrase = pending_.first(end).simplified();
        pending_.remove(0, end);
        if (fromSnapshot_) consumed_ += end;
        if (std::any_of(phrase.begin(), phrase.end(), [](QChar ch) { return ch.isLetterOrNumber(); }))
            result.push_back({phrase, language_, endUs_});
    }
    return result;
}
}

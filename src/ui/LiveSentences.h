// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <vector>

namespace veyra::ui {
struct SpeechMark {
    qsizetype offset = 0;
    qint64 endUs = 0;
};

struct LiveSentence {
    QString text;
    QString language;
    qint64 endUs = 0;
    qint64 wordEndUs = 0;
};

class LiveSpeechText {
public:
    struct Update {
        QString committed;
        QString draft;
        qsizetype offset = 0;
    };

    Update update(quint64 segment, qint64 beginUs, qint64 endUs, const QString& text, bool final);
    void reset();

private:
    quint64 segment_ = 0;
    qint64 previousFinalEndUs_ = 0;
    QString previousFinal_, previous_, committed_;
};

class LiveSentences {
public:
    std::vector<LiveSentence> appendFinal(const QString& text, const QString& language, qint64 endUs,
                                         qint64 nowMs, const std::vector<SpeechMark>& marks = {});
    std::vector<LiveSentence> updateSnapshot(const QString& text, const QString& language, qint64 endUs, qint64 nowMs);
    std::vector<LiveSentence> flush(qint64 nowMs);
    void reset(const QString& baseline = {});

private:
    std::vector<LiveSentence> take(bool flushTail, bool unstableEnd);
    QString snapshot_;
    qsizetype consumed_ = 0;
    QString pending_;
    std::vector<SpeechMark> pendingMarks_;
    QString language_;
    qint64 endUs_ = 0;
    qint64 changedAt_ = 0;
    bool fromSnapshot_ = false;
};
}

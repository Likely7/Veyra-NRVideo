// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <vector>

namespace veyra::ui {
struct LiveSentence {
    QString text;
    QString language;
    qint64 endUs = 0;
};

class LiveSentences {
public:
    std::vector<LiveSentence> appendFinal(const QString& text, const QString& language, qint64 endUs, qint64 nowMs);
    std::vector<LiveSentence> updateSnapshot(const QString& text, const QString& language, qint64 endUs, qint64 nowMs);
    std::vector<LiveSentence> flush(qint64 nowMs);
    void reset(const QString& baseline = {});

private:
    std::vector<LiveSentence> take(bool flushTail, bool unstableEnd);
    QString snapshot_;
    qsizetype consumed_ = 0;
    QString pending_;
    QString language_;
    qint64 endUs_ = 0;
    qint64 changedAt_ = 0;
    bool fromSnapshot_ = false;
};
}

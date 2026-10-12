#include "FlashProgressParser.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>

void FlashProgressParser::reset() {
    m_operations.clear();
    m_currentOp.clear();
}

QString FlashProgressParser::activityName(const QString& op) {
    if (op == "erase") return "Erasing";
    if (op == "program") return "Programming";
    if (op == "verify") return "Verifying";
    if (op == "read") return "Reading";
    if (op == "fill") return "Restoring";
    return op;
}

// The share of work done over all announced operations. -1 while nothing was announced.
int FlashProgressParser::overallPercent() const {
    qint64 total = 0;
    qint64 done = 0;
    for (const Operation& op : m_operations) {
        total += op.total;
        done += std::min(op.done, op.total);  // never count more than was announced
    }
    if (total <= 0) return -1;
    // 100 is left for the final result, so the bar is not full while a step is still running.
    return static_cast<int>(std::min<qint64>(99, done * 100 / total));
}

FlashHelperEvent FlashProgressParser::feed(const QByteArray& line) {
    FlashHelperEvent result;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return result;

    const QJsonObject object = document.object();
    const QString event = object.value("event").toString();
    const QString op = object.value("op").toString();

    if (event == "plan") {
        // An operation can be announced several times (one per flash area): the totals add up.
        const QJsonValue total = object.value("total");
        if (total.isDouble()) m_operations[op].total += static_cast<qint64>(total.toDouble());
        return result;
    }

    if (event == "started" || event == "progress" || event == "finished") {
        m_currentOp = op;
        if (event == "progress") m_operations[op].done += static_cast<qint64>(object.value("bytes").toDouble());
        const int percent = overallPercent();
        if (percent < 0) return result;  // nothing to show without a known total
        result.kind = FlashHelperEvent::Kind::Progress;
        result.percent = percent;
        result.text = QString("%1 %2%").arg(activityName(m_currentOp)).arg(percent);
        return result;
    }

    if (event == "message") {
        result.kind = FlashHelperEvent::Kind::Message;
        result.text = object.value("text").toString();
        return result;
    }

    if (event == "result") {
        result.kind = FlashHelperEvent::Kind::Result;
        result.ok = object.value("ok").toBool();
        result.text = object.value("error").toString();
        return result;
    }

    // "failed" for a single operation is followed by a "result" with the error text.
    return result;
}

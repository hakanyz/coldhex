#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

// What one line of coldhex-flash output means for the program.
struct FlashHelperEvent {
    enum class Kind {
        Ignored,   // not a line of the helper (or one that changes nothing visible)
        Progress,  // `percent` and `text` are set
        Message,   // a note from the flash algorithm in `text`
        Result     // the helper is done: `ok`, and `text` holds the error when it failed
    };

    Kind kind = Kind::Ignored;
    int percent = -1;
    QString text;
    bool ok = false;
};

// Reads the JSON lines of the coldhex-flash helper (see tools/coldhex-flash/src/main.rs) and
// turns them into one overall percentage. Every operation (erase, program, verify) announces how
// many bytes it will handle and then reports the bytes it finished; the percentage is the sum
// of finished bytes over the sum of all announced bytes.
class FlashProgressParser {
public:
    // Handles one line. Anything that is not a JSON object of the helper gives Kind::Ignored.
    FlashHelperEvent feed(const QByteArray& line);

    // Forget everything: call before a new command starts.
    void reset();

private:
    struct Operation {
        qint64 total = 0;
        qint64 done = 0;
    };

    int overallPercent() const;
    static QString activityName(const QString& op);

    QMap<QString, Operation> m_operations;
    QString m_currentOp;
};

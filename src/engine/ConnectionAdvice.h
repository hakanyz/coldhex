#pragma once

#include <QString>

// Turns the output of a failed probe-rs command into advice a person can act on.
class ConnectionAdvice {
public:
    // Returns a short title and bullet lines (separated by '\n'), or an empty string when the
    // output does not match anything known.
    static QString explain(const QString& probeRsOutput);

    // Advice for a connection that failed without a recognisable message.
    static QString generic();
};

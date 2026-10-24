#pragma once

#include "core/ProfileTransfer.h"
#include <QList>
#include <QStringList>

class QWidget;

// The two small windows of Import Profiles and Export Profiles.
class ProfileTransferDialog {
public:
    // Lets the user pick which profiles to export. Returns the chosen names (empty when cancelled).
    static QStringList askExport(QWidget* parent, const QStringList& names, bool& includePaths);

    enum class Conflict {
        Replace,   // the profile in the file takes the place of the stored one
        KeepBoth,  // the imported one gets a new name, "name (2)"
        Skip       // the stored one stays, the imported one is not used
    };

    struct ImportChoice {
        QList<NamedProfile> profiles;  // the ones the user ticked
        Conflict conflict = Conflict::KeepBoth;
    };

    // Shows the profiles found in a file; the ones whose name is in `existing` are marked.
    // Returns false when cancelled or nothing is ticked.
    static bool askImport(QWidget* parent, const QList<NamedProfile>& profiles,
                          const QStringList& existing, ImportChoice& choice);
};

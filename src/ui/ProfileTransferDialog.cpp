#include "ProfileTransferDialog.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {
// A list with a tick box in front of each row; all rows start ticked.
QListWidget* makeCheckList(QDialog* dialog, const QStringList& texts, const QStringList& tooltips = {}) {
    auto* list = new QListWidget(dialog);
    for (int i = 0; i < texts.size(); ++i) {
        auto* item = new QListWidgetItem(texts[i], list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        if (i < tooltips.size()) item->setToolTip(tooltips[i]);
    }
    return list;
}

QList<int> ticked(const QListWidget* list) {
    QList<int> rows;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->checkState() == Qt::Checked) rows << i;
    }
    return rows;
}

// Keeps the OK button off while nothing is ticked.
void enableOkWhenSomethingIsTicked(QListWidget* list, QPushButton* ok) {
    auto update = [list, ok]() { ok->setEnabled(!ticked(list).isEmpty()); };
    QObject::connect(list, &QListWidget::itemChanged, list, update);
    update();
}
}

QStringList ProfileTransferDialog::askExport(QWidget* parent, const QStringList& names, bool& includePaths) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Export Profiles");
    dialog.setMinimumWidth(360);
    auto* layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel("Profiles to export:", &dialog));
    QListWidget* list = makeCheckList(&dialog, names);
    layout->addWidget(list);

    auto* paths = new QCheckBox("Include firmware file paths", &dialog);
    paths->setChecked(false);
    paths->setToolTip("A path belongs to this computer and can contain your user name.\n"
                      "A file in the same folder as the exported file or below it is saved relative to it.");
    layout->addWidget(paths);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Export...");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    enableOkWhenSomethingIsTicked(list, buttons->button(QDialogButtonBox::Ok));

    if (dialog.exec() != QDialog::Accepted) return {};
    includePaths = paths->isChecked();
    QStringList chosen;
    for (int row : ticked(list)) chosen << names[row];
    return chosen;
}

bool ProfileTransferDialog::askImport(QWidget* parent, const QList<NamedProfile>& profiles,
                                      const QStringList& existing, ImportChoice& choice) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Import Profiles");
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel("Profiles in the file:", &dialog));

    QStringList texts;
    QStringList tips;
    bool anyExists = false;
    for (const NamedProfile& n : profiles) {
        bool exists = false;
        for (const QString& e : existing) exists = exists || e.compare(n.name, Qt::CaseInsensitive) == 0;
        anyExists = anyExists || exists;
        texts << QString("%1   (%2)%3").arg(n.name, n.profile.chip, exists ? "   - already exists" : QString());
        tips << QString("%1, %2, %3 kHz").arg(n.profile.manufacturer, n.profile.interfaceName).arg(n.profile.speedKhz);
    }
    QListWidget* list = makeCheckList(&dialog, texts, tips);
    layout->addWidget(list);

    auto* group = new QGroupBox("If a profile with the same name already exists", &dialog);
    auto* groupLayout = new QVBoxLayout(group);
    auto* keepBoth = new QRadioButton("Keep both (the imported one is named \"name (2)\")", group);
    auto* replace = new QRadioButton("Replace the existing profile", group);
    auto* skip = new QRadioButton("Skip the imported profile", group);
    keepBoth->setChecked(true);
    groupLayout->addWidget(keepBoth);
    groupLayout->addWidget(replace);
    groupLayout->addWidget(skip);
    group->setEnabled(anyExists);
    layout->addWidget(group);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Import");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    enableOkWhenSomethingIsTicked(list, buttons->button(QDialogButtonBox::Ok));

    if (dialog.exec() != QDialog::Accepted) return false;

    choice.profiles.clear();
    for (int row : ticked(list)) choice.profiles << profiles[row];
    choice.conflict = replace->isChecked() ? Conflict::Replace : (skip->isChecked() ? Conflict::Skip : Conflict::KeepBoth);
    return !choice.profiles.isEmpty();
}

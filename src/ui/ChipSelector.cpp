#include "ChipSelector.h"
#include "core/ChipCatalog.h"
#include "engine/BundledChips.h"
#include "engine/ChipDescription.h"
#include <QComboBox>
#include <QCompleter>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTimer>

ChipSelector::ChipSelector(QWidget* dialogParent, QComboBox* manufacturerBox, QComboBox* modelBox,
                           IProgrammerEngine* engine)
    : QObject(dialogParent), m_parent(dialogParent), m_manufacturerBox(manufacturerBox),
      m_modelBox(modelBox), m_engine(engine), m_chipNames(new QStringListModel(this))
{
    // Until the chip list arrives the model box is a free-text field, so a saved chip name is
    // still usable.
    m_modelBox->setEditable(true);
    m_modelBox->setInsertPolicy(QComboBox::NoInsert);

    // Typing searches every known chip, not only those of the manufacturer chosen above.
    auto* completer = new QCompleter(m_chipNames, this);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setMaxVisibleItems(15);
    m_modelBox->setCompleter(completer);
    connect(completer, QOverload<const QString&>::of(&QCompleter::activated), this, &ChipSelector::onTextCommitted);
    connect(m_modelBox->lineEdit(), &QLineEdit::editingFinished, this, &ChipSelector::onTextCommitted);

    connect(m_manufacturerBox, &QComboBox::currentTextChanged, this, &ChipSelector::onManufacturerChanged);
    connect(m_engine, &IProgrammerEngine::chipListReady, this, &ChipSelector::onChipListReady);
}

void ChipSelector::requestChips() {
    m_engine->requestChipList();
}

void ChipSelector::restore(const QString& manufacturer, const QString& chip, const QStringList& customChipFiles) {
    m_pendingManufacturer = manufacturer;
    m_pendingChip = chip;
    m_customChipFiles = customChipFiles;
    m_modelBox->setEditText(chip);
}

QString ChipSelector::manufacturer() const {
    return m_manufacturerBox->currentText();
}

QString ChipSelector::chipText() const {
    return m_modelBox->currentText().trimmed();
}

const ChipEntry* ChipSelector::entry(const QString& chip) const {
    return ChipCatalog::findEntry(m_chips, chip);
}

QString ChipSelector::descriptionFileFor(const QString& chip) const {
    const ChipEntry* e = entry(chip);
    return e ? e->descriptionFile : QString();
}

// Returns false (after telling the user) when the chip name is not known to probe-rs.
bool ChipSelector::resolve(QString& chip) {
    chip = chipText();
    if (chip.isEmpty()) {
        QMessageBox::warning(m_parent, "No Chip Selected", "Please select or type a target chip.");
        return false;
    }

    QString canonical;
    if (ChipCatalog::find(m_chips, chip, &canonical)) {
        chip = canonical;
        return true;
    }

    // Without the list from probe-rs a typed name cannot be checked; let probe-rs judge it.
    if (!m_builtinChipsLoaded) return true;

    const QString msg = QString("'%1' is not a chip name known to probe-rs. Pick one from the list.").arg(chip);
    log("ERROR", msg);
    QMessageBox::warning(m_parent, "Unknown Chip", msg);
    return false;
}

void ChipSelector::onChipListReady(bool success, const QList<ChipEntry>& chips, const QString& error) {
    m_builtinChipsLoaded = success;
    if (success) {
        m_builtinChips = chips;
    } else {
        m_builtinChips.clear();
        log("ERROR", QString("Chip list unavailable: %1").arg(error));
        log("WARNING", "Chip names cannot be checked; type the exact probe-rs chip name.");
    }
    rebuildCatalog();
}

void ChipSelector::onManufacturerChanged(const QString& manufacturer) {
    if (m_chips.isEmpty() || manufacturer.isEmpty()) return;

    const QString current = m_modelBox->currentText();
    m_modelBox->clear();
    m_modelBox->addItems(ChipCatalog::chipNames(m_chips, manufacturer));

    // Keep the typed/selected chip if it belongs to this manufacturer, otherwise start empty.
    const int idx = m_modelBox->findText(current, Qt::MatchFixedString);
    if (idx >= 0) {
        m_modelBox->setCurrentIndex(idx);
    } else {
        m_modelBox->setCurrentIndex(-1);
        m_modelBox->clearEditText();
    }
}

// Merges the chips of probe-rs with those of the bundled and user-added chip description files
// and refreshes the manufacturer and model boxes.
void ChipSelector::rebuildCatalog() {
    m_chips = m_builtinChips;

    QStringList descriptionFiles = BundledChips::extract(":/chips",
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/chips");
    for (const QString& path : m_customChipFiles) {
        if (!descriptionFiles.contains(path)) descriptionFiles << path;
    }

    for (const QString& path : descriptionFiles) {
        QString family;
        QString error;
        const QStringList names = ChipDescription::variantNamesFromFile(path, &family, &error);
        if (names.isEmpty()) {
            log("ERROR", error);
            continue;
        }
        for (const QString& name : names) {
            m_chips.append({"Chip file: " + family, name, path});
        }
    }

    QStringList allNames;
    for (const ChipEntry& chip : m_chips) allNames << chip.name;
    m_chipNames->setStringList(allNames);

    if (m_chips.isEmpty()) {
        m_modelBox->setEditText(m_pendingChip);
        return;
    }

    const QString wantedMaker = m_pendingManufacturer.isEmpty() ? m_manufacturerBox->currentText() : m_pendingManufacturer;
    m_manufacturerBox->blockSignals(true);
    m_manufacturerBox->clear();
    m_manufacturerBox->addItems(ChipCatalog::manufacturers(m_chips));
    int makerIdx = m_manufacturerBox->findText(wantedMaker);
    if (makerIdx < 0) makerIdx = m_manufacturerBox->findText("STM32");
    m_manufacturerBox->setCurrentIndex(makerIdx >= 0 ? makerIdx : 0);
    m_manufacturerBox->blockSignals(false);

    const QString wantedChip = m_pendingChip.isEmpty() ? m_modelBox->currentText() : m_pendingChip;
    m_modelBox->setEditText(wantedChip);
    onManufacturerChanged(m_manufacturerBox->currentText());

    QString canonical;
    if (!wantedChip.isEmpty() && ChipCatalog::find(m_chips, wantedChip, &canonical)) {
        m_modelBox->setEditText(canonical);
    }
    m_pendingManufacturer.clear();
    m_pendingChip.clear();
}

void ChipSelector::addChipFile() {
    const QString path = QFileDialog::getOpenFileName(m_parent, "Add Chip Description File", "",
                                                      "probe-rs chip description (*.yaml *.yml);;All Files (*.*)");
    if (path.isEmpty()) return;

    if (m_customChipFiles.contains(path)) {
        log("INFO", "That chip description file is already added.");
        return;
    }

    QString family;
    QString error;
    const QStringList names = ChipDescription::variantNamesFromFile(path, &family, &error);
    if (names.isEmpty()) {
        log("ERROR", error);
        QMessageBox::warning(m_parent, "Invalid Chip Description", error);
        return;
    }

    m_customChipFiles << path;
    rebuildCatalog();
    // The only chip list message left: the user asked for this file, so say what it gave.
    log("INFO", QString("Added chip description '%1': %2 chips (%3).")
                    .arg(QFileInfo(path).fileName()).arg(names.size()).arg(family));
}

void ChipSelector::clearChipFiles() {
    if (m_customChipFiles.isEmpty()) return;
    m_customChipFiles.clear();
    log("INFO", "Custom chip description files removed.");
    rebuildCatalog();
}

// After a chip was picked or typed: if it belongs to another manufacturer than the one shown,
// switch the manufacturer box so the two always agree.
void ChipSelector::onTextCommitted() {
    QTimer::singleShot(0, this, [this]() {
        QString canonical;
        if (!ChipCatalog::find(m_chips, m_modelBox->currentText(), &canonical)) return;

        const QString shown = m_manufacturerBox->currentText();
        if (ChipCatalog::chipNames(m_chips, shown).contains(canonical)) {
            m_modelBox->setEditText(canonical);
            return;
        }

        const QString maker = ChipCatalog::manufacturerOf(m_chips, canonical);
        const int idx = m_manufacturerBox->findText(maker.isEmpty() ? ChipCatalog::allChipsLabel() : maker);
        if (idx >= 0) m_manufacturerBox->setCurrentIndex(idx);  // keeps the chip name
        m_modelBox->setEditText(canonical);
    });
}

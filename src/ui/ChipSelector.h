#pragma once

#include "engine/IProgrammerEngine.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QComboBox;
class QStringListModel;
class QWidget;

// The manufacturer and model boxes and everything behind them: the chips probe-rs knows, the
// chips of bundled and user-added chip description files, searching while typing, and checking
// that a chip name is valid before it is used.
class ChipSelector : public QObject {
    Q_OBJECT

public:
    ChipSelector(QWidget* dialogParent, QComboBox* manufacturerBox, QComboBox* modelBox, IProgrammerEngine* engine);

    // Asks the engine for the list of supported chips (the answer arrives asynchronously).
    void requestChips();

    // Applies values from the saved settings; they take effect once the chip list is known.
    void restore(const QString& manufacturer, const QString& chip, const QStringList& customChipFiles);

    // Switches to a chip right now (a profile was loaded). Works before the chip list is known too.
    void select(const QString& manufacturer, const QString& chip);

    QString manufacturer() const;
    QString chipText() const;
    QStringList customChipFiles() const { return m_customChipFiles; }

    // The chip typed or selected by the user, validated against the known chips. Returns false
    // after telling the user why when no valid chip is chosen. `chip` gets the engine's spelling.
    bool resolve(QString& chip);

    // The catalog entry of a chip (null when unknown); its descriptionFile is set for chips
    // that come from a chip description file.
    const ChipEntry* entry(const QString& chip) const;
    QString descriptionFileFor(const QString& chip) const;

    // Menu actions.
    void addChipFile();
    void clearChipFiles();

signals:
    // Something worth writing to the console: level ("INFO", "ERROR"...) and text.
    void message(const QString& level, const QString& text);

private slots:
    void onChipListReady(bool success, const QList<ChipEntry>& chips, const QString& error);
    void onManufacturerChanged(const QString& manufacturer);
    void onTextCommitted();

private:
    void rebuildCatalog();
    void log(const QString& level, const QString& text) {
        emit message(level, text);
    }

    QWidget* m_parent;
    QComboBox* m_manufacturerBox;
    QComboBox* m_modelBox;
    IProgrammerEngine* m_engine;
    QStringListModel* m_chipNames;

    QList<ChipEntry> m_builtinChips;  // reported by probe-rs
    QList<ChipEntry> m_chips;         // built-in plus chip description files
    QStringList m_customChipFiles;
    bool m_builtinChipsLoaded = false;

    QString m_pendingManufacturer;  // restored from settings, applied once the chip list arrives
    QString m_pendingChip;
};

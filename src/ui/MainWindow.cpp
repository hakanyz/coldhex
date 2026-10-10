#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "core/HexParser.h"
#include "core/AppSettings.h"
#include "engine/ChipDescription.h"
#include "engine/BundledChips.h"
#include "engine/ProbeRsLocator.h"
#include "SystemCheckDialog.h"
#include <QTimer>
#include <QStandardPaths>
#include <QCloseEvent>
#include <QEvent>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QCompleter>
#include <QLineEdit>
#include <QStringListModel>
#include <QSettings>
#include <QFileDialog>
#include <QDateTime>
#include <QMessageBox>
#include <QFileInfo>
#include <QHeaderView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_engine(new ProbeRsEngine(this)),
      m_coordinator(new FlashCoordinator(m_engine, this)),
      m_memoryModel(new HexMemoryModel(this))
{
    ui->setupUi(this);
    setAcceptDrops(true);

    setupUiCustomizations();
    setupToolBarWidgets();
    setupDeviceDatabase();

    // Engine Signals
    connect(m_engine, &ProbeRsEngine::logMessage, this, &MainWindow::onEngineLogMessage);
    connect(m_engine, &ProbeRsEngine::connected, this, &MainWindow::onEngineConnected);
    connect(m_engine, &ProbeRsEngine::disconnected, this, &MainWindow::onEngineDisconnected);
    connect(m_engine, &ProbeRsEngine::chipListReady, this, &MainWindow::onChipListReady);
    connect(m_engine, &ProbeRsEngine::probeListReady, this, &MainWindow::onProbeListReady);
    connect(m_engine, &ProbeRsEngine::operationFinished, this, &MainWindow::onEngineOperationFinished);

    // Coordinator Signals
    connect(m_coordinator, &FlashCoordinator::sequenceStarted, this, &MainWindow::onSequenceStarted);
    connect(m_coordinator, &FlashCoordinator::sequenceFinished, this, &MainWindow::onSequenceFinished);
    connect(m_coordinator, &FlashCoordinator::stepChanged, this, &MainWindow::onStepChanged);
    connect(m_coordinator, &FlashCoordinator::totalProgressChanged, this, &MainWindow::onTotalProgressChanged);
    connect(m_coordinator, &FlashCoordinator::autoSpeedFallbackTriggered, this, &MainWindow::onAutoSpeedFallbackTriggered);

    // Toolbar Actions
    connect(ui->actionConnect, &QAction::triggered, this, &MainWindow::onConnectTriggered);
    connect(ui->actionDisconnect, &QAction::triggered, this, &MainWindow::onDisconnectTriggered);
    connect(ui->actionRead, &QAction::triggered, this, &MainWindow::onReadTargetTriggered);
    connect(ui->actionSaveLog, &QAction::triggered, this, &MainWindow::onSaveLogTriggered);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAboutTriggered);
    connect(ui->actionErase, &QAction::triggered, this, &MainWindow::onEraseTriggered);
    connect(ui->actionProgram, &QAction::triggered, this, &MainWindow::onProgramTriggered);
    connect(ui->actionVerify, &QAction::triggered, this, &MainWindow::onVerifyTriggered);
    connect(ui->actionResetTarget, &QAction::triggered, this, &MainWindow::onResetTargetTriggered);
    connect(ui->actionOpenHex, &QAction::triggered, this, &MainWindow::onOpenHexTriggered);
    connect(ui->actionClearLog, &QAction::triggered, ui->textLog, &QPlainTextEdit::clear);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionRefreshProbes, &QAction::triggered, this, &MainWindow::onRefreshProbesTriggered);
    connect(ui->actionAddChipFile, &QAction::triggered, this, &MainWindow::onAddChipFileTriggered);
    connect(ui->actionSystemCheck, &QAction::triggered, this, &MainWindow::onSystemCheckTriggered);
    connect(ui->actionCancel, &QAction::triggered, this, &MainWindow::onCancelTriggered);
    connect(ui->actionClearChipFiles, &QAction::triggered, this, &MainWindow::onClearChipFilesTriggered);

    // Settings Panel Controls
    connect(ui->comboManufacturer, &QComboBox::currentTextChanged, this, &MainWindow::onManufacturerChanged);
    connect(ui->btnOpenSlot1, &QPushButton::clicked, this, &MainWindow::onOpenSlot1Clicked);
    connect(ui->btnOpenSlot2, &QPushButton::clicked, this, &MainWindow::onOpenSlot2Clicked);
    connect(ui->editSlot1Path, &QLineEdit::textChanged, this, &MainWindow::onSlot1PathChanged);
    connect(ui->editSlot2Path, &QLineEdit::textChanged, this, &MainWindow::onSlot2PathChanged);

    // Start address edits re-base binary files in the memory viewer
    connect(ui->editSlot1Address, &QLineEdit::editingFinished, this, [this]() { loadFileIntoSlot(1, ui->editSlot1Path->text()); });
    connect(ui->editSlot2Address, &QLineEdit::editingFinished, this, [this]() { loadFileIntoSlot(2, ui->editSlot2Path->text()); });

    // Data Source Radios
    connect(ui->radioSourceFile1, &QRadioButton::toggled, this, &MainWindow::onDataSourceChanged);
    connect(ui->radioSourceFile2, &QRadioButton::toggled, this, &MainWindow::onDataSourceChanged);
    connect(ui->radioSourceTarget, &QRadioButton::toggled, this, &MainWindow::onDataSourceChanged);

    // Model selection updates memory map title
    connect(ui->comboModel, &QComboBox::currentTextChanged, [this](const QString& model) {
        const QString name = model.trimmed();
        ui->lblMemoryMapTitle->setText(name.isEmpty() ? QString("Memory Map") : QString("Memory Map (%1) - Flash").arg(name));
    });

    onRefreshProbesTriggered();
    restoreSettings();
    m_engine->requestChipList();

    // After the window is up, check the setup and open the System Check only if something needs fixing.
    QTimer::singleShot(0, this, [this]() {
        auto* dialog = new SystemCheckDialog(currentProbeRsPath(), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        if (dialog->runCheck()) {
            dialog->open();
        } else {
            dialog->deleteLater();
        }
    });
    appendLog("INFO", "ColdHex v0.1 started.");
    updateUiState();
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::restoreSettings() {
    QSettings settings;
    const UiSettings s = AppSettings::load(settings);
    if (!s.hasSaved) return;

    auto selectIfPresent = [](QComboBox* combo, const QString& text) {
        int idx = combo->findText(text);
        if (idx >= 0) combo->setCurrentIndex(idx);
    };

    m_pendingManufacturer = s.manufacturer;
    m_pendingChip = s.chip;
    m_customChipFiles = s.customChipFiles;
    ui->comboModel->setEditText(s.chip);
    selectIfPresent(ui->comboProbe, s.probe);
    selectIfPresent(ui->comboInterface, s.interfaceName);
    ui->spinSpeed->setValue(qBound(ui->spinSpeed->minimum(), s.speedKhz, ui->spinSpeed->maximum()));

    ui->checkSlot1Enabled->setChecked(s.slot1Enabled);
    ui->checkSlot2Enabled->setChecked(s.slot2Enabled);
    ui->checkEraseBeforeFlash->setChecked(s.eraseBeforeFlash);
    ui->checkVerifyAfterFlash->setChecked(s.verifyAfterFlash);
    ui->checkResetAfterFlash->setChecked(s.resetAfterFlash);
    ui->checkConnectUnderReset->setChecked(s.connectUnderReset);
    ui->editSlot1Path->setText(s.slot1Path);
    if (!s.slot1Address.isEmpty()) ui->editSlot1Address->setText(s.slot1Address);
    if (!s.slot2Address.isEmpty()) ui->editSlot2Address->setText(s.slot2Address);
    ui->editSlot2Path->setText(s.slot2Path);

    if (!s.geometry.isEmpty()) restoreGeometry(s.geometry);
    if (!s.windowState.isEmpty()) restoreState(s.windowState);
}

void MainWindow::saveSettings() {
    UiSettings s;
    s.manufacturer = ui->comboManufacturer->currentText();
    s.chip = ui->comboModel->currentText();
    s.probe = ui->comboProbe->currentText();
    s.interfaceName = ui->comboInterface->currentText();
    s.speedKhz = ui->spinSpeed->value();
    s.slot1Path = ui->editSlot1Path->text();
    s.slot2Path = ui->editSlot2Path->text();
    s.slot1Address = ui->editSlot1Address->text();
    s.slot2Address = ui->editSlot2Address->text();
    s.customChipFiles = m_customChipFiles;
    s.slot1Enabled = ui->checkSlot1Enabled->isChecked();
    s.slot2Enabled = ui->checkSlot2Enabled->isChecked();
    s.eraseBeforeFlash = ui->checkEraseBeforeFlash->isChecked();
    s.verifyAfterFlash = ui->checkVerifyAfterFlash->isChecked();
    s.resetAfterFlash = ui->checkResetAfterFlash->isChecked();
    s.connectUnderReset = ui->checkConnectUnderReset->isChecked();
    s.geometry = saveGeometry();
    s.windowState = saveState();

    QSettings settings;
    AppSettings::save(settings, s);
}

void MainWindow::setupUiCustomizations() {
    // Memory Table Setup
    ui->tableMemory->setModel(m_memoryModel);
    ui->tableMemory->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int i = 1; i <= 16; ++i) {
        ui->tableMemory->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Fixed);
        ui->tableMemory->setColumnWidth(i, 32);
    }
    ui->tableMemory->horizontalHeader()->setSectionResizeMode(17, QHeaderView::Stretch);

    // Monospace Font for Table & Console
    QFont monoFont("Consolas", 9);
    monoFont.setStyleHint(QFont::Monospace);
    ui->tableMemory->setFont(monoFont);
    ui->textLog->setFont(monoFont);

    // DockWidget properties
    ui->dockTargetSettings->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    ui->dockConsoleOutput->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    ui->actionVerify->setToolTip("Compare the target with the selected firmware files");
    ui->actionRead->setToolTip("Read the chip's flash into a file and show it in the memory view");

    ui->statusBar->showMessage("Ready");
}

void MainWindow::setupToolBarWidgets() {
    ui->mainToolBar->addSeparator();

    // Select HEX File ComboBox
    m_comboToolbarHexFile = new QComboBox(this);
    m_comboToolbarHexFile->setMinimumWidth(180);
    m_comboToolbarHexFile->addItem("Select HEX File");
    ui->mainToolBar->addWidget(m_comboToolbarHexFile);

    connect(m_comboToolbarHexFile, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int idx) {
        if (idx == 1 && !ui->editSlot1Path->text().isEmpty()) {
            ui->radioSourceFile1->setChecked(true);
        } else if (idx == 2 && !ui->editSlot2Path->text().isEmpty()) {
            ui->radioSourceFile2->setChecked(true);
        }
    });
}

void MainWindow::setupDeviceDatabase() {
    // The chip list comes from the engine (see onChipListReady); until then the
    // chip box stays a free-text field so a saved chip name is still usable.
    ui->comboModel->setEditable(true);
    ui->comboModel->setInsertPolicy(QComboBox::NoInsert);
    // Typing searches every known chip, not only the manufacturer chosen above.
    m_chipNames = new QStringListModel(this);
    auto* completer = new QCompleter(m_chipNames, this);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setMaxVisibleItems(15);
    ui->comboModel->setCompleter(completer);
    connect(completer, QOverload<const QString&>::of(&QCompleter::activated), this, &MainWindow::onChipTextCommitted);
    connect(ui->comboModel->lineEdit(), &QLineEdit::editingFinished, this, &MainWindow::onChipTextCommitted);

    // Interfaces
    ui->comboInterface->addItems({"SWD", "JTAG"});
    ui->comboInterface->setCurrentText("SWD");
}

void MainWindow::onManufacturerChanged(const QString& manufacturer) {
    if (m_chips.isEmpty() || manufacturer.isEmpty()) return;

    const QString current = ui->comboModel->currentText();
    ui->comboModel->clear();
    ui->comboModel->addItems(ChipCatalog::chipNames(m_chips, manufacturer));

    // Keep the typed/selected chip if it belongs to this manufacturer, otherwise start empty.
    int idx = ui->comboModel->findText(current, Qt::MatchFixedString);
    if (idx >= 0) {
        ui->comboModel->setCurrentIndex(idx);
    } else {
        ui->comboModel->setCurrentIndex(-1);
        ui->comboModel->clearEditText();
    }
}

void MainWindow::onChipListReady(bool success, const QList<ChipEntry>& chips, const QString& error) {
    m_builtinChipsLoaded = success;
    if (success) {
        m_builtinChips = chips;
        appendLog("INFO", QString("Loaded %1 supported chips from probe-rs.").arg(chips.size()));
    } else {
        m_builtinChips.clear();
        appendLog("ERROR", QString("Chip list unavailable: %1").arg(error), "#ff5555");
        appendLog("WARNING", "Chip names cannot be checked; type the exact probe-rs chip name.");
    }
    rebuildChipCatalog();
}

// Merges probe-rs' own chips with the ones from the user's chip description files and
// refreshes the manufacturer and model boxes.
void MainWindow::rebuildChipCatalog() {
    m_chips = m_builtinChips;

    // Chip descriptions built into the executable, then the ones the user added.
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
            appendLog("ERROR", error, "#ff5555");
            continue;
        }
        for (const QString& name : names) {
            m_chips.append({"Chip file: " + family, name, path});
        }
        appendLog("INFO", QString("Chip description '%1': %2 chips (%3).")
                              .arg(QFileInfo(path).fileName()).arg(names.size()).arg(family));
    }

    QStringList allNames;
    for (const ChipEntry& chip : m_chips) allNames << chip.name;
    m_chipNames->setStringList(allNames);

    if (m_chips.isEmpty()) {
        ui->comboModel->setEditText(m_pendingChip);
        return;
    }

    const QString wantedMaker = m_pendingManufacturer.isEmpty() ? ui->comboManufacturer->currentText() : m_pendingManufacturer;
    ui->comboManufacturer->blockSignals(true);
    ui->comboManufacturer->clear();
    ui->comboManufacturer->addItems(ChipCatalog::manufacturers(m_chips));
    int makerIdx = ui->comboManufacturer->findText(wantedMaker);
    if (makerIdx < 0) makerIdx = ui->comboManufacturer->findText("STM32");
    ui->comboManufacturer->setCurrentIndex(makerIdx >= 0 ? makerIdx : 0);
    ui->comboManufacturer->blockSignals(false);

    const QString wantedChip = m_pendingChip.isEmpty() ? ui->comboModel->currentText() : m_pendingChip;
    ui->comboModel->setEditText(wantedChip);
    onManufacturerChanged(ui->comboManufacturer->currentText());

    QString canonical;
    if (!wantedChip.isEmpty() && ChipCatalog::find(m_chips, wantedChip, &canonical)) {
        ui->comboModel->setEditText(canonical);
    }
    m_pendingManufacturer.clear();
    m_pendingChip.clear();
}

void MainWindow::onAddChipFileTriggered() {
    const QString path = QFileDialog::getOpenFileName(this, "Add Chip Description File", "",
                                                      "probe-rs chip description (*.yaml *.yml);;All Files (*.*)");
    if (path.isEmpty()) return;

    if (m_customChipFiles.contains(path)) {
        appendLog("INFO", "That chip description file is already added.");
        return;
    }

    QString family;
    QString error;
    if (ChipDescription::variantNamesFromFile(path, &family, &error).isEmpty()) {
        appendLog("ERROR", error, "#ff5555");
        QMessageBox::warning(this, "Invalid Chip Description", error);
        return;
    }

    m_customChipFiles << path;
    rebuildChipCatalog();
}

void MainWindow::onClearChipFilesTriggered() {
    if (m_customChipFiles.isEmpty()) return;
    m_customChipFiles.clear();
    appendLog("INFO", "Custom chip description files removed.");
    rebuildChipCatalog();
}

// Pushes the chip, speed and probe currently shown in the UI to the engine, so erase, reset
// and program never run with stale or empty settings.
bool MainWindow::prepareEngine() {
    QString chip;
    if (!resolveChip(chip)) return false;

    const ChipEntry* entry = ChipCatalog::findEntry(m_chips, chip);
    const QString descriptionFile = entry ? entry->descriptionFile : QString();
    TargetSettings target;
    target.chipName = chip;
    target.speedKhz = ui->spinSpeed->value();
    target.probeId = ui->comboProbe->currentData().toString();
    target.chipDescriptionPath = descriptionFile;
    target.protocol = (ui->comboInterface->currentText() == "JTAG") ? DebugProtocol::Jtag : DebugProtocol::Swd;
    target.connectUnderReset = ui->checkConnectUnderReset->isChecked();
    m_engine->configureTarget(target);
    return true;
}

// Returns false (after telling the user) when the chip name is not known to probe-rs.
bool MainWindow::resolveChip(QString& chip) {
    chip = ui->comboModel->currentText().trimmed();
    if (chip.isEmpty()) {
        QMessageBox::warning(this, "No Chip Selected", "Please select or type a target chip.");
        return false;
    }

    QString canonical;
    if (ChipCatalog::find(m_chips, chip, &canonical)) {
        chip = canonical;
        return true;
    }

    // Without probe-rs' own list a typed name cannot be checked; let probe-rs judge it.
    if (!m_builtinChipsLoaded) return true;

    const QString msg = QString("'%1' is not a chip name known to probe-rs. Pick one from the list.").arg(chip);
    appendLog("ERROR", msg, "#ff5555");
    QMessageBox::warning(this, "Unknown Chip", msg);
    return false;
}

void MainWindow::appendLog(const QString& levelStr, const QString& message, const QString& colorHex) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString formatted = QString("<span style='color: #888888;'>[%1]</span> <span style='color: %2; font-weight: bold;'>%3:</span> <span style='color: #dcdcdc;'>%4</span>")
                            .arg(timeStr, colorHex, levelStr, message.toHtmlEscaped());
    ui->textLog->appendHtml(formatted);
}

void MainWindow::updateUiState() {
    bool busy = m_isOperationRunning || m_engine->isBusy();
    ui->actionConnect->setEnabled(!busy && !m_engine->isConnected());
    ui->actionDisconnect->setEnabled(!busy && m_engine->isConnected());
    ui->actionRead->setEnabled(!busy);
    ui->actionErase->setEnabled(!busy);
    ui->actionProgram->setEnabled(!busy);
    ui->actionVerify->setEnabled(!busy);
    ui->actionResetTarget->setEnabled(!busy);
    ui->actionCancel->setEnabled(busy);
}

void MainWindow::onRefreshProbesTriggered() {
    m_probeRefreshVerbose = true;  // the user asked for it: always report the result
    m_engine->requestProbeList();
}

// Fills the probe box with what probe-rs found. Automatic refreshes only write to the log when
// the set of probes changed; a manual refresh always does.
void MainWindow::onProbeListReady(bool success, const QList<ProbeInfo>& probes, const QString& error) {
    const bool verbose = m_probeRefreshVerbose;
    m_probeRefreshVerbose = false;

    if (!success) {
        if (verbose || !m_lastProbeError) appendLog("ERROR", error, "#ff5555");
        m_lastProbeError = true;
        return;
    }
    m_lastProbeError = false;

    QStringList keys;
    for (const ProbeInfo& p : probes) keys << p.selector();
    const bool changed = (keys != m_lastProbeKeys);
    m_lastProbeKeys = keys;

    const QString previous = ui->comboProbe->currentText();
    ui->comboProbe->clear();
    if (probes.isEmpty()) {
        // Empty data means "let probe-rs pick"; connecting will report the real error.
        ui->comboProbe->addItem("(no probe detected)", QString());
        if (verbose || changed) {
            appendLog("WARNING", "No debug probe detected. Check the USB connection, the probe driver and that probe-rs is installed.");
        }
        return;
    }

    for (const auto& p : probes) {
        const QString label = p.displayName + " [" + p.selector() + "]";
        ui->comboProbe->addItem(label, p.selector());
        if (verbose || changed) appendLog("INFO", QString("Found probe: %1").arg(label));
    }
    int idx = ui->comboProbe->findText(previous);
    if (idx >= 0) ui->comboProbe->setCurrentIndex(idx);

    if (probes.size() > 1 && (verbose || changed)) {
        const QString msg = QString("%1 probes found. Using: %2. Check the Probe selection before flashing.")
                                .arg(probes.size())
                                .arg(ui->comboProbe->currentText());
        appendLog("WARNING", msg, "#ffb86c");
        ui->statusBar->showMessage(msg, 8000);
    }
}

// Probes are plugged in while the program runs: look again whenever the window is activated.
void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow()
        && !m_isOperationRunning && !m_engine->isBusy()) {
        m_engine->requestProbeList();
    }
}

void MainWindow::onConnectTriggered() {
    if (!prepareEngine()) return;
    const QString chip = ui->comboModel->currentText().trimmed();
    const uint32_t speed = ui->spinSpeed->value();
    const QString probe = ui->comboProbe->currentData().toString();

    m_isOperationRunning = true;
    updateUiState();
    appendLog("INFO", QString("Connecting to %1 via %2 at %3 kHz...").arg(chip, ui->comboInterface->currentText()).arg(speed));
    m_engine->connectTarget(chip, speed, probe);
}

void MainWindow::onDisconnectTriggered() {
    m_engine->disconnectTarget();
}

void MainWindow::onEraseTriggered() {
    if (QMessageBox::question(this, "Confirm Erase",
            "Are you sure you want to completely erase the target chip flash?",
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!prepareEngine()) return;

    m_isOperationRunning = true;
    updateUiState();
    m_engine->eraseTarget(true);
}

// Collects target settings and both slots from the UI and checks them. Returns false (after
// telling the user why) when the run must not start.
bool MainWindow::prepareFirmwareRun(TargetConfig& cfg, FirmwareSlot& s1, FirmwareSlot& s2) {
    if (!resolveChip(cfg.chipName)) return false;
    cfg.speedKhz = ui->spinSpeed->value();
    cfg.probeId = ui->comboProbe->currentData().toString();
    cfg.autoSpeedEnabled = true;
    cfg.eraseBeforeFlash = ui->checkEraseBeforeFlash->isChecked();
    cfg.verifyAfterFlash = ui->checkVerifyAfterFlash->isChecked();
    cfg.resetAfterFlash = ui->checkResetAfterFlash->isChecked();

    AddressRange range1;
    AddressRange range2;
    if (!buildSlot(1, s1, range1) || !buildSlot(2, s2, range2)) {
        return false;
    }

    if (s1.enabled && s2.enabled && SlotValidator::overlaps(range1, range2)) {
        const QString msg = QString("Slot 1 (0x%1 - 0x%2) and Slot 2 (0x%3 - 0x%4) overlap, so both cannot be on the target. "
                                    "Flashing would overwrite the bootloader.")
                                .arg(range1.start, 8, 16, QLatin1Char('0'))
                                .arg(uint64_t(range1.start) + range1.size - 1, 8, 16, QLatin1Char('0'))
                                .arg(range2.start, 8, 16, QLatin1Char('0'))
                                .arg(uint64_t(range2.start) + range2.size - 1, 8, 16, QLatin1Char('0'));
        appendLog("ERROR", msg, "#ff5555");
        QMessageBox::warning(this, "Address Overlap", msg);
        return false;
    }

    if (!prepareEngine()) return false;

    // Refuse to write outside the chip's flash. Skipped when the memory layout is unknown.
    const ChipEntry* entry = ChipCatalog::findEntry(m_chips, cfg.chipName);
    const ChipMemoryMap memory = m_engine->queryMemoryMap(cfg.chipName, entry ? entry->descriptionFile : QString());
    if (memory.isValid()) {
        struct SlotCheck { const FirmwareSlot* slot; const AddressRange* range; };
        for (const SlotCheck& c : {SlotCheck{&s1, &range1}, SlotCheck{&s2, &range2}}) {
            if (!c.slot->enabled || memory.flashContains(c.range->start, c.range->size)) continue;

            const MemoryRegion* flash = memory.mainFlash();
            QString msg = QString("%1 (0x%2 - 0x%3) does not fit into the flash of %4")
                              .arg(c.slot->label)
                              .arg(c.range->start, 8, 16, QLatin1Char('0'))
                              .arg(uint64_t(c.range->start) + c.range->size - 1, 8, 16, QLatin1Char('0'))
                              .arg(cfg.chipName);
            if (flash) {
                msg += QString(" (0x%1 - 0x%2).")
                           .arg(flash->start, 8, 16, QLatin1Char('0'))
                           .arg(flash->end() - 1, 8, 16, QLatin1Char('0'));
            } else {
                msg += ".";
            }
            msg += "\nCheck the start address of the slot and the selected chip.";
            appendLog("ERROR", msg, "#ff5555");
            QMessageBox::warning(this, "Outside Flash Range", msg);
            return false;
        }
    }
    return true;
}

void MainWindow::onProgramTriggered() {
    TargetConfig cfg;
    FirmwareSlot s1;
    FirmwareSlot s2;
    if (!prepareFirmwareRun(cfg, s1, s2)) return;

    m_coordinator->setTargetConfig(cfg);
    m_coordinator->setSlot1(s1);
    m_coordinator->setSlot2(s2);

    m_coordinator->startFlashSequence();
}

void MainWindow::onVerifyTriggered() {
    TargetConfig cfg;
    FirmwareSlot s1;
    FirmwareSlot s2;
    if (!prepareFirmwareRun(cfg, s1, s2)) return;

    m_coordinator->setTargetConfig(cfg);
    m_coordinator->setSlot1(s1);
    m_coordinator->setSlot2(s2);

    m_coordinator->startVerifySequence();
}

bool MainWindow::slotAddress(int slotIndex, uint32_t& address) const {
    QLineEdit* edit = (slotIndex == 1) ? ui->editSlot1Address : ui->editSlot2Address;
    return SlotValidator::parseAddress(edit->text(), address);
}

// Fills `slot` and `range` from the UI. Returns false (after telling the user why)
// when an enabled slot has an unreadable file or an invalid address.
bool MainWindow::buildSlot(int slotIndex, FirmwareSlot& slot, AddressRange& range) {
    const bool first = (slotIndex == 1);
    const QString path = (first ? ui->editSlot1Path : ui->editSlot2Path)->text().trimmed();
    const bool checked = (first ? ui->checkSlot1Enabled : ui->checkSlot2Enabled)->isChecked();

    slot.label = first ? "Slot 1 (Bootloader)" : "Slot 2 (Application)";
    slot.filePath = path;
    slot.enabled = checked && !path.isEmpty();
    if (!slot.enabled) return true;

    uint32_t address = 0;
    if (!slotAddress(slotIndex, address)) {
        const QString msg = QString("%1: invalid start address.").arg(slot.label);
        appendLog("ERROR", msg, "#ff5555");
        QMessageBox::warning(this, "Invalid Address", msg);
        return false;
    }

    HexParseResult res;
    const FileFormat format = HexParser::detectFormat(path);
    if (format == FileFormat::IntelHex) {
        res = HexParser::analyzeHexFile(path, false);
        address = res.baseAddress;  // HEX records carry their own addresses
    } else if (format == FileFormat::Elf) {
        res = HexParser::analyzeElfFile(path, false);
        address = res.baseAddress;  // so do the segments of an ELF file
    } else {
        res = HexParser::analyzeBinFile(path, address);
    }
    if (!res.success) {
        const QString msg = QString("%1: %2").arg(slot.label, res.errorMessage);
        appendLog("ERROR", msg, "#ff5555");
        QMessageBox::warning(this, "Invalid Firmware File", msg);
        return false;
    }

    slot.startAddress = address;
    range.start = address;
    range.size = res.size;
    return true;
}

void MainWindow::onResetTargetTriggered() {
    if (!prepareEngine()) return;
    m_engine->resetTarget(false);
}

void MainWindow::onOpenHexTriggered() {
    QString file = QFileDialog::getOpenFileName(this, "Open HEX File", "", "HEX Files (*.hex *.ihex);;All Files (*.*)");
    if (!file.isEmpty()) {
        ui->editSlot2Path->setText(file);
        ui->radioSourceFile2->setChecked(true);
    }
}

void MainWindow::onOpenSlot1Clicked() {
    QString file = QFileDialog::getOpenFileName(this, "Select Slot 1 (Bootloader) File", "", "Firmware Files (*.hex *.bin *.elf *.axf);;All Files (*.*)");
    if (!file.isEmpty()) {
        ui->editSlot1Path->setText(file);
    }
}

void MainWindow::onOpenSlot2Clicked() {
    QString file = QFileDialog::getOpenFileName(this, "Select Slot 2 (Application) File", "", "Firmware Files (*.hex *.bin *.elf *.axf);;All Files (*.*)");
    if (!file.isEmpty()) {
        ui->editSlot2Path->setText(file);
    }
}

void MainWindow::onSlot1PathChanged(const QString& path) {
    loadFileIntoSlot(1, path);
}

void MainWindow::onSlot2PathChanged(const QString& path) {
    loadFileIntoSlot(2, path);
}

void MainWindow::loadFileIntoSlot(int slotIndex, const QString& path) {
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    if (!fi.exists()) return;

    FileFormat fmt = HexParser::detectFormat(path);
    HexParseResult res;
    if (fmt == FileFormat::IntelHex) {
        res = HexParser::analyzeHexFile(path, true);
    } else if (fmt == FileFormat::Elf) {
        res = HexParser::analyzeElfFile(path, true);
    } else {
        uint32_t base = 0x08000000;
        slotAddress(slotIndex, base);
        res = HexParser::analyzeBinFile(path, base);
    }

    if (!res.success) {
        appendLog("ERROR", QString("Cannot load '%1': %2").arg(fi.fileName(), res.errorMessage), "#ff5555");
    }

    if (slotIndex == 1) {
        m_slot1ParseResult = res;
    } else {
        m_slot2ParseResult = res;
    }

    // Update Toolbar HEX File Combo
    if (m_comboToolbarHexFile) {
        while (m_comboToolbarHexFile->count() > 1) {
            m_comboToolbarHexFile->removeItem(1);
        }
        if (!ui->editSlot1Path->text().isEmpty()) {
            m_comboToolbarHexFile->addItem(QFileInfo(ui->editSlot1Path->text()).fileName());
        }
        if (!ui->editSlot2Path->text().isEmpty()) {
            m_comboToolbarHexFile->addItem(QFileInfo(ui->editSlot2Path->text()).fileName());
        }
    }

    if (res.success) {
        appendLog("INFO", QString("File '%1' loaded.").arg(fi.fileName()));
    }
    updateMemoryViewer();
}

void MainWindow::onDataSourceChanged() {
    updateMemoryViewer();
}

void MainWindow::updateMemoryViewer() {
    if (ui->radioSourceFile1->isChecked()) {
        if (m_slot1ParseResult.success && !m_slot1ParseResult.data.isEmpty()) {
            m_memoryModel->setMemoryData(m_slot1ParseResult.baseAddress, m_slot1ParseResult.data);
            ui->lblHexFileInfo->setText(QString("HEX File Loaded: %1 (%2 KB)")
                .arg(QFileInfo(ui->editSlot1Path->text()).fileName())
                .arg(m_slot1ParseResult.size / 1024.0, 0, 'f', 1));
        } else {
            m_memoryModel->clear();
            ui->lblHexFileInfo->setText("HEX File Loaded: None");
        }
    } else if (ui->radioSourceFile2->isChecked()) {
        if (m_slot2ParseResult.success && !m_slot2ParseResult.data.isEmpty()) {
            m_memoryModel->setMemoryData(m_slot2ParseResult.baseAddress, m_slot2ParseResult.data);
            ui->lblHexFileInfo->setText(QString("HEX File Loaded: %1 (%2 KB)")
                .arg(QFileInfo(ui->editSlot2Path->text()).fileName())
                .arg(m_slot2ParseResult.size / 1024.0, 0, 'f', 1));
        } else {
            m_memoryModel->clear();
            ui->lblHexFileInfo->setText("HEX File Loaded: None");
        }
    } else if (ui->radioSourceTarget->isChecked()) {
        if (!m_targetReadData.isEmpty()) {
            m_memoryModel->setMemoryData(m_targetReadBaseAddress, m_targetReadData);
            ui->lblHexFileInfo->setText(QString("Target Memory: %1 bytes loaded").arg(m_targetReadData.size()));
        } else {
            m_memoryModel->clear();
            ui->lblHexFileInfo->setText("Target Memory: No data read");
        }
    }
}

void MainWindow::onEngineLogMessage(const QString& message, EngineLogLevel level) {
    QString levelStr = "INFO";
    QString color = "#5dade2";
    switch (level) {
    case EngineLogLevel::Debug: levelStr = "DEBUG"; color = "#95a5a6"; break;
    case EngineLogLevel::Info: levelStr = "INFO"; color = "#5dade2"; break;
    case EngineLogLevel::Warning: levelStr = "WARN"; color = "#f39c12"; break;
    case EngineLogLevel::Error: levelStr = "ERROR"; color = "#e74c3c"; break;
    case EngineLogLevel::Success: levelStr = "SUCCESS"; color = "#2ecc71"; break;
    }
    appendLog(levelStr, message, color);
}

void MainWindow::onEngineConnected(bool success, const QString& chipInfo) {
    m_isOperationRunning = false;
    updateUiState();
    if (success) {
        ui->statusBar->showMessage(QString("Connected to %1 via %2 at %3 kHz").arg(chipInfo, ui->comboInterface->currentText()).arg(ui->spinSpeed->value()));
        appendLog("INFO", QString("Connected to %1 via %2 at %3 kHz.").arg(chipInfo, ui->comboInterface->currentText()).arg(ui->spinSpeed->value()), "#2ecc71");
    } else {
        ui->statusBar->showMessage("Connection Failed");
        appendLog("ERROR", QString("Connection failed: %1").arg(chipInfo), "#e74c3c");
    }
}

void MainWindow::onEngineDisconnected() {
    m_isOperationRunning = false;
    ui->statusBar->showMessage("Disconnected");
    appendLog("INFO", "Target Disconnected.");
    updateUiState();
}

void MainWindow::onSequenceStarted() {
    m_sequenceRunning = true;
    m_isOperationRunning = true;
    ui->progressBar->setValue(0);
    updateUiState();
    appendLog("INFO", "Programming sequence initiated.");
}

void MainWindow::onSequenceFinished(bool success, const QString& message) {
    m_sequenceRunning = false;
    m_isOperationRunning = false;
    updateUiState();
    if (success) {
        ui->progressBar->setValue(100);
        appendLog("SUCCESS", message, "#2ecc71");
    } else {
        appendLog("ERROR", message, "#e74c3c");
    }
}

void MainWindow::onStepChanged(const QString& stepDescription, int stepIndex, int totalSteps) {
    appendLog("INFO", QString("[%1/%2] %3").arg(stepIndex).arg(totalSteps).arg(stepDescription));
}

void MainWindow::onTotalProgressChanged(int overallPercentage) {
    ui->progressBar->setValue(overallPercentage);
}

void MainWindow::onAutoSpeedFallbackTriggered(uint32_t newSpeedKhz) {
    appendLog("WARN", QString("Auto-Speed fallback triggered -> %1 kHz. Retrying...").arg(newSpeedKhz), "#e67e22");
    ui->spinSpeed->setValue(static_cast<int>(newSpeedKhz));
}

QString MainWindow::currentProbeRsPath() const {
    return ProbeRsLocator::find(QCoreApplication::applicationDirPath(), QDir::homePath());
}

void MainWindow::onSystemCheckTriggered() {
    auto* dialog = new SystemCheckDialog(currentProbeRsPath(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->runCheck();
    dialog->open();
}

// After a chip was picked or typed: if it belongs to another manufacturer than the one
// shown, switch the manufacturer box so the two always agree.
void MainWindow::onChipTextCommitted() {
    QTimer::singleShot(0, this, [this]() {
        QString canonical;
        if (!ChipCatalog::find(m_chips, ui->comboModel->currentText(), &canonical)) return;

        const QString shown = ui->comboManufacturer->currentText();
        if (ChipCatalog::chipNames(m_chips, shown).contains(canonical)) {
            ui->comboModel->setEditText(canonical);
            return;
        }

        const QString maker = ChipCatalog::manufacturerOf(m_chips, canonical);
        const int idx = ui->comboManufacturer->findText(maker.isEmpty() ? ChipCatalog::allChipsLabel() : maker);
        if (idx >= 0) ui->comboManufacturer->setCurrentIndex(idx);  // keeps the chip name
        ui->comboModel->setEditText(canonical);
    });
}

void MainWindow::onCancelTriggered() {
    appendLog("WARNING", "Stopping the running operation...", "#e67e22");
    if (m_sequenceRunning) {
        m_coordinator->cancelSequence();
    } else {
        m_engine->cancelCurrentOperation();
    }
}

// Single commands (Erase, Reset, ...) end here; during a sequence the coordinator decides
// what happens next and the window is updated from its own signals.
void MainWindow::onEngineOperationFinished(bool success, const QString& message) {
    Q_UNUSED(message);

    // A memory read just ended: show what was read.
    if (!m_pendingReadFile.isEmpty()) {
        const QString file = m_pendingReadFile;
        m_pendingReadFile.clear();
        m_isOperationRunning = false;
        updateUiState();

        QFile in(file);
        if (success && in.open(QIODevice::ReadOnly)) {
            m_targetReadData = in.readAll();
            m_targetReadBaseAddress = m_pendingReadBase;
            ui->radioSourceTarget->setChecked(true);
            updateMemoryViewer();
            appendLog("SUCCESS", QString("Read %1 bytes into %2").arg(m_targetReadData.size()).arg(file), "#2ecc71");
        } else if (success) {
            appendLog("ERROR", "The read finished, but the output file could not be opened: " + file, "#e74c3c");
        }
        return;
    }

    if (m_sequenceRunning) return;
    m_isOperationRunning = false;
    updateUiState();
}

void MainWindow::onReadTargetTriggered() {
    if (!prepareEngine()) return;

    const QString chip = ui->comboModel->currentText().trimmed();
    const ChipEntry* entry = ChipCatalog::findEntry(m_chips, chip);
    const ChipMemoryMap memory = m_engine->queryMemoryMap(chip, entry ? entry->descriptionFile : QString());
    const MemoryRegion* flash = memory.mainFlash();
    if (!flash) {
        const QString msg = "The flash size of this chip is unknown, so the whole flash cannot be read.";
        appendLog("ERROR", msg, "#ff5555");
        QMessageBox::warning(this, "Read Flash", msg);
        return;
    }

    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                  .filePath(chip + "_flash.bin");
    const QString file = QFileDialog::getSaveFileName(this, "Save Flash Contents As", suggested,
                                                      "Binary (*.bin);;All Files (*.*)");
    if (file.isEmpty()) return;

    m_pendingReadFile = file;
    m_pendingReadBase = static_cast<uint32_t>(flash->start);
    m_isOperationRunning = true;
    updateUiState();
    appendLog("INFO", QString("Reading %1 KiB of flash from 0x%2...")
                          .arg(flash->size / 1024).arg(flash->start, 8, 16, QLatin1Char('0')));
    m_engine->readMemory(flash->start, flash->size, file);
}

void MainWindow::onSaveLogTriggered() {
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                  .filePath("coldhex-log.txt");
    const QString file = QFileDialog::getSaveFileName(this, "Save Log", suggested, "Text (*.txt);;All Files (*.*)");
    if (file.isEmpty()) return;

    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save Log", "Cannot write to " + file);
        return;
    }
    out.write(ui->textLog->toPlainText().toUtf8());
    appendLog("INFO", "Log saved to " + file);
}

namespace {

bool isFirmwareFile(const QString& path) {
    const FileFormat format = HexParser::detectFormat(path);
    return format == FileFormat::IntelHex || format == FileFormat::RawBinary || format == FileFormat::Elf;
}

// Names that usually belong to a bootloader: boot.hex, my_bootloader.bin, app_bl.elf ...
bool looksLikeBootloader(const QString& path) {
    const QString name = QFileInfo(path).completeBaseName().toLower();
    return name.contains("boot") || name.endsWith("_bl") || name.startsWith("bl_") || name == "bl";
}

}  // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && isFirmwareFile(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
}

// Dropped firmware files go to the slots: bootloader-like names to Slot 1, everything else to
// Slot 2. Two files without such a name fill Slot 1 and Slot 2 in the order they were dropped.
void MainWindow::dropEvent(QDropEvent* event) {
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && isFirmwareFile(url.toLocalFile())) files << url.toLocalFile();
    }
    if (files.isEmpty()) return;

    const bool twoPlainFiles = files.size() == 2 && !looksLikeBootloader(files[0]) && !looksLikeBootloader(files[1]);
    for (int i = 0; i < files.size() && i < 2; ++i) {
        const QString& file = files[i];
        const bool toSlot1 = twoPlainFiles ? (i == 0) : looksLikeBootloader(file);
        QLineEdit* edit = toSlot1 ? ui->editSlot1Path : ui->editSlot2Path;
        QCheckBox* check = toSlot1 ? ui->checkSlot1Enabled : ui->checkSlot2Enabled;
        edit->setText(file);
        check->setChecked(true);
        appendLog("INFO", QString("%1 -> %2").arg(QFileInfo(file).fileName(), toSlot1 ? "Slot 1 (Bootloader)" : "Slot 2 (Application)"));
    }
    if (files.size() > 2) appendLog("WARNING", "Only the first two dropped files were used.");
    event->acceptProposedAction();
}

void MainWindow::onAboutTriggered() {
    QMessageBox::about(this, "About ColdHex",
        QString("<h3>ColdHex %1</h3>"
                "<p>Flash and manage ARM Cortex-M microcontrollers through ST-Link, J-Link and CMSIS-DAP probes. "
                "Flashing is done by <a href='https://probe.rs'>probe-rs</a>.</p>"
                "<p>Licensed under the GNU LGPL v3. Built with Qt %2.</p>")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

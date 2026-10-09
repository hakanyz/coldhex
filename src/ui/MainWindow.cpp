#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "core/HexParser.h"
#include "core/AppSettings.h"
#include <QCloseEvent>
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

    setupUiCustomizations();
    setupToolBarWidgets();
    setupDeviceDatabase();

    // Engine Signals
    connect(m_engine, &ProbeRsEngine::logMessage, this, &MainWindow::onEngineLogMessage);
    connect(m_engine, &ProbeRsEngine::connected, this, &MainWindow::onEngineConnected);
    connect(m_engine, &ProbeRsEngine::disconnected, this, &MainWindow::onEngineDisconnected);

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
    connect(ui->actionErase, &QAction::triggered, this, &MainWindow::onEraseTriggered);
    connect(ui->actionProgram, &QAction::triggered, this, &MainWindow::onProgramTriggered);
    connect(ui->actionVerify, &QAction::triggered, this, &MainWindow::onVerifyTriggered);
    connect(ui->actionResetTarget, &QAction::triggered, this, &MainWindow::onResetTargetTriggered);
    connect(ui->actionOpenHex, &QAction::triggered, this, &MainWindow::onOpenHexTriggered);
    connect(ui->actionClearLog, &QAction::triggered, ui->textLog, &QPlainTextEdit::clear);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionRefreshProbes, &QAction::triggered, this, &MainWindow::onRefreshProbesTriggered);

    // Settings Panel Controls
    connect(ui->comboManufacturer, &QComboBox::currentTextChanged, this, &MainWindow::onManufacturerChanged);
    connect(ui->spinSpeed, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onSpeedSpinChanged);
    connect(ui->btnOpenSlot1, &QPushButton::clicked, this, &MainWindow::onOpenSlot1Clicked);
    connect(ui->btnOpenSlot2, &QPushButton::clicked, this, &MainWindow::onOpenSlot2Clicked);
    connect(ui->editSlot1Path, &QLineEdit::textChanged, this, &MainWindow::onSlot1PathChanged);
    connect(ui->editSlot2Path, &QLineEdit::textChanged, this, &MainWindow::onSlot2PathChanged);

    // Data Source Radios
    connect(ui->radioSourceFile1, &QRadioButton::toggled, this, &MainWindow::onDataSourceChanged);
    connect(ui->radioSourceFile2, &QRadioButton::toggled, this, &MainWindow::onDataSourceChanged);
    connect(ui->radioSourceTarget, &QRadioButton::toggled, this, &MainWindow::onDataSourceChanged);

    // Model selection updates memory map title
    connect(ui->comboModel, &QComboBox::currentTextChanged, [this](const QString& model) {
        ui->lblMemoryMapTitle->setText(QString("Memory Map (%1) - Flash").arg(model.trimmed()));
    });

    onRefreshProbesTriggered();
    restoreSettings();
    appendLog("INFO", "ProbeDesk v0.1 started.");
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

    selectIfPresent(ui->comboManufacturer, s.manufacturer);  // repopulates the model list
    selectIfPresent(ui->comboModel, s.chip);
    selectIfPresent(ui->comboProbe, s.probe);
    selectIfPresent(ui->comboInterface, s.interfaceName);
    ui->spinSpeed->setValue(qBound(ui->spinSpeed->minimum(), s.speedKhz, ui->spinSpeed->maximum()));

    ui->checkSlot1Enabled->setChecked(s.slot1Enabled);
    ui->checkSlot2Enabled->setChecked(s.slot2Enabled);
    ui->editSlot1Path->setText(s.slot1Path);
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
    s.slot1Enabled = ui->checkSlot1Enabled->isChecked();
    s.slot2Enabled = ui->checkSlot2Enabled->isChecked();
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

    ui->mainToolBar->addSeparator();

    // SWD Frequency label & slider
    m_lblToolbarSpeed = new QLabel("  SWD Frequency (kHz): ", this);
    ui->mainToolBar->addWidget(m_lblToolbarSpeed);

    m_sliderToolbarSpeed = new QSlider(Qt::Horizontal, this);
    m_sliderToolbarSpeed->setRange(100, 10000);
    m_sliderToolbarSpeed->setSingleStep(500);
    m_sliderToolbarSpeed->setValue(1000);
    m_sliderToolbarSpeed->setMaximumWidth(140);
    ui->mainToolBar->addWidget(m_sliderToolbarSpeed);

    connect(m_sliderToolbarSpeed, &QSlider::valueChanged, this, &MainWindow::onSpeedSliderChanged);
}

void MainWindow::setupDeviceDatabase() {
    // Manufacturers
    ui->comboManufacturer->addItems({"STM32", "GD32", "APM32 / Geehy", "TI", "Nuvoton"});
    ui->comboManufacturer->setCurrentText("STM32");

    // Probes
    ui->comboProbe->addItems({"J-Link", "ST-Link", "CMSIS-DAP"});

    // Interfaces
    ui->comboInterface->addItems({"SWD", "JTAG"});
    ui->comboInterface->setCurrentText("SWD");

    // Default model trigger
    onManufacturerChanged("STM32");
}

void MainWindow::onManufacturerChanged(const QString& manufacturer) {
    ui->comboModel->clear();
    if (manufacturer == "STM32") {
        ui->comboModel->addItems({"STM32F407VGT6", "STM32F401RETx", "STM32F103C8", "STM32G474RETx", "STM32H743ZITx"});
        ui->comboModel->setCurrentText("STM32F407VGT6");
    } else if (manufacturer == "GD32") {
        ui->comboModel->addItems({"GD32F303CG", "GD32F103C8", "GD32E103TB"});
    } else if (manufacturer.contains("APM32")) {
        ui->comboModel->addItems({"APM32F103CB", "APM32F407VG"});
    } else if (manufacturer == "TI") {
        ui->comboModel->addItems({"MSP432P401R", "CC2652R"});
    } else if (manufacturer == "Nuvoton") {
        ui->comboModel->addItems({"M487JIDAE", "NUC126NE4AE"});
    }
}

void MainWindow::onSpeedSliderChanged(int value) {
    if (ui->spinSpeed->value() != value) {
        ui->spinSpeed->setValue(value);
    }
}

void MainWindow::onSpeedSpinChanged(int value) {
    if (m_sliderToolbarSpeed && m_sliderToolbarSpeed->value() != value) {
        m_sliderToolbarSpeed->setValue(value);
    }
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
    ui->actionRead->setEnabled(!busy && m_engine->isConnected());
    ui->actionErase->setEnabled(!busy);
    ui->actionProgram->setEnabled(!busy);
    ui->actionVerify->setEnabled(!busy);
    ui->actionResetTarget->setEnabled(!busy);
}

void MainWindow::onRefreshProbesTriggered() {
    auto probes = m_engine->listAvailableProbes();
    if (!probes.isEmpty()) {
        ui->comboProbe->clear();
        for (const auto& p : probes) {
            ui->comboProbe->addItem(p.identifier);
        }
        appendLog("INFO", QString("Found probe: %1.").arg(probes.first().identifier));
    } else {
        appendLog("WARNING", "No debug probe detected. Check the USB connection and probe driver.");
    }
}

void MainWindow::onConnectTriggered() {
    QString chip = ui->comboModel->currentText().trimmed();
    uint32_t speed = ui->spinSpeed->value();
    QString probe = ui->comboProbe->currentText();

    m_isOperationRunning = true;
    updateUiState();
    appendLog("INFO", QString("Connecting to %1 via %2 at %3 kHz...").arg(chip, ui->comboInterface->currentText()).arg(speed));
    m_engine->connectTarget(chip, speed, probe);
}

void MainWindow::onDisconnectTriggered() {
    m_engine->disconnectTarget();
}

void MainWindow::onReadTargetTriggered() {
    appendLog("INFO", "Reading memory from target...");
    // Mock read 256 bytes for memory viewer
    m_targetReadData = QByteArray(512, 0);
    for (int i = 0; i < m_targetReadData.size(); ++i) {
        m_targetReadData[i] = static_cast<char>((i * 17 + 0x33) & 0xFF);
    }
    m_targetReadBaseAddress = 0x08000000;
    ui->radioSourceTarget->setChecked(true);
    updateMemoryViewer();
    appendLog("INFO", "Read 512 bytes from target at 0x08000000 complete.");
}

void MainWindow::onEraseTriggered() {
    if (QMessageBox::question(this, "Confirm Erase",
            "Are you sure you want to completely erase the target chip flash?",
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    m_isOperationRunning = true;
    updateUiState();
    m_engine->eraseTarget(true);
}

void MainWindow::onProgramTriggered() {
    TargetConfig cfg;
    cfg.chipName = ui->comboModel->currentText().trimmed();
    cfg.speedKhz = ui->spinSpeed->value();
    cfg.autoSpeedEnabled = true;
    cfg.eraseBeforeFlash = true;
    cfg.verifyAfterFlash = true;
    cfg.resetAfterFlash = true;

    FirmwareSlot s1;
    s1.enabled = ui->checkSlot1Enabled->isChecked() && !ui->editSlot1Path->text().isEmpty();
    s1.label = "Slot 1 (Bootloader)";
    s1.filePath = ui->editSlot1Path->text().trimmed();

    FirmwareSlot s2;
    s2.enabled = ui->checkSlot2Enabled->isChecked() && !ui->editSlot2Path->text().isEmpty();
    s2.label = "Slot 2 (Application)";
    s2.filePath = ui->editSlot2Path->text().trimmed();

    m_coordinator->setTargetConfig(cfg);
    m_coordinator->setSlot1(s1);
    m_coordinator->setSlot2(s2);

    m_coordinator->startFlashSequence();
}

void MainWindow::onVerifyTriggered() {
    appendLog("INFO", "Verification starting...");
    m_engine->resetTarget(false);
}

void MainWindow::onResetTargetTriggered() {
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
    QString file = QFileDialog::getOpenFileName(this, "Select Slot 1 (Bootloader) File", "", "Firmware Files (*.hex *.bin);;All Files (*.*)");
    if (!file.isEmpty()) {
        ui->editSlot1Path->setText(file);
    }
}

void MainWindow::onOpenSlot2Clicked() {
    QString file = QFileDialog::getOpenFileName(this, "Select Slot 2 (Application) File", "", "Firmware Files (*.hex *.bin);;All Files (*.*)");
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
    } else {
        res = HexParser::analyzeBinFile(path, 0x08000000);
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

    appendLog("INFO", QString("HEX file '%1' loaded.").arg(fi.fileName()));
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
        ui->statusBar->showMessage(QString("Connected to %1 via SWD at %2 kHz").arg(chipInfo).arg(ui->spinSpeed->value()));
        appendLog("INFO", QString("Connected to %1 via SWD at %2 kHz.").arg(chipInfo).arg(ui->spinSpeed->value()), "#2ecc71");
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
    m_isOperationRunning = true;
    ui->progressBar->setValue(0);
    updateUiState();
    appendLog("INFO", "Programming sequence initiated.");
}

void MainWindow::onSequenceFinished(bool success, const QString& message) {
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

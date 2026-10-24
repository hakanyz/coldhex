#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "core/HexParser.h"
#include "core/AppSettings.h"
#include "core/ProfileStore.h"
#include "ProfileCombo.h"
#include "ProfileTransferDialog.h"
#include "core/ProfileTransfer.h"
#include <QSaveFile>
#include "Theme.h"
#include <QGroupBox>
#include <QRadioButton>
#include <QCheckBox>
#include "engine/ProbeRsLocator.h"
#include "engine/JLinkLocator.h"
#include "engine/RoutingEngine.h"
#include "SystemCheckDialog.h"
#include <QTimer>
#include <QStandardPaths>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QKeySequence>
#include <QDialog>
#include <QPushButton>
#include <QIcon>
#include <QEvent>
#include <QApplication>
#include "core/FirmwareFiles.h"
#include "HelpDialogs.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSettings>
#include <QFileDialog>
#include <QDateTime>
#include <QMessageBox>
#include <QInputDialog>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHeaderView>
#include <QTextDocument>

namespace {

// The engine for J-Link probes that keeps SEGGER's USB driver, or null when SEGGER's J-Link
// software is not installed (J-Link probes then need the WinUSB driver and go through probe-rs).
JLinkEngine* makeJLinkEngine(ProbeRsEngine* chipData, QObject* parent) {
    const QString commander = JLinkLocator::findDefault();
    return commander.isEmpty() ? nullptr : new JLinkEngine(commander, chipData, parent);
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_probeRsEngine(new ProbeRsEngine(this)),
      m_jlinkEngine(makeJLinkEngine(m_probeRsEngine, this)),
      m_engine(new RoutingEngine(m_probeRsEngine, m_jlinkEngine, this)),
      m_coordinator(new FlashCoordinator(m_engine, this)),
      m_memoryModel(new HexMemoryModel(this))
{
    ui->setupUi(this);
    fillSpeedList();

    // The Firmware Slots box takes the spare height of the settings panel, so its bottom edge
    // lines up with the bottom of the memory viewer next to it.
    ui->layoutSettings->setStretchFactor(ui->groupFirmwareSlots, 1);
    ui->layoutSlots->addStretch(1);  // keeps the slot rows at the top when the box grows

    // The console has a fixed height that shows kConsoleLines lines.
    constexpr int kConsoleLines = 12;
    ui->textLog->setFixedHeight(kConsoleLines * QFontMetrics(ui->textLog->font()).lineSpacing()
                                + 2 * ui->textLog->frameWidth()
                                + 2 * qRound(ui->textLog->document()->documentMargin()));

    m_console = new ConsoleDock(this, ui->dockConsoleOutput, ui->textLog, ui->actionClearLog, ui->actionSaveLog);
    m_chipSelector = new ChipSelector(this, ui->comboManufacturer, ui->comboModel, m_engine);
    connect(m_chipSelector, &ChipSelector::message, this, &MainWindow::appendLog);
    m_probeSelector = new ProbeSelector(this, ui->comboProbe, m_engine, [this]() { return m_isOperationRunning || m_engine->isBusy(); });
    connect(m_probeSelector, &ProbeSelector::message, this, &MainWindow::appendLog);
    connect(m_probeSelector, &ProbeSelector::status, ui->statusBar, &QStatusBar::showMessage);
    connect(m_probeSelector, &ProbeSelector::probesListed, this, &MainWindow::onProbesListed);
    m_probeInspector = new ProbeInspector(this);
    connect(m_probeInspector, &ProbeInspector::finished, this, &MainWindow::onProbeInspected);
    setAcceptDrops(true);

    setupUiCustomizations();
    setupProfileBar();
    setupFileBadges();
    setupToolBarWidgets();

    m_operationStatus = new OperationStatus(ui->statusBar, m_engine, m_coordinator, this);
    if (m_jlinkEngine) {
        appendLog("INFO", QString("J-Link software found (%1): J-Link probes keep SEGGER's USB driver.")
                              .arg(m_jlinkEngine->commanderPath()));
    }

    // Engine Signals
    connect(m_engine, &IProgrammerEngine::logMessage, this, &MainWindow::onEngineLogMessage);
    connect(m_engine, &IProgrammerEngine::connected, this, &MainWindow::onEngineConnected);
    connect(m_engine, &IProgrammerEngine::protectionDetected, this, &MainWindow::onProtectionDetected);
    connect(m_engine, &IProgrammerEngine::capabilitiesReady, this, &MainWindow::onCapabilitiesReady);
    connect(m_engine, &IProgrammerEngine::disconnected, this, &MainWindow::onEngineDisconnected);
    connect(m_engine, &IProgrammerEngine::operationFinished, this, &MainWindow::onEngineOperationFinished);

    // Coordinator Signals
    connect(m_coordinator, &FlashCoordinator::sequenceStarted, this, &MainWindow::onSequenceStarted);
    connect(m_coordinator, &FlashCoordinator::sequenceFinished, this, &MainWindow::onSequenceFinished);
    connect(m_coordinator, &FlashCoordinator::stepChanged, this, &MainWindow::onStepChanged);
    connect(m_coordinator, &FlashCoordinator::autoSpeedFallbackTriggered, this, &MainWindow::onAutoSpeedFallbackTriggered);

    // Toolbar Actions
    connect(ui->actionConnect, &QAction::triggered, this, &MainWindow::onConnectTriggered);
    connect(ui->actionDisconnect, &QAction::triggered, this, &MainWindow::onDisconnectTriggered);
    connect(ui->actionRead, &QAction::triggered, this, &MainWindow::onReadTargetTriggered);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAboutTriggered);
    connect(ui->actionErase, &QAction::triggered, this, &MainWindow::onEraseTriggered);
    connect(ui->actionSaveRead, &QAction::triggered, this, &MainWindow::onSaveReadTriggered);
    connect(ui->actionUnlock, &QAction::triggered, this, &MainWindow::onUnlockTriggered);
    connect(ui->actionLock, &QAction::triggered, this, &MainWindow::onLockTriggered);
    connect(ui->actionProgram, &QAction::triggered, this, &MainWindow::onProgramTriggered);
    connect(ui->actionVerify, &QAction::triggered, this, &MainWindow::onVerifyTriggered);
    connect(ui->actionResetTarget, &QAction::triggered, this, &MainWindow::onResetTargetTriggered);
    connect(ui->actionOpenHex, &QAction::triggered, this, &MainWindow::onOpenHexTriggered);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionRefreshProbes, &QAction::triggered, this, [this]() { m_probeSelector->refresh(true); });
    connect(ui->actionAddChipFile, &QAction::triggered, m_chipSelector, &ChipSelector::addChipFile);
    connect(ui->actionSystemCheck, &QAction::triggered, this, &MainWindow::onSystemCheckTriggered);
    connect(ui->actionWiringGuide, &QAction::triggered, this, &MainWindow::onWiringGuideTriggered);
    connect(ui->actionCancel, &QAction::triggered, this, &MainWindow::onCancelTriggered);
    connect(ui->actionClearChipFiles, &QAction::triggered, m_chipSelector, &ChipSelector::clearChipFiles);

    // Settings Panel Controls
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
    connect(ui->radioSourceBoth, &QRadioButton::toggled, this, &MainWindow::onDataSourceChanged);
    connect(ui->radioSourceTarget, &QRadioButton::toggled, this, &MainWindow::onDataSourceChanged);

    // Model selection updates memory map title
    connect(ui->comboModel, &QComboBox::currentTextChanged, [this](const QString& model) {
        const QString name = model.trimmed();
        ui->lblMemoryMapTitle->setText(name.isEmpty() ? QString("Memory Map") : QString("Memory Map (%1) - Flash").arg(name));
    });

    // Lock and Unlock exist only for some chip families: ask the helper after the chip stops changing.
    m_capabilitiesTimer = new QTimer(this);
    m_capabilitiesTimer->setSingleShot(true);
    m_capabilitiesTimer->setInterval(300);
    connect(m_capabilitiesTimer, &QTimer::timeout, this, &MainWindow::askCapabilities);
    connect(ui->comboModel, &QComboBox::currentTextChanged, this, [this]() {
        m_canLock = m_canUnlock = true;  // unknown until answered
        updateUiState();
        m_capabilitiesTimer->start();
    });

    restoreSettings();
    m_chipSelector->requestChips();

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
    appendLog("INFO", QString("ColdHex v%1 started.").arg(QApplication::applicationVersion()));
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

    m_chipSelector->restore(s.manufacturer, s.chip, s.customChipFiles);
    m_probeSelector->restore(s.probe);
    selectIfPresent(ui->comboInterface, s.interfaceName);
    setSpeedKhz(s.speedKhz);

    ui->checkEraseBeforeFlash->setChecked(s.eraseBeforeFlash);
    ui->checkVerifyAfterFlash->setChecked(s.verifyAfterFlash);
    ui->checkResetAfterFlash->setChecked(s.resetAfterFlash);
    ui->checkConnectUnderReset->setChecked(s.connectUnderReset);
    ui->editSlot1Path->setText(s.slot1Path);
    if (!s.slot1Address.isEmpty()) ui->editSlot1Address->setText(s.slot1Address);
    if (!s.slot2Address.isEmpty()) ui->editSlot2Address->setText(s.slot2Address);
    ui->editSlot2Path->setText(s.slot2Path);

    if (!s.geometry.isEmpty()) restoreGeometry(s.geometry);
    // The dock layout (windowState) is deliberately not restored: sizes saved for a window of
    // another size left the console drawn past the bottom of the window on start-up.
}

void MainWindow::saveSettings() {
    UiSettings s;
    s.manufacturer = m_chipSelector->manufacturer();
    s.chip = m_chipSelector->chipText();
    s.probe = m_probeSelector->label();
    s.interfaceName = ui->comboInterface->currentText();
    s.speedKhz = speedKhz();
    s.slot1Path = ui->editSlot1Path->text();
    s.slot2Path = ui->editSlot2Path->text();
    s.slot1Address = ui->editSlot1Address->text();
    s.slot2Address = ui->editSlot2Address->text();
    s.customChipFiles = m_chipSelector->customChipFiles();
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

    // View menu (defined in the .ui file): show or hide the side panel and the console. They can
    // also be closed with their X.
    QAction* toggleSettings = ui->dockTargetSettings->toggleViewAction();
    toggleSettings->setText("Target Settings");
    QAction* toggleConsole = ui->dockConsoleOutput->toggleViewAction();
    toggleConsole->setText("Console Output");
    toggleConsole->setShortcut(QKeySequence("F12"));
    ui->menuView->addAction(toggleSettings);
    ui->menuView->addAction(toggleConsole);

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

// Pushes the chip, speed and probe currently shown in the UI to the engine, so erase, reset
// and program never run with stale or empty settings.
bool MainWindow::prepareEngine() {
    QString chip;
    if (!m_chipSelector->resolve(chip)) return false;

    const QString descriptionFile = m_chipSelector->descriptionFileFor(chip);
    TargetSettings target;
    target.chipName = chip;
    target.speedKhz = speedKhz();
    target.probeId = m_probeSelector->selector();
    target.chipDescriptionPath = descriptionFile;
    target.protocol = (ui->comboInterface->currentText() == "JTAG") ? DebugProtocol::Jtag : DebugProtocol::Swd;
    target.connectUnderReset = ui->checkConnectUnderReset->isChecked();
    m_engine->configureTarget(target);
    return true;
}

// Returns false (after telling the user) when the chip name is not known to probe-rs.
void MainWindow::appendLog(const QString& levelStr, const QString& message) {
    m_console->append(levelStr, message);
}

namespace {
// Clock speeds offered for the debug link, in kHz. A probe uses the closest speed it supports.
const int kSpeedsKhz[] = {5, 10, 25, 50, 100, 250, 500, 1000, 2000, 4000, 6000, 8000, 10000,
                          12000, 15000, 20000, 24000, 30000, 40000, 50000};
constexpr int kDefaultSpeedKhz = 1000;
}  // namespace

void MainWindow::fillSpeedList() {
    for (int khz : kSpeedsKhz) {
        const QString text = khz >= 1000 ? QString("%1 MHz").arg(khz / 1000) : QString("%1 kHz").arg(khz);
        ui->comboSpeed->addItem(text, khz);
    }
    setSpeedKhz(kDefaultSpeedKhz);
}

int MainWindow::speedKhz() const {
    const QVariant value = ui->comboSpeed->currentData();
    return value.isValid() ? value.toInt() : kDefaultSpeedKhz;
}

void MainWindow::setSpeedKhz(int khz) {
    // The largest listed speed that is not above the wanted one (the smallest when all are above).
    int best = 0;
    for (int i = 0; i < ui->comboSpeed->count(); ++i) {
        if (ui->comboSpeed->itemData(i).toInt() <= khz) best = i;
    }
    ui->comboSpeed->setCurrentIndex(best);
}

void MainWindow::updateUiState() {
    bool busy = m_isOperationRunning || m_engine->isBusy();
    ui->actionConnect->setEnabled(!busy && !m_engine->isConnected());
    ui->actionDisconnect->setEnabled(!busy && m_engine->isConnected());
    ui->actionRead->setEnabled(!busy);
    ui->actionErase->setEnabled(!busy);
    ui->actionSaveRead->setEnabled(!busy && !m_targetReadData.isEmpty());
    ui->actionUnlock->setEnabled(!busy && m_canUnlock);
    ui->actionLock->setEnabled(!busy && m_canLock);
    ui->actionProgram->setEnabled(!busy);
    ui->actionVerify->setEnabled(!busy);
    ui->actionResetTarget->setEnabled(!busy);
    ui->actionCancel->setEnabled(busy);
    if (m_comboProfile) m_comboProfile->setEnabled(!busy);
}

void MainWindow::setupFileBadges() {
    m_lblUpdated[0] = addUpdatedBadge(ui->lblSlot1Title);
    m_lblUpdated[1] = addUpdatedBadge(ui->lblSlot2Title);
    for (int i = 0; i < 2; ++i) {
        m_fileWatch[i] = new FileWatcher(this);
        connect(m_fileWatch[i], &FileWatcher::changed, this, [this, i]() { onSlotFileChanged(i + 1); });
    }
}

// Puts a small "updated" note next to a slot title. It stays hidden until the file changes.
QLabel* MainWindow::addUpdatedBadge(QLabel* title) {
    auto* badge = new QLabel("Updated", this);
    badge->setStyleSheet(QString("color: %1; font-weight: bold;").arg(Theme::hex(Theme::Color::logSuccess)));
    badge->setToolTip("The file was rebuilt on disk. The memory view shows the new content. The note goes away after Program.");
    badge->hide();

    const int index = ui->layoutSlots->indexOf(title);
    ui->layoutSlots->removeWidget(title);
    auto* row = new QHBoxLayout;
    row->addWidget(title);
    row->addWidget(badge);
    row->addStretch();
    ui->layoutSlots->insertLayout(index, row);
    return badge;
}

// A different file is not an update of the old one, so the note goes away with it.
void MainWindow::watchSlotFile(int slotIndex, const QString& path) {
    if (!m_fileWatch[slotIndex - 1]) return;
    if (m_fileWatch[slotIndex - 1]->path() == path) return;
    m_lblUpdated[slotIndex - 1]->hide();
    m_fileWatch[slotIndex - 1]->setFile(path);
}

void MainWindow::onSlotFileChanged(int slotIndex) {
    const QString path = (slotIndex == 1 ? ui->editSlot1Path : ui->editSlot2Path)->text();
    ++m_changeCount[slotIndex - 1];
    m_lblUpdated[slotIndex - 1]->show();
    appendLog("INFO", QString("File '%1' changed on disk, reloaded.").arg(QFileInfo(path).fileName()));
    loadFileIntoSlot(slotIndex, path);
}

void MainWindow::updateHexFileCombo() {
    if (!m_comboToolbarHexFile) return;
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

// The profile box sits at the top of the Target Settings panel, next to the settings it stores.
void MainWindow::setupProfileBar() {
    m_comboProfile = new ProfileCombo(this);

    auto* box = new QGroupBox("Profiles", this);
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->addWidget(m_comboProfile);
    ui->layoutSettings->insertWidget(0, box);

    connect(m_comboProfile, &ProfileCombo::profileChosen, this, [this](const QString& name) {
        QSettings settings;
        Profile profile;
        if (ProfileStore::load(settings, name, profile)) applyProfile(name, profile);
    });
    connect(m_comboProfile, &ProfileCombo::saveRequested, this, &MainWindow::saveProfileAs);
    connect(m_comboProfile, &ProfileCombo::updateRequested, this, &MainWindow::updateProfile);
    connect(m_comboProfile, &ProfileCombo::renameRequested, this, &MainWindow::renameProfile);
    connect(m_comboProfile, &ProfileCombo::deleteRequested, this, &MainWindow::deleteProfile);

    // Anything the profile stores changing marks the shown profile as modified.
    auto changed = [this]() { updateProfileMarker(); };
    connect(ui->comboManufacturer, &QComboBox::currentTextChanged, this, changed);
    connect(ui->comboModel, &QComboBox::currentTextChanged, this, changed);
    connect(ui->comboInterface, &QComboBox::currentTextChanged, this, changed);
    connect(ui->comboSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
    for (QCheckBox* check : {ui->checkEraseBeforeFlash, ui->checkVerifyAfterFlash,
                             ui->checkResetAfterFlash, ui->checkConnectUnderReset}) {
        connect(check, &QCheckBox::toggled, this, changed);
    }
    for (QLineEdit* edit : {ui->editSlot1Path, ui->editSlot2Path, ui->editSlot1Address, ui->editSlot2Address}) {
        connect(edit, &QLineEdit::textChanged, this, changed);
    }
    for (QRadioButton* radio : {ui->radioSourceFile1, ui->radioSourceFile2, ui->radioSourceBoth}) {
        connect(radio, &QRadioButton::toggled, this, changed);
    }

    // Import and Export sit in the File menu, above Exit.
    auto* importAct = new QAction("Import Profiles...", this);
    auto* exportAct = new QAction("Export Profiles...", this);
    connect(importAct, &QAction::triggered, this, &MainWindow::importProfiles);
    connect(exportAct, &QAction::triggered, this, &MainWindow::exportProfiles);
    ui->menuFile->insertAction(ui->actionExit, importAct);
    ui->menuFile->insertAction(ui->actionExit, exportAct);
    ui->menuFile->insertSeparator(ui->actionExit);

    refreshProfileCombo();
}

void MainWindow::refreshProfileCombo() {
    QSettings settings;
    m_comboProfile->setProfiles(ProfileStore::names(settings), m_currentProfile);
    updateProfileMarker();
}

// Shows "name *" when the settings differ from what the profile holds.
void MainWindow::updateProfileMarker() {
    if (m_applyingProfile || !m_comboProfile) return;
    m_comboProfile->setModified(!m_currentProfile.isEmpty() && !(collectProfile() == m_profileSnapshot));
}
Profile MainWindow::collectProfile() const {
    Profile p;
    p.manufacturer = m_chipSelector->manufacturer();
    p.chip = m_chipSelector->chipText();
    p.interfaceName = ui->comboInterface->currentText();
    p.speedKhz = speedKhz();
    p.slot1Path = ui->editSlot1Path->text();
    p.slot2Path = ui->editSlot2Path->text();
    p.slot1Address = ui->editSlot1Address->text();
    p.slot2Address = ui->editSlot2Address->text();
    if (ui->radioSourceBoth->isChecked()) {
        p.firmware = FirmwareChoice::Both;
    } else if (ui->radioSourceFile1->isChecked()) {
        p.firmware = FirmwareChoice::File1;
    } else {
        p.firmware = FirmwareChoice::File2;
    }
    p.eraseBeforeFlash = ui->checkEraseBeforeFlash->isChecked();
    p.verifyAfterFlash = ui->checkVerifyAfterFlash->isChecked();
    p.resetAfterFlash = ui->checkResetAfterFlash->isChecked();
    p.connectUnderReset = ui->checkConnectUnderReset->isChecked();
    return p;
}

void MainWindow::applyProfile(const QString& name, const Profile& p) {
    m_applyingProfile = true;
    m_chipSelector->select(p.manufacturer, p.chip);
    const int ifaceIdx = ui->comboInterface->findText(p.interfaceName);
    if (ifaceIdx >= 0) ui->comboInterface->setCurrentIndex(ifaceIdx);
    setSpeedKhz(p.speedKhz);

    ui->checkEraseBeforeFlash->setChecked(p.eraseBeforeFlash);
    ui->checkVerifyAfterFlash->setChecked(p.verifyAfterFlash);
    ui->checkResetAfterFlash->setChecked(p.resetAfterFlash);
    ui->checkConnectUnderReset->setChecked(p.connectUnderReset);

    // Addresses first, and the files loaded by hand afterwards: a .bin file is read at its
    // address, and a profile may keep the path of the file that is already shown.
    if (!p.slot1Address.isEmpty()) ui->editSlot1Address->setText(p.slot1Address);
    if (!p.slot2Address.isEmpty()) ui->editSlot2Address->setText(p.slot2Address);
    {
        const QSignalBlocker block1(ui->editSlot1Path);
        const QSignalBlocker block2(ui->editSlot2Path);
        ui->editSlot1Path->setText(p.slot1Path);
        ui->editSlot2Path->setText(p.slot2Path);
    }
    loadFileIntoSlot(1, p.slot1Path);
    loadFileIntoSlot(2, p.slot2Path);

    switch (p.firmware) {
    case FirmwareChoice::File1: ui->radioSourceFile1->setChecked(true); break;
    case FirmwareChoice::Both: ui->radioSourceBoth->setChecked(true); break;
    case FirmwareChoice::File2: ui->radioSourceFile2->setChecked(true); break;
    }

    m_applyingProfile = false;
    m_currentProfile = name;
    m_profileSnapshot = collectProfile();
    refreshProfileCombo();
    appendLog("INFO", QString("Profile '%1' loaded.").arg(name));
}

void MainWindow::saveProfileAs() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "Save Profile", "Profile name:", QLineEdit::Normal,
                                         QString(), &ok).trimmed();
    if (!ok) return;
    if (!ProfileStore::isValidName(name)) {
        QMessageBox::warning(this, "Save Profile", "Please enter a name. It cannot contain / or \\.");
        return;
    }

    QSettings settings;
    const QStringList names = ProfileStore::names(settings);
    const int existing = names.indexOf(QRegularExpression(QRegularExpression::escape(name),
                                                          QRegularExpression::CaseInsensitiveOption));
    if (existing >= 0) {
        name = names.at(existing);
        const auto answer = QMessageBox::question(this, "Save Profile",
            QString("A profile named '%1' already exists. Replace it?").arg(name));
        if (answer != QMessageBox::Yes) return;
    }

    ProfileStore::save(settings, name, collectProfile());
    m_currentProfile = name;
    m_profileSnapshot = collectProfile();
    refreshProfileCombo();
    appendLog("INFO", QString("Profile '%1' saved.").arg(name));
}

void MainWindow::updateProfile(const QString& name) {
    if (name != m_currentProfile) {
        const auto answer = QMessageBox::question(this, "Update Profile",
            QString("Replace the profile '%1' with the current settings?").arg(name));
        if (answer != QMessageBox::Yes) return;
    }
    QSettings settings;
    ProfileStore::save(settings, name, collectProfile());
    m_currentProfile = name;
    m_profileSnapshot = collectProfile();
    // The list may be open: change the selection without rebuilding it.
    m_comboProfile->selectProfile(name);
    m_comboProfile->markSaved(name);
    appendLog("INFO", QString("Profile '%1' updated.").arg(name));
}

void MainWindow::renameProfile(const QString& oldName) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Rename Profile", "New name:", QLineEdit::Normal,
                                               oldName, &ok).trimmed();
    if (!ok || name == oldName) return;
    if (!ProfileStore::isValidName(name)) {
        QMessageBox::warning(this, "Rename Profile", "Please enter a name. It cannot contain / or \\.");
        return;
    }

    QSettings settings;
    for (const QString& existing : ProfileStore::names(settings)) {
        // A different capitalisation of the same name is a plain rename (names are not case sensitive).
        if (existing.compare(name, Qt::CaseInsensitive) == 0 && existing.compare(oldName, Qt::CaseInsensitive) != 0) {
            QMessageBox::warning(this, "Rename Profile", QString("A profile named '%1' already exists.").arg(existing));
            return;
        }
    }

    Profile profile;
    if (!ProfileStore::load(settings, oldName, profile)) return;
    ProfileStore::remove(settings, oldName);
    ProfileStore::save(settings, name, profile);
    if (m_currentProfile == oldName) m_currentProfile = name;
    refreshProfileCombo();
    appendLog("INFO", QString("Profile '%1' renamed to '%2'.").arg(oldName, name));
}
void MainWindow::exportProfiles() {
    QSettings settings;
    const QStringList names = ProfileStore::names(settings);
    if (names.isEmpty()) {
        QMessageBox::information(this, "Export Profiles", "There are no profiles to export yet. Save one first with the Profiles box.");
        return;
    }

    bool includePaths = false;
    const QStringList chosen = ProfileTransferDialog::askExport(this, names, includePaths);
    if (chosen.isEmpty()) return;

    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("coldhex-profiles.json");
    const QString path = QFileDialog::getSaveFileName(this, "Export Profiles", suggested, "ColdHex profiles (*.json);;All Files (*.*)");
    if (path.isEmpty()) return;

    QList<NamedProfile> profiles;
    for (const QString& name : chosen) {
        Profile profile;
        if (ProfileStore::load(settings, name, profile)) profiles.append({name, profile});
    }

    const QByteArray json = ProfileTransfer::toJson(profiles, QApplication::applicationVersion(), includePaths,
                                                    QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        QMessageBox::warning(this, "Export Profiles", QString("Could not write '%1':\n%2").arg(path, file.errorString()));
        return;
    }
    appendLog("INFO", QString("Exported %1 profile(s) to '%2'.").arg(profiles.size()).arg(QFileInfo(path).fileName()));
}

void MainWindow::importProfiles() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Import Profiles", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "ColdHex profiles (*.json);;All Files (*.*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import Profiles", QString("Could not read '%1':\n%2").arg(path, file.errorString()));
        return;
    }
    const QByteArray data = file.read(ProfileTransfer::kMaxFileBytes + 1);
    const ImportResult result = ProfileTransfer::fromJson(data, QFileInfo(path).absolutePath());
    if (!result.ok()) {
        QString message = result.error;
        if (!result.skipped.isEmpty()) message += "\n\n" + result.skipped.join('\n');
        QMessageBox::warning(this, "Import Profiles", message);
        return;
    }

    QSettings settings;
    QStringList existing = ProfileStore::names(settings);
    ProfileTransferDialog::ImportChoice choice;
    if (!ProfileTransferDialog::askImport(this, result.profiles, existing, choice)) return;

    int imported = 0;
    int replaced = 0;
    int renamed = 0;
    int skipped = 0;
    int missingFiles = 0;
    for (const NamedProfile& incoming : choice.profiles) {
        QString name = incoming.name;
        int clash = -1;
        for (int i = 0; i < existing.size(); ++i) {
            if (existing[i].compare(name, Qt::CaseInsensitive) == 0) clash = i;
        }
        if (clash >= 0) {
            if (choice.conflict == ProfileTransferDialog::Conflict::Skip) {
                ++skipped;
                continue;
            }
            if (choice.conflict == ProfileTransferDialog::Conflict::Replace) {
                name = existing[clash];
                ++replaced;
            } else {
                name = ProfileTransfer::uniqueName(incoming.name, existing);
                ++renamed;
            }
        }
        ProfileStore::save(settings, name, incoming.profile);
        if (!existing.contains(name, Qt::CaseInsensitive)) existing << name;
        ++imported;
        for (const QString& file : {incoming.profile.slot1Path, incoming.profile.slot2Path}) {
            if (!file.isEmpty() && !QFileInfo::exists(file)) ++missingFiles;
        }
    }

    refreshProfileCombo();
    QString summary = QString("Imported %1 profile(s) from '%2'").arg(imported).arg(QFileInfo(path).fileName());
    QStringList notes;
    if (replaced) notes << QString("%1 replaced").arg(replaced);
    if (renamed) notes << QString("%1 renamed").arg(renamed);
    if (skipped) notes << QString("%1 skipped").arg(skipped);
    if (!notes.isEmpty()) summary += " (" + notes.join(", ") + ")";
    appendLog("INFO", summary + ".");
    if (missingFiles > 0) {
        appendLog("WARNING", QString("%1 firmware file(s) named in the imported profiles were not found on this computer.").arg(missingFiles));
    }
    if (!result.skipped.isEmpty()) {
        QMessageBox::information(this, "Import Profiles",
                                 "Some entries in the file were not imported:\n\n" + result.skipped.join('\n'));
    }
}
void MainWindow::deleteProfile(const QString& name) {
    const auto answer = QMessageBox::question(this, "Delete Profile",
        QString("Delete the profile '%1'?").arg(name));
    if (answer != QMessageBox::Yes) return;

    QSettings settings;
    ProfileStore::remove(settings, name);
    if (m_currentProfile == name) m_currentProfile.clear();
    refreshProfileCombo();
    appendLog("INFO", QString("Profile '%1' deleted.").arg(name));
}

// Fills the probe box with what probe-rs found. Automatic refreshes only write to the log when
// the set of probes changed; a manual refresh always does.
// Probes are plugged in while the program runs: look again whenever the window is activated.
void MainWindow::onConnectTriggered() {
    m_console->ensureVisible();
    if (!prepareEngine()) return;
    const QString chip = m_chipSelector->chipText();
    const uint32_t speed = speedKhz();
    const QString probe = m_probeSelector->selector();

    m_manualConnect = true;
    m_isOperationRunning = true;
    updateUiState();
    appendLog("INFO", QString("Connecting to %1 via %2 at %3 kHz...").arg(chip, ui->comboInterface->currentText()).arg(speed));
    m_engine->connectTarget(chip, speed, probe);
}

void MainWindow::onDisconnectTriggered() {
    m_engine->disconnectTarget();
}

void MainWindow::onEraseTriggered() {
    m_console->ensureVisible();
    if (QMessageBox::question(this, "Confirm Erase",
            "Are you sure you want to completely erase the target chip flash?",
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!prepareEngine()) return;

    m_isOperationRunning = true;
    m_lastCommandWasErase = true;
    updateUiState();
    m_engine->eraseTarget(true);
}

void MainWindow::onLockTriggered() {
    m_console->ensureVisible();
    if (QMessageBox::warning(this, "Enable Read Protection",
            "This sets the read-out protection to level 1: the flash of the chip can no longer be "
            "read through the debug port.\n\n"
            "Level 1 can be removed again with Unlock, but that ERASES THE WHOLE FLASH.\n\nContinue?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    if (!prepareEngine()) return;

    m_isOperationRunning = true;
    updateUiState();
    m_engine->lockTarget();
}

void MainWindow::askCapabilities() {
    const QString chip = m_chipSelector->chipText().trimmed();
    if (!chip.isEmpty()) m_engine->requestCapabilities(chip);
}

// Greys out Lock and Unlock for chips without a recipe and says why in the tooltip.
void MainWindow::onCapabilitiesReady(const QString& chipName, bool canLock, bool canUnlock) {
    if (chipName != m_chipSelector->chipText().trimmed()) return;  // an older question
    m_canLock = canLock;
    m_canUnlock = canUnlock;

    const QString none = tr("Not available for %1: no verified method for this chip family yet.").arg(chipName);
    ui->actionLock->setToolTip(canLock ? tr("Enable read-out protection level 1 (the flash can no longer be read)")
                                       : none);
    ui->actionUnlock->setToolTip(canUnlock ? tr("Remove read-out protection level 1; the flash is erased") : none);
    updateUiState();
}

// The chip answered, but a read-protected one answers too: say what the protection is. For
// level 1 offer the way out right away, like ST-LINK Utility does.
void MainWindow::onProtectionDetected(int level) {
    if (level == 0) {
        appendLog("INFO", "Read protection: off.");
        return;
    }
    if (level >= 2) {
        appendLog("ERROR", "Read protection: level 2. This is permanent: the chip cannot be read, "
                           "programmed or unlocked any more.");
        return;
    }
    appendLog("WARN", "Read protection: level 1. The flash cannot be read or programmed until the "
                      "protection is removed (this erases the whole flash).");
    if (m_isOperationRunning || m_engine->isBusy()) return;
    if (QMessageBox::question(this, "Chip is read-protected",
            "The chip has read-out protection level 1: its flash cannot be read or programmed.\n\n"
            "Remove the protection now? The chip ERASES ITS WHOLE FLASH while it does this.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        startUnlock();
    }
}

void MainWindow::onUnlockTriggered() {
    m_console->ensureVisible();
    if (QMessageBox::warning(this, "Remove Read Protection",
            "This removes the read-out protection (level 1) of the chip.\n\n"
            "The chip ERASES ITS WHOLE FLASH while it does this; the firmware on it cannot be "
            "recovered. A chip with protection level 2 cannot be unlocked.\n\nContinue?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    startUnlock();
}

// Starts the unlock; the user has already confirmed it.
void MainWindow::startUnlock() {
    m_console->ensureVisible();
    if (!prepareEngine()) return;

    m_isOperationRunning = true;
    m_lastCommandWasErase = true;  // the flash is empty afterwards, like after an erase
    updateUiState();
    m_engine->unlockTarget();
}

// Collects target settings and both slots from the UI and checks them. Returns false (after
// telling the user why) when the run must not start.
bool MainWindow::prepareFirmwareRun(TargetConfig& cfg, FirmwareSlot& s1, FirmwareSlot& s2) {
    if (!m_chipSelector->resolve(cfg.chipName)) return false;
    cfg.speedKhz = speedKhz();
    cfg.probeId = m_probeSelector->selector();
    cfg.autoSpeedEnabled = true;
    cfg.eraseBeforeFlash = ui->checkEraseBeforeFlash->isChecked();
    cfg.verifyAfterFlash = ui->checkVerifyAfterFlash->isChecked();
    cfg.resetAfterFlash = ui->checkResetAfterFlash->isChecked();

    if (ui->radioSourceTarget->isChecked()) {
        const QString msg = "Choose File 1, File 2 or File 1 + File 2 under Data Source first. Read from Target is not a firmware file.";
        appendLog("ERROR", msg);
        QMessageBox::warning(this, "No Firmware Selected", msg);
        return false;
    }

    AddressRange range1;
    AddressRange range2;
    if (!buildSlot(1, s1, range1) || !buildSlot(2, s2, range2)) {
        return false;
    }

    if (s1.enabled && s2.enabled && SlotValidator::overlaps(range1, range2)) {
        const QString msg = QString("File 1 (0x%1 - 0x%2) and File 2 (0x%3 - 0x%4) use the same flash area, so they cannot both be written. "
                                    "If they are for different boards, pick only one of them under Data Source. "
                                    "Writing both would overwrite the first one.")
                                .arg(range1.start, 8, 16, QLatin1Char('0'))
                                .arg(uint64_t(range1.start) + range1.size - 1, 8, 16, QLatin1Char('0'))
                                .arg(range2.start, 8, 16, QLatin1Char('0'))
                                .arg(uint64_t(range2.start) + range2.size - 1, 8, 16, QLatin1Char('0'));
        appendLog("ERROR", msg);
        QMessageBox::warning(this, "Address Overlap", msg);
        return false;
    }

    if (!prepareEngine()) return false;

    // Refuse to write outside the chip's flash. Skipped when the memory layout is unknown.
    const ChipMemoryMap memory = m_engine->queryMemoryMap(cfg.chipName, m_chipSelector->descriptionFileFor(cfg.chipName));
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
            appendLog("ERROR", msg);
            QMessageBox::warning(this, "Outside Flash Range", msg);
            return false;
        }
    }
    return true;
}

void MainWindow::onProgramTriggered() {
    m_console->ensureVisible();
    TargetConfig cfg;
    FirmwareSlot s1;
    FirmwareSlot s2;
    if (!prepareFirmwareRun(cfg, s1, s2)) return;

    m_coordinator->setTargetConfig(cfg);
    m_coordinator->setSlot1(s1);
    m_coordinator->setSlot2(s2);

    // Remember what this run writes: once it succeeds those files are no longer "updated".
    m_programRunning = true;
    m_programmedSlot[0] = s1.enabled;
    m_programmedSlot[1] = s2.enabled;
    m_changeCountAtProgram[0] = m_changeCount[0];
    m_changeCountAtProgram[1] = m_changeCount[1];

    m_coordinator->startFlashSequence();
}

void MainWindow::onVerifyTriggered() {
    m_console->ensureVisible();
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
    const bool checked = ui->radioSourceBoth->isChecked() || (first ? ui->radioSourceFile1 : ui->radioSourceFile2)->isChecked();

    slot.label = first ? "File 1" : "File 2";
    slot.filePath = path;
    slot.enabled = checked && !path.isEmpty();
    if (!slot.enabled) return true;

    uint32_t address = 0;
    if (!slotAddress(slotIndex, address)) {
        const QString msg = QString("%1: invalid start address.").arg(slot.label);
        appendLog("ERROR", msg);
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
        appendLog("ERROR", msg);
        QMessageBox::warning(this, "Invalid Firmware File", msg);
        return false;
    }

    slot.startAddress = address;
    range.start = address;
    range.size = res.size;
    return true;
}

void MainWindow::onResetTargetTriggered() {
    m_console->ensureVisible();
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
    QString file = QFileDialog::getOpenFileName(this, "Select File 1", "", "Firmware Files (*.hex *.bin *.elf *.axf);;All Files (*.*)");
    if (!file.isEmpty()) {
        ui->editSlot1Path->setText(file);
    }
}

void MainWindow::onOpenSlot2Clicked() {
    QString file = QFileDialog::getOpenFileName(this, "Select File 2", "", "Firmware Files (*.hex *.bin *.elf *.axf);;All Files (*.*)");
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
    watchSlotFile(slotIndex, path);
    if (path.isEmpty()) {
        (slotIndex == 1 ? m_slot1ParseResult : m_slot2ParseResult) = HexParseResult();
        updateHexFileCombo();
        updateMemoryViewer();
        return;
    }

    QFileInfo fi(path);
    if (!fi.exists()) {
        // The file was moved or renamed since it was chosen: drop what was loaded from it.
        (slotIndex == 1 ? m_slot1ParseResult : m_slot2ParseResult) = HexParseResult();
        appendLog("ERROR", QString("File '%1' not found.").arg(path));
        updateMemoryViewer();
        return;
    }

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
        appendLog("ERROR", QString("Cannot load '%1': %2").arg(fi.fileName(), res.errorMessage));
    }

    if (slotIndex == 1) {
        m_slot1ParseResult = res;
    } else {
        m_slot2ParseResult = res;
    }

    updateHexFileCombo();


    if (res.success) {
        appendLog("INFO", QString("File '%1' loaded.").arg(fi.fileName()));
    }
    updateMemoryViewer();
}

void MainWindow::onDataSourceChanged() {
    updateMemoryViewer();
}

void MainWindow::updateMemoryViewer() {
    if (ui->radioSourceFile1->isChecked() || ui->radioSourceBoth->isChecked()) {
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
    switch (level) {
    case EngineLogLevel::Debug: levelStr = "DEBUG"; break;
    case EngineLogLevel::Info: levelStr = "INFO"; break;
    case EngineLogLevel::Warning: levelStr = "WARN"; break;
    case EngineLogLevel::Error: levelStr = "ERROR"; break;
    case EngineLogLevel::Success: levelStr = "SUCCESS"; break;
    }
    appendLog(levelStr, message);
}

void MainWindow::onEngineConnected(bool success, const QString& chipInfo) {
    m_isOperationRunning = false;
    updateUiState();
    if (success) {
        m_connectedProbe = m_probeSelector->selector();
        ui->statusBar->showMessage(QString("Target answered via %1 at %2 kHz (chip: %3, as selected)").arg(ui->comboInterface->currentText()).arg(speedKhz()).arg(chipInfo));
        // The wires work and the target answered. The chip name is the user's selection: nothing here
        // checks that the chip behind the probe really is that one.
        appendLog("INFO", QString("The target answered via %1 at %2 kHz. Chip: %3 (as selected, not verified).").arg(ui->comboInterface->currentText()).arg(speedKhz()).arg(chipInfo));
    } else {
        ui->statusBar->showMessage("Connection Failed");
        appendLog("ERROR", "Connection failed.");

        // The user pressed Connect: explain what is probably wrong and offer the wiring guide.
        // (A failed connect inside a flash sequence is handled by the sequence itself.)
        if (m_manualConnect) {
            QMessageBox box(QMessageBox::Warning, "Connection Failed", chipInfo, QMessageBox::Ok, this);
            QPushButton* guide = box.addButton("Wiring Guide...", QMessageBox::HelpRole);
            box.exec();
            if (box.clickedButton() == guide) onWiringGuideTriggered();
        }
    }
    m_manualConnect = false;
}

// The probe was unplugged while the program shows "connected": nothing else would tell it, so
// the connection is closed here. Not during an operation: that one fails by itself.
// Asks each newly seen probe once who it is and which voltage it sees, and writes the answer to
// the console. A probe that is unplugged and plugged in again is asked again.
void MainWindow::onProbesListed(const QStringList& selectors) {
    m_inspectedProbes.intersect(QSet<QString>(selectors.begin(), selectors.end()));
    const bool idle = !m_engine->isConnected() && !m_isOperationRunning && !m_engine->isBusy();
    if (idle) {
        for (const QString& selector : selectors) {
            if (m_inspectedProbes.contains(selector)) continue;
            m_inspectedProbes.insert(selector);
            m_probeInspector->inspect(selector,
                                      m_jlinkEngine ? m_jlinkEngine->commanderPath() : QString(),
                                      m_probeRsEngine->helperPath());
        }
    }

    if (!m_engine->isConnected() || m_isOperationRunning || m_engine->isBusy()) return;
    if (ProbeRsEngine::probeStillListed(m_connectedProbe, selectors)) return;

    appendLog("WARNING", "The probe was unplugged. The connection is closed.");
    m_engine->disconnectTarget();
}

void MainWindow::onProbeInspected(const QString& selector, const ProbeDetails& details, bool ok, const QString& error) {
    if (ok) {
        appendLog("INFO", QString("Probe %1: %2").arg(selector, ProbeInspect::describe(details)));
    } else {
        appendLog("WARNING", QString("Probe %1: could not read its details: %2").arg(selector, error));
    }
}

void MainWindow::onEngineDisconnected() {
    m_isOperationRunning = false;
    ui->statusBar->showMessage("Disconnected");
    updateUiState();
}

void MainWindow::onSequenceStarted() {
    m_sequenceRunning = true;
    m_isOperationRunning = true;
    updateUiState();
    appendLog("INFO", "Programming sequence initiated.");
}

void MainWindow::onSequenceFinished(bool success, const QString& message) {
    if (m_programRunning && success) {
        for (int i = 0; i < 2; ++i) {
            // A file that changed again while it was being written stays marked.
            if (m_programmedSlot[i] && m_changeCount[i] == m_changeCountAtProgram[i]) m_lblUpdated[i]->hide();
        }
    }
    m_programRunning = false;
    m_sequenceRunning = false;
    m_isOperationRunning = false;
    updateUiState();
    if (success) {
        appendLog("SUCCESS", message);
    } else {
        appendLog("ERROR", message);
    }
}

void MainWindow::onStepChanged(const QString& stepDescription, int stepIndex, int totalSteps) {
    appendLog("INFO", QString("[%1/%2] %3").arg(stepIndex).arg(totalSteps).arg(stepDescription));
}

void MainWindow::onAutoSpeedFallbackTriggered(uint32_t newSpeedKhz) {
    appendLog("WARN", QString("Auto-Speed fallback triggered -> %1 kHz. Retrying...").arg(newSpeedKhz));
    setSpeedKhz(static_cast<int>(newSpeedKhz));
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
void MainWindow::onCancelTriggered() {
    appendLog("WARNING", "Stopping the running operation...");
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
            appendLog("SUCCESS", QString("Read %1 bytes. They are shown in the memory view; press Save to keep them as a file.")
                                     .arg(m_targetReadData.size()));
        } else if (success) {
            appendLog("ERROR", "The read finished, but its data could not be opened: " + file);
        }
        QFile::remove(file);  // only a hand-over file between the engine and the window
        updateUiState();
        return;
    }

    if (m_sequenceRunning) return;
    m_isOperationRunning = false;
    updateUiState();

    if (m_lastCommandWasErase) {
        m_lastCommandWasErase = false;
        if (success) {
            // The data read earlier no longer matches the chip, so drop it instead of showing it.
            m_targetReadData.clear();
            updateUiState();
            updateMemoryViewer();
            appendLog("INFO", "Flash erased. Press Read to see the new contents (an erased flash reads 0xFF).");
        }
    }
}

void MainWindow::onReadTargetTriggered() {
    m_console->ensureVisible();
    if (!prepareEngine()) return;

    const QString chip = m_chipSelector->chipText();
    const ChipMemoryMap memory = m_engine->queryMemoryMap(chip, m_chipSelector->descriptionFileFor(chip));
    const MemoryRegion* flash = memory.mainFlash();
    if (!flash) {
        const QString msg = "The flash size of this chip is unknown, so the whole flash cannot be read.";
        appendLog("ERROR", msg);
        QMessageBox::warning(this, "Read Flash", msg);
        return;
    }

    // The engines write the data to a file; read into a temporary one and show it. Keeping it
    // is a separate step (Save), so Read does not ask where to put something the user only
    // wants to look at.
    const QString file = QDir::temp().filePath(
        QString("coldhex_read_%1.bin").arg(QCoreApplication::applicationPid()));

    m_pendingReadFile = file;
    m_pendingReadBase = static_cast<uint32_t>(flash->start);
    m_isOperationRunning = true;
    updateUiState();
    appendLog("INFO", QString("Reading %1 KiB of flash from 0x%2...")
                          .arg(flash->size / 1024).arg(flash->start, 8, 16, QLatin1Char('0')));
    m_engine->readMemory(flash->start, flash->size, file);
}

// Keeps what the last Read showed: writes the bytes to a .bin file the user picks.
void MainWindow::onSaveReadTriggered() {
    if (m_targetReadData.isEmpty()) return;

    const QString chip = m_chipSelector->chipText().trimmed();
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                  .filePath((chip.isEmpty() ? QString("target") : chip) + "_flash.bin");
    const QString file = QFileDialog::getSaveFileName(this, "Save Flash Contents As", suggested,
                                                      "Binary (*.bin);;All Files (*.*)");
    if (file.isEmpty()) return;

    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(m_targetReadData) != m_targetReadData.size()) {
        const QString msg = "Cannot write " + file;
        appendLog("ERROR", msg);
        QMessageBox::warning(this, "Save", msg);
        return;
    }
    appendLog("SUCCESS", QString("Saved %1 bytes to %2").arg(m_targetReadData.size()).arg(file));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && FirmwareFiles::isFirmwareFile(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
}

// Dropped firmware files go to the slots: bootloader-like names to Slot 1, everything else to
// Slot 2. Two files without such a name fill Slot 1 and Slot 2 in the order they were dropped.
// Asks probe-rs for the probe list now and once more shortly after: right after a probe is
// plugged in, the driver may not be ready yet.

// A slim title bar for the console with two small buttons: minimize (keeps only the bar, click
// again to restore) and close (hides the console until the next action reopens it).
// A closed console opens again when the user starts something. A minimized one stays small.
void MainWindow::onAboutTriggered() {
    HelpDialogs::showAbout(this);
}

void MainWindow::onWiringGuideTriggered() {
    HelpDialogs::showWiringGuide(this);
}

// Dropped firmware files go to the slots (see FirmwareFiles::assignDroppedFiles).
void MainWindow::dropEvent(QDropEvent* event) {
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) files << url.toLocalFile();
    }

    bool ignoredExtra = false;
    const auto assignments = FirmwareFiles::assignDroppedFiles(files, &ignoredExtra);
    if (assignments.isEmpty()) return;

    for (const auto& a : assignments) {
        const bool toSlot1 = (a.slot == 1);
        (toSlot1 ? ui->editSlot1Path : ui->editSlot2Path)->setText(a.file);
        appendLog("INFO", QString("%1 -> %2").arg(QFileInfo(a.file).fileName(),
                                                   toSlot1 ? "File 1" : "File 2"));
    }
    if (assignments.size() > 1) {
        ui->radioSourceBoth->setChecked(true);
    } else {
        (assignments.first().slot == 1 ? ui->radioSourceFile1 : ui->radioSourceFile2)->setChecked(true);
    }
    if (ignoredExtra) appendLog("WARNING", "Only the first two dropped files were used.");
    event->acceptProposedAction();
}

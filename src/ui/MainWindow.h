#pragma once

#include <QMainWindow>
#include <QToolButton>
#include <QLabel>
#include <QTimer>
#include <QComboBox>
#include <QLabel>
#include "core/FlashCoordinator.h"
#include "core/FirmwareTarget.h"
#include "core/HexParser.h"
#include "core/SlotValidator.h"
#include "core/ProfileStore.h"
#include "core/UpdateInfo.h"
#include "engine/SystemCheck.h"
#include "core/FileWatcher.h"
#include "engine/ProbeRsEngine.h"
#include "engine/JLinkEngine.h"
#include "engine/ProbeInspector.h"
#include <QSet>
#include "HexMemoryModel.h"
#include "ConsoleDock.h"
#include "OperationStatus.h"
#include "ChipSelector.h"
#include "ProbeSelector.h"

class QMenu;
class ProfileCombo;
class Updater;
class QThread;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    // Toolbar Actions
    void onConnectTriggered();
    void onDisconnectTriggered();
    void onEraseTriggered();
    void onSaveReadTriggered();
    void onUnlockTriggered();
    void onLockTriggered();
    void onProgramTriggered();
    void onVerifyTriggered();
    void onResetTargetTriggered();
    void onOpenHexTriggered();

    // Settings Panel Controls
    void onSystemCheckTriggered();
    void onWiringGuideTriggered();
    void onReadTargetTriggered();
    void onAboutTriggered();
    void onCancelTriggered();
    void onEngineOperationFinished(bool success, const QString& message);
    void onOpenSlot1Clicked();
    void onOpenSlot2Clicked();
    void onSlot1PathChanged(const QString& path);
    void onSlot2PathChanged(const QString& path);

    // Data Source Selection
    void onDataSourceChanged();

    // Engine and Coordinator Signals
    void onEngineLogMessage(const QString& message, EngineLogLevel level);
    void onEngineConnected(bool success, const QString& chipInfo);
    void onProtectionDetected(int level);
    void onCapabilitiesReady(const QString& chipName, bool canLock, bool canUnlock);
    void askCapabilities();
    void startUnlock();
    void onEngineDisconnected();
    void onProbesListed(const QStringList& selectors);
    void onProbeInspected(const QString& selector, const ProbeDetails& details, bool ok, const QString& error);
    void onSequenceStarted();
    void onSequenceFinished(bool success, const QString& message);
    void onStepChanged(const QString& stepDescription, int stepIndex, int totalSteps);
    void onAutoSpeedFallbackTriggered(uint32_t newSpeedKhz);

private:
    void setupUiCustomizations();
    void setupToolBarWidgets();
    void updateUiState();
    // The speed list: fixed values, in kHz. speedKhz() is the chosen one; setSpeedKhz() picks
    // the closest listed value that is not above the wanted one.
    void fillSpeedList();
    int speedKhz() const;
    void setSpeedKhz(int khz);
    void appendLog(const QString& levelStr, const QString& message);
    void loadFileIntoSlot(int slotIndex, const QString& filePath);
    void updateMemoryViewer();
    void restoreSettings();
    void saveSettings();

    // Profiles menu: named sets of target and firmware settings.
    void updateHexFileCombo();

    // A file chosen for File 1 / File 2 that is rebuilt on disk: reload it and show "updated".
    void setupFileBadges();
    QLabel* addUpdatedBadge(QLabel* title);
    void watchSlotFile(int slotIndex, const QString& path);
    void onSlotFileChanged(int slotIndex);
    void setupProfileBar();
    void refreshProfileCombo();
    void updateProfileMarker();
    Profile collectProfile() const;
    void applyProfile(const QString& name, const Profile& profile);
    void saveProfileAs();
    void updateProfile(const QString& name);
    void renameProfile(const QString& oldName);
    void exportProfiles();
    void importProfiles();

    // Help > Check for Updates, and the quiet check at start-up.
    void startSetupCheck();
    void onSetupChecked(const QString& probeRsPath, const SystemCheck::Input& input);
    void setupUpdates();
    void downloadAndInstall(const ReleaseInfo& release);
    void installUpdate(const QString& installerPath, const ReleaseInfo& release);
    void onUpdateAvailable(const ReleaseInfo& release, bool manual);
    void deleteProfile(const QString& name);
    bool slotAddress(int slotIndex, uint32_t& address) const;
    bool buildSlot(int slotIndex, FirmwareSlot& slot, AddressRange& range);
    bool prepareEngine();
    bool prepareFirmwareRun(TargetConfig& cfg, FirmwareSlot& s1, FirmwareSlot& s2);
    QString currentProbeRsPath() const;

    Ui::MainWindow *ui;
    ProbeRsEngine *m_probeRsEngine;
    JLinkEngine *m_jlinkEngine;  // null when SEGGER's J-Link software is not installed
    IProgrammerEngine *m_engine;  // routes to one of the two
    FlashCoordinator *m_coordinator;
    HexMemoryModel *m_memoryModel;
    ConsoleDock* m_console = nullptr;
    OperationStatus* m_operationStatus = nullptr;
    bool m_lastCommandWasErase = false;
    ChipSelector* m_chipSelector = nullptr;
    ProbeSelector* m_probeSelector = nullptr;
    ProbeInspector* m_probeInspector = nullptr;
    QSet<QString> m_inspectedProbes;  // probes whose details were already asked for

    FileWatcher* m_fileWatch[2] = {nullptr, nullptr};
    QLabel* m_lblUpdated[2] = {nullptr, nullptr};
    int m_changeCount[2] = {0, 0};  // how often each file changed on disk
    bool m_programRunning = false;  // the running sequence is Program (not Verify)
    bool m_programmedSlot[2] = {false, false};
    int m_changeCountAtProgram[2] = {0, 0};
    Updater* m_updater = nullptr;
    QThread* m_setupCheckThread = nullptr;
    ProfileCombo* m_comboProfile = nullptr;
    Profile m_profileSnapshot;  // what the shown profile held when it was loaded or saved
    bool m_applyingProfile = false;
    QString m_currentProfile;  // the profile last loaded or saved, empty when none

    // Additional toolbar widgets
    QComboBox *m_comboToolbarHexFile = nullptr;

    // Cached file data for memory viewer
    HexParseResult m_slot1ParseResult;
    HexParseResult m_slot2ParseResult;
    QByteArray m_targetReadData;
    uint32_t m_targetReadBaseAddress = 0x08000000;


    bool m_isOperationRunning = false;
    // What Lock and Unlock can do for the selected chip; true until the helper has answered.
    bool m_canLock = true;
    bool m_canUnlock = true;
    QTimer* m_capabilitiesTimer = nullptr;
    bool m_sequenceRunning = false;  // a flash or verify sequence of the coordinator is active
    bool m_manualConnect = false;  // the user pressed Connect (not a step of a sequence)
    QString m_connectedProbe;      // VID:PID[:serial] of the probe the connection was made with
    QString m_pendingReadFile;       // set while a memory read is running
    uint32_t m_pendingReadBase = 0;
};

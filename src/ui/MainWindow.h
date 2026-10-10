#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QStringListModel>
#include <QComboBox>
#include <QLabel>
#include "core/FlashCoordinator.h"
#include "core/FirmwareTarget.h"
#include "core/HexParser.h"
#include "core/SlotValidator.h"
#include "core/ChipCatalog.h"
#include "engine/ChipDescription.h"
#include "engine/ProbeRsEngine.h"
#include "HexMemoryModel.h"

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
    void changeEvent(QEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    // Toolbar Actions
    void onConnectTriggered();
    void onDisconnectTriggered();
    void onEraseTriggered();
    void onProgramTriggered();
    void onVerifyTriggered();
    void onResetTargetTriggered();
    void onOpenHexTriggered();
    void onRefreshProbesTriggered();
    void onProbeListReady(bool success, const QList<ProbeInfo>& probes, const QString& error);

    // Settings Panel Controls
    void onManufacturerChanged(const QString& manufacturer);
    void onChipListReady(bool success, const QList<ChipEntry>& chips, const QString& error);
    void onAddChipFileTriggered();
    void onClearChipFilesTriggered();
    void onSystemCheckTriggered();
    void onWiringGuideTriggered();
    void onReadTargetTriggered();
    void onSaveLogTriggered();
    void onAboutTriggered();
    void onCancelTriggered();
    void onEngineOperationFinished(bool success, const QString& message);
    void onChipTextCommitted();
    void onOpenSlot1Clicked();
    void onOpenSlot2Clicked();
    void onSlot1PathChanged(const QString& path);
    void onSlot2PathChanged(const QString& path);

    // Data Source Selection
    void onDataSourceChanged();

    // Engine and Coordinator Signals
    void onEngineLogMessage(const QString& message, EngineLogLevel level);
    void onEngineConnected(bool success, const QString& chipInfo);
    void onEngineDisconnected();
    void onSequenceStarted();
    void onSequenceFinished(bool success, const QString& message);
    void onStepChanged(const QString& stepDescription, int stepIndex, int totalSteps);
    void onTotalProgressChanged(int overallPercentage);
    void onAutoSpeedFallbackTriggered(uint32_t newSpeedKhz);

private:
    void setupUiCustomizations();
    void setupToolBarWidgets();
    void setupDeviceDatabase();
    void updateUiState();
    void appendLog(const QString& levelStr, const QString& message, const QString& colorHex = "#dcdcdc");
    void loadFileIntoSlot(int slotIndex, const QString& filePath);
    void updateMemoryViewer();
    void restoreSettings();
    void saveSettings();
    bool slotAddress(int slotIndex, uint32_t& address) const;
    bool buildSlot(int slotIndex, FirmwareSlot& slot, AddressRange& range);
    bool resolveChip(QString& chip);
    bool prepareEngine();
    bool prepareFirmwareRun(TargetConfig& cfg, FirmwareSlot& s1, FirmwareSlot& s2);
    void rebuildChipCatalog();
    void scheduleProbeRefresh();
    QString currentProbeRsPath() const;

    Ui::MainWindow *ui;
    ProbeRsEngine *m_engine;
    FlashCoordinator *m_coordinator;
    HexMemoryModel *m_memoryModel;

    // Additional toolbar widgets
    QComboBox *m_comboToolbarHexFile = nullptr;

    // Cached file data for memory viewer
    HexParseResult m_slot1ParseResult;
    HexParseResult m_slot2ParseResult;
    QByteArray m_targetReadData;
    uint32_t m_targetReadBaseAddress = 0x08000000;

    QList<ChipEntry> m_builtinChips;  // reported by probe-rs
    QList<ChipEntry> m_chips;         // built-in plus custom
    QStringList m_customChipFiles;
    QStringListModel* m_chipNames = nullptr;
    bool m_builtinChipsLoaded = false;
    QString m_pendingManufacturer;  // restored from settings, applied once the chip list arrives
    QString m_pendingChip;

    bool m_isOperationRunning = false;
    bool m_sequenceRunning = false;  // a flash or verify sequence of the coordinator is active
    bool m_manualConnect = false;  // the user pressed Connect (not a step of a sequence)
    bool m_probeRefreshVerbose = false;
    bool m_lastProbeError = false;
    QStringList m_lastProbeKeys;
    bool m_probeListSeen = false;
    QTimer* m_probeTimer = nullptr;
    QStringList m_lastUsbKeys;
    int m_probeSafetyTick = 0;
    QString m_pendingReadFile;       // set while a memory read is running
    uint32_t m_pendingReadBase = 0;
};

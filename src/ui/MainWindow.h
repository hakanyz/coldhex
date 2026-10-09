#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include "core/FlashCoordinator.h"
#include "core/FirmwareTarget.h"
#include "core/HexParser.h"
#include "core/SlotValidator.h"
#include "core/ChipCatalog.h"
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

private slots:
    // Toolbar Actions
    void onConnectTriggered();
    void onDisconnectTriggered();
    void onEraseTriggered();
    void onProgramTriggered();
    void onResetTargetTriggered();
    void onOpenHexTriggered();
    void onRefreshProbesTriggered();

    // Settings Panel Controls
    void onManufacturerChanged(const QString& manufacturer);
    void onChipListReady(bool success, const QList<ChipEntry>& chips, const QString& error);
    void onSpeedSliderChanged(int value);
    void onSpeedSpinChanged(int value);
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

    Ui::MainWindow *ui;
    ProbeRsEngine *m_engine;
    FlashCoordinator *m_coordinator;
    HexMemoryModel *m_memoryModel;

    // Additional toolbar widgets
    QComboBox *m_comboToolbarHexFile = nullptr;
    QSlider *m_sliderToolbarSpeed = nullptr;
    QLabel *m_lblToolbarSpeed = nullptr;

    // Cached file data for memory viewer
    HexParseResult m_slot1ParseResult;
    HexParseResult m_slot2ParseResult;
    QByteArray m_targetReadData;
    uint32_t m_targetReadBaseAddress = 0x08000000;

    QList<ChipEntry> m_chips;
    QString m_pendingManufacturer;  // restored from settings, applied once the chip list arrives
    QString m_pendingChip;

    bool m_isOperationRunning = false;
};

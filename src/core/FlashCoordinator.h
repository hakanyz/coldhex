#pragma once

#include "FirmwareTarget.h"
#include "engine/IProgrammerEngine.h"
#include <QObject>
#include <QVector>

enum class CoordinatorStep {
    Idle,
    Connecting,
    Erasing,
    FlashingSlot1,
    FlashingSlot2,
    Resetting
};

class FlashCoordinator : public QObject {
    Q_OBJECT

public:
    explicit FlashCoordinator(IProgrammerEngine* engine, QObject* parent = nullptr);

    void setTargetConfig(const TargetConfig& config);
    TargetConfig targetConfig() const;

    void setSlot1(const FirmwareSlot& slot);
    FirmwareSlot slot1() const;

    void setSlot2(const FirmwareSlot& slot);
    FirmwareSlot slot2() const;

    void startFlashSequence();
    void cancelSequence();

signals:
    void sequenceStarted();
    void stepChanged(const QString& stepDescription, int stepIndex, int totalSteps);
    void totalProgressChanged(int overallPercentage);
    void sequenceFinished(bool success, const QString& message);
    void autoSpeedFallbackTriggered(uint32_t newSpeedKhz);

private slots:
    void onEngineOperationFinished(bool success, const QString& message);
    void onEngineProgressChanged(int percentage, const QString& statusText);

private:
    void executeNextStep();
    void handleStepFailure(const QString& errorMsg);
    bool stepDownSpeed();

    IProgrammerEngine* m_engine = nullptr;
    TargetConfig m_config;
    FirmwareSlot m_slot1;
    FirmwareSlot m_slot2;

    CoordinatorStep m_currentStep = CoordinatorStep::Idle;
    QVector<CoordinatorStep> m_plannedSteps;
    int m_currentStepIndex = 0;

    const QVector<uint32_t> m_speedFallbackTable = {10000, 4000, 2000, 1000, 500, 100};
};

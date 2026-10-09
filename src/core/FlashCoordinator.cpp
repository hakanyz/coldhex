#include "FlashCoordinator.h"

FlashCoordinator::FlashCoordinator(IProgrammerEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    if (m_engine) {
        connect(m_engine, &IProgrammerEngine::operationFinished,
                this, &FlashCoordinator::onEngineOperationFinished);
        connect(m_engine, &IProgrammerEngine::progressChanged,
                this, &FlashCoordinator::onEngineProgressChanged);
    }
}

void FlashCoordinator::setTargetConfig(const TargetConfig& config) {
    m_config = config;
}

TargetConfig FlashCoordinator::targetConfig() const {
    return m_config;
}

void FlashCoordinator::setSlot1(const FirmwareSlot& slot) {
    m_slot1 = slot;
}

FirmwareSlot FlashCoordinator::slot1() const {
    return m_slot1;
}

void FlashCoordinator::setSlot2(const FirmwareSlot& slot) {
    m_slot2 = slot;
}

FirmwareSlot FlashCoordinator::slot2() const {
    return m_slot2;
}

void FlashCoordinator::startFlashSequence() {
    if (!m_engine) {
        emit sequenceFinished(false, "Programmer engine is not initialized.");
        return;
    }

    if (m_config.chipName.trimmed().isEmpty()) {
        emit sequenceFinished(false, "Please select a target microcontroller chip.");
        return;
    }

    if (!m_slot1.enabled && !m_slot2.enabled) {
        emit sequenceFinished(false, "At least one firmware slot (Bootloader or Application) must be enabled.");
        return;
    }

    m_plannedSteps.clear();
    m_currentStepIndex = 0;

    if (!m_engine->isConnected()) {
        m_plannedSteps.append(CoordinatorStep::Connecting);
    }
    if (m_config.eraseBeforeFlash) {
        m_plannedSteps.append(CoordinatorStep::Erasing);
    }
    if (m_slot1.enabled && !m_slot1.filePath.isEmpty()) {
        m_plannedSteps.append(CoordinatorStep::FlashingSlot1);
    }
    if (m_slot2.enabled && !m_slot2.filePath.isEmpty()) {
        m_plannedSteps.append(CoordinatorStep::FlashingSlot2);
    }
    if (m_config.resetAfterFlash) {
        m_plannedSteps.append(CoordinatorStep::Resetting);
    }

    if (m_plannedSteps.isEmpty()) {
        emit sequenceFinished(false, "No valid flashing steps found to execute.");
        return;
    }

    emit sequenceStarted();
    executeNextStep();
}

void FlashCoordinator::cancelSequence() {
    m_plannedSteps.clear();
    m_currentStep = CoordinatorStep::Idle;
    if (m_engine) {
        m_engine->cancelCurrentOperation();
    }
    emit sequenceFinished(false, "Flashing sequence cancelled.");
}

void FlashCoordinator::executeNextStep() {
    if (m_currentStepIndex >= m_plannedSteps.size()) {
        m_currentStep = CoordinatorStep::Idle;
        emit totalProgressChanged(100);
        emit sequenceFinished(true, "All flashing and verification steps completed successfully!");
        return;
    }

    m_currentStep = m_plannedSteps[m_currentStepIndex];
    int total = m_plannedSteps.size();
    int current = m_currentStepIndex + 1;

    switch (m_currentStep) {
    case CoordinatorStep::Connecting:
        emit stepChanged("Connecting to target...", current, total);
        m_engine->connectTarget(m_config.chipName, m_config.speedKhz, m_config.probeId);
        break;

    case CoordinatorStep::Erasing:
        emit stepChanged("Erasing chip (Chip Erase)...", current, total);
        m_engine->eraseTarget(true);
        break;

    case CoordinatorStep::FlashingSlot1:
        emit stepChanged(QString("Flashing Slot 1: %1").arg(m_slot1.label), current, total);
        m_engine->programFile(m_slot1.filePath, m_slot1.startAddress, m_config.verifyAfterFlash);
        break;

    case CoordinatorStep::FlashingSlot2:
        emit stepChanged(QString("Flashing Slot 2: %1").arg(m_slot2.label), current, total);
        m_engine->programFile(m_slot2.filePath, m_slot2.startAddress, m_config.verifyAfterFlash);
        break;

    case CoordinatorStep::Resetting:
        emit stepChanged("Resetting and running target...", current, total);
        m_engine->resetTarget(false);
        break;

    default:
        break;
    }
}

void FlashCoordinator::onEngineOperationFinished(bool success, const QString& message) {
    if (m_currentStep == CoordinatorStep::Idle) {
        return;
    }

    if (!success) {
        handleStepFailure(message);
        return;
    }

    m_currentStepIndex++;
    executeNextStep();
}

void FlashCoordinator::onEngineProgressChanged(int percentage, const QString& statusText) {
    Q_UNUSED(statusText);
    if (m_plannedSteps.isEmpty()) return;

    int totalSteps = m_plannedSteps.size();
    int stepBase = (m_currentStepIndex * 100) / totalSteps;
    int stepContribution = percentage / totalSteps;
    int overall = qMin(100, stepBase + stepContribution);

    emit totalProgressChanged(overall);
}

void FlashCoordinator::handleStepFailure(const QString& errorMsg) {
    if (m_config.autoSpeedEnabled && stepDownSpeed()) {
        emit autoSpeedFallbackTriggered(m_config.speedKhz);
        // Retry sequence with reduced speed
        startFlashSequence();
        return;
    }

    m_currentStep = CoordinatorStep::Idle;
    emit sequenceFinished(false, QString("Flashing step failed: %1").arg(errorMsg));
}

bool FlashCoordinator::stepDownSpeed() {
    for (int i = 0; i < m_speedFallbackTable.size(); ++i) {
        if (m_speedFallbackTable[i] < m_config.speedKhz) {
            m_config.speedKhz = m_speedFallbackTable[i];
            return true;
        }
    }
    return false;
}

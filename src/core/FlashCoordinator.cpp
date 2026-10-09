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

bool FlashCoordinator::checkRequest() {
    if (!m_engine) {
        emit sequenceFinished(false, "Programmer engine is not initialized.");
        return false;
    }

    if (m_config.chipName.trimmed().isEmpty()) {
        emit sequenceFinished(false, "Please select a target microcontroller chip.");
        return false;
    }

    if (!m_slot1.enabled && !m_slot2.enabled) {
        emit sequenceFinished(false, "At least one firmware slot (Bootloader or Application) must be enabled.");
        return false;
    }
    return true;
}

void FlashCoordinator::startVerifySequence() {
    if (!checkRequest()) return;

    m_verifyOnly = true;
    m_plannedSteps.clear();
    m_currentStepIndex = 0;

    if (!m_engine->isConnected()) {
        m_plannedSteps.append(CoordinatorStep::Connecting);
    }
    if (m_slot1.enabled && !m_slot1.filePath.isEmpty()) {
        m_plannedSteps.append(CoordinatorStep::VerifyingSlot1);
    }
    if (m_slot2.enabled && !m_slot2.filePath.isEmpty()) {
        m_plannedSteps.append(CoordinatorStep::VerifyingSlot2);
    }

    if (m_plannedSteps.isEmpty() || m_plannedSteps.last() == CoordinatorStep::Connecting) {
        m_plannedSteps.clear();
        emit sequenceFinished(false, "No firmware file to verify.");
        return;
    }

    emit sequenceStarted();
    executeNextStep();
}

void FlashCoordinator::startFlashSequence() {
    if (!checkRequest()) return;
    m_verifyOnly = false;

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
        emit sequenceFinished(true, m_verifyOnly ? "Target memory matches the selected files." : "All flashing and verification steps completed successfully!");
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

    case CoordinatorStep::VerifyingSlot1:
        emit stepChanged(QString("Verifying Slot 1: %1").arg(m_slot1.label), current, total);
        m_engine->verifyFile(m_slot1.filePath, m_slot1.startAddress);
        break;

    case CoordinatorStep::VerifyingSlot2:
        emit stepChanged(QString("Verifying Slot 2: %1").arg(m_slot2.label), current, total);
        m_engine->verifyFile(m_slot2.filePath, m_slot2.startAddress);
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
    // A mismatch found by a plain verify is a result, not a link problem, so do not retry it slower.
    const bool verifyMismatch = m_verifyOnly
        && (m_currentStep == CoordinatorStep::VerifyingSlot1 || m_currentStep == CoordinatorStep::VerifyingSlot2);
    if (verifyMismatch) {
        m_currentStep = CoordinatorStep::Idle;
        emit sequenceFinished(false, QString("Verification failed: %1").arg(errorMsg));
        return;
    }

    if (m_config.autoSpeedEnabled && stepDownSpeed()) {
        emit autoSpeedFallbackTriggered(m_config.speedKhz);
        // Resume from the failed step instead of restarting, so steps that already
        // succeeded (e.g. erase, Slot 1) are not repeated. The engine takes the speed
        // from its connect call, so a lower speed requires reconnecting first.
        if (m_currentStep != CoordinatorStep::Connecting) {
            m_plannedSteps.insert(m_currentStepIndex, CoordinatorStep::Connecting);
        }
        executeNextStep();
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

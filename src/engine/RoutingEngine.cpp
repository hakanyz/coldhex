#include "RoutingEngine.h"
#include "JLinkCommands.h"

RoutingEngine::RoutingEngine(IProgrammerEngine* probeRs, IProgrammerEngine* jlink, QObject* parent)
    : IProgrammerEngine(parent), m_probeRs(probeRs), m_jlink(jlink), m_active(probeRs)
{
    // Pass the signals of both engines on as our own. Only the active one is ever busy.
    for (IProgrammerEngine* engine : {m_probeRs, m_jlink}) {
        if (!engine) continue;
        connect(engine, &IProgrammerEngine::connected, this, &IProgrammerEngine::connected);
        connect(engine, &IProgrammerEngine::disconnected, this, &IProgrammerEngine::disconnected);
        connect(engine, &IProgrammerEngine::operationStarted, this, &IProgrammerEngine::operationStarted);
        connect(engine, &IProgrammerEngine::progressChanged, this, &IProgrammerEngine::progressChanged);
        connect(engine, &IProgrammerEngine::logMessage, this, &IProgrammerEngine::logMessage);
        connect(engine, &IProgrammerEngine::operationFinished, this, &IProgrammerEngine::operationFinished);
    }
    // Lists are asked from probe-rs only.
    connect(m_probeRs, &IProgrammerEngine::chipListReady, this, &IProgrammerEngine::chipListReady);
    connect(m_probeRs, &IProgrammerEngine::probeListReady, this, &IProgrammerEngine::probeListReady);
}

void RoutingEngine::select(const QString& probeSelector) {
    IProgrammerEngine* wanted = (m_jlink && JLinkCommands::isJLinkProbe(probeSelector)) ? m_jlink : m_probeRs;
    if (wanted == m_active) return;

    if (m_active->isConnected() && !m_active->isBusy()) m_active->disconnectTarget();
    m_active = wanted;
}

void RoutingEngine::configureTarget(const TargetSettings& settings) {
    select(settings.probeId);
    m_active->configureTarget(settings);
}

ChipMemoryMap RoutingEngine::queryMemoryMap(const QString& chipName, const QString& chipDescriptionPath) {
    return m_probeRs->queryMemoryMap(chipName, chipDescriptionPath);
}

bool RoutingEngine::connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId) {
    select(probeId);
    return m_active->connectTarget(chipName, speedKhz, probeId);
}

void RoutingEngine::disconnectTarget() { m_active->disconnectTarget(); }
bool RoutingEngine::isConnected() const { return m_active->isConnected(); }
bool RoutingEngine::isBusy() const { return m_active->isBusy(); }

void RoutingEngine::eraseTarget(bool fullErase) { m_active->eraseTarget(fullErase); }

void RoutingEngine::programFile(const QString& filePath, uint32_t baseAddress, bool verify) {
    m_active->programFile(filePath, baseAddress, verify);
}

void RoutingEngine::verifyFile(const QString& filePath, uint32_t baseAddress) {
    m_active->verifyFile(filePath, baseAddress);
}

void RoutingEngine::readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) {
    m_active->readMemory(address, byteCount, outputFile);
}

void RoutingEngine::resetTarget(bool halt) { m_active->resetTarget(halt); }
void RoutingEngine::cancelCurrentOperation() { m_active->cancelCurrentOperation(); }

void RoutingEngine::requestChipList() { m_probeRs->requestChipList(); }
void RoutingEngine::requestProbeList() { m_probeRs->requestProbeList(); }

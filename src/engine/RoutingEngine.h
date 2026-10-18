#pragma once

#include "IProgrammerEngine.h"

// The engine the window talks to. It owns no hardware code itself: it passes every call on to
// the probe-rs engine, or to the J-Link engine when the selected probe is a J-Link and SEGGER's
// J-Link software is installed (the J-Link engine is then null otherwise). The signals of both
// engines are passed on, so the rest of the program sees one engine.
//
// The probe list and the chip list always come from probe-rs.
class RoutingEngine : public IProgrammerEngine {
    Q_OBJECT

public:
    // Neither engine is owned; `jlink` may be null.
    RoutingEngine(IProgrammerEngine* probeRs, IProgrammerEngine* jlink, QObject* parent = nullptr);

    void configureTarget(const TargetSettings& settings) override;
    ChipMemoryMap queryMemoryMap(const QString& chipName, const QString& chipDescriptionPath) override;
    bool connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId = QString()) override;
    void disconnectTarget() override;
    bool isConnected() const override;
    bool isBusy() const override;

    void eraseTarget(bool fullErase = true) override;
    void unlockTarget() override;
    void lockTarget() override;
    void requestCapabilities(const QString& chipName) override;
    void programFile(const QString& filePath, uint32_t baseAddress, bool verify = true) override;
    void verifyFile(const QString& filePath, uint32_t baseAddress) override;
    void readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) override;
    void resetTarget(bool halt = false) override;
    void cancelCurrentOperation() override;

    void requestChipList() override;
    void requestProbeList() override;

    // True when calls go to the J-Link engine at the moment.
    bool usesJLink() const { return m_active == m_jlink && m_jlink != nullptr; }

private:
    // Picks the engine for a probe selector. A running connection is closed first when the
    // engine changes, since the old engine cannot serve the new probe.
    void select(const QString& probeSelector);

    IProgrammerEngine* m_probeRs;
    IProgrammerEngine* m_jlink;
    IProgrammerEngine* m_active;
};

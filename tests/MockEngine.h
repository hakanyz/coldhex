#pragma once

#include "engine/IProgrammerEngine.h"
#include <QStringList>

// In-memory engine for tests. Records every command issued by the coordinator;
// the test decides when and how each command finishes by calling finish().
class MockEngine : public IProgrammerEngine {
    Q_OBJECT

public:
    explicit MockEngine(QObject* parent = nullptr) : IProgrammerEngine(parent) {}

    void configureTarget(const TargetSettings& s) override {
        configured << QString("%1@%2:%3:%4:%5:%6").arg(s.chipName).arg(s.speedKhz).arg(s.probeId, s.chipDescriptionPath)
                          .arg(s.protocol == DebugProtocol::Jtag ? "jtag" : "swd").arg(s.connectUnderReset ? "cur" : "-");
    }
    void verifyFile(const QString& filePath, uint32_t baseAddress) override {
        commands << QString("verify:%1@0x%2").arg(filePath).arg(baseAddress, 0, 16);
        m_busy = true;
    }

    bool connectTarget(const QString&, uint32_t speedKhz, const QString&) override {
        commands << QString("connect@%1").arg(speedKhz);
        m_pendingConnect = true;
        m_busy = true;
        return true;
    }
    void disconnectTarget() override { m_connected = false; }
    bool isConnected() const override { return m_connected; }
    bool isBusy() const override { return m_busy; }

    void eraseTarget(bool) override { commands << "erase"; m_busy = true; }
    void unlockTarget() override { commands << "unlock"; m_busy = true; }
    void lockTarget() override { commands << "lock"; m_busy = true; }
    void requestCapabilities(const QString&) override {}
    void programFile(const QString& filePath, uint32_t baseAddress, bool verify) override {
        commands << QString("program:%1@0x%2%3").arg(filePath).arg(baseAddress, 0, 16).arg(verify ? ":verify" : "");
        m_busy = true;
    }
    void readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) override {
        commands << QString("read:0x%1+%2:%3").arg(address, 0, 16).arg(byteCount).arg(outputFile);
        m_busy = true;
    }
    void resetTarget(bool) override { commands << "reset"; m_busy = true; }
    void cancelCurrentOperation() override { commands << "cancel"; m_busy = false; }

    ChipMemoryMap queryMemoryMap(const QString&, const QString&) override { return memory; }
    void requestChipList() override { emit chipListReady(true, chips, QString()); }
    void requestProbeList() override { emit probeListReady(true, {}, QString()); }

    // Completes the command that is currently running.
    void finish(bool success) {
        if (m_pendingConnect) {
            m_connected = success;
            m_pendingConnect = false;
        }
        m_busy = false;
        emit operationFinished(success, success ? "ok" : "mock failure");
    }

    QStringList commands;
    QStringList configured;
    QList<ChipEntry> chips;
    ChipMemoryMap memory;

private:
    bool m_connected = false;
    bool m_busy = false;
    bool m_pendingConnect = false;
};

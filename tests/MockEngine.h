#pragma once

#include "engine/IProgrammerEngine.h"
#include <QStringList>

// In-memory engine for tests. Records every command issued by the coordinator;
// the test decides when and how each command finishes by calling finish().
class MockEngine : public IProgrammerEngine {
    Q_OBJECT

public:
    explicit MockEngine(QObject* parent = nullptr) : IProgrammerEngine(parent) {}

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
    void programFile(const QString& filePath, uint32_t baseAddress, bool verify) override {
        commands << QString("program:%1@0x%2%3").arg(filePath).arg(baseAddress, 0, 16).arg(verify ? ":verify" : "");
        m_busy = true;
    }
    void resetTarget(bool) override { commands << "reset"; m_busy = true; }
    void cancelCurrentOperation() override { commands << "cancel"; m_busy = false; }

    QStringList listSupportedChips(const QString&) override { return {}; }
    QList<ProbeInfo> listAvailableProbes() override { return {}; }

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

private:
    bool m_connected = false;
    bool m_busy = false;
    bool m_pendingConnect = false;
};

#include "engine/ConnectionAdvice.h"
#include <QtTest>

class TestConnectionAdvice : public QObject {
    Q_OBJECT

private slots:
    void recognisesTargetNotAnswering() {
        const QString out =
            "WARN probe_rs::probe::stlink: send_jtag_command 242 failed: JtagGetIdcodeError\n"
            "Error: Connecting to the chip was unsuccessful.\n"
            "Caused by:\n    2: Command failed with status JtagGetIdcodeError.\n";
        const QString a = ConnectionAdvice::explain(out);
        QVERIFY(a.startsWith("The probe works, but the target does not answer."));
        QVERIFY(a.contains("GND"));
        QVERIFY(a.contains("SWDIO"));
        QVERIFY(a.contains("Connect under reset"));
    }

    void recognisesUnknownChip() {
        const QString out = "Error: The chip 'FOO' was not found in the database.\n\n"
                            "Caused by:\n    The requested chip 'FOO' was not found in the list of known targets.\n";
        QVERIFY(ConnectionAdvice::explain(out).startsWith("The chip name is not known"));
    }

    void recognisesAWrongUsbDriverAndDoesNotBlameTheCables() {
        // Real output for a J-Link that still has SEGGER's driver.
        const QString out =
            "Error: Failed to open probe: Failed to open the debug probe.\n\nCaused by:\n"
            "    0: USB Communication Error\n"
            "    1: error while taking control over USB device: incompatible driver is installed for this "
            "device(this error may be caused by not having the WinUSB driver installed; use Zadig "
            "(https://zadig.akeo.ie/) to install it for the J-Link device; this will replace the SEGGER "
            "J-Link driver)\n";
        const QString a = ConnectionAdvice::explain(out);
        QVERIFY(a.startsWith("The probe is found, but it has the wrong USB driver."));
        QVERIFY(a.contains("Zadig"));
        QVERIFY(a.contains("WinUSB"));
        QVERIFY(!a.contains("SWDIO"));  // no wiring hints for a driver problem
    }

    void recognisesMissingProbe() {
        const QString a = ConnectionAdvice::explain("Error: No connected probes were found.\n");
        QVERIFY(a.startsWith("The selected probe was not found."));
        QVERIFY(a.contains("System Check"));
    }

    void recognisesOtherCauses() {
        QVERIFY(ConnectionAdvice::explain("Error: chip is locked by read protection").contains("read-protected"));
        QVERIFY(ConnectionAdvice::explain("USB error: Access is denied").startsWith("The probe cannot be opened."));
    }

    void matchesCaseInsensitively() {
        QVERIFY(!ConnectionAdvice::explain("no connected probes were FOUND").isEmpty());
    }

    void unknownOutputGivesNothingAndGenericIsAvailable() {
        QVERIFY(ConnectionAdvice::explain("").isEmpty());
        QVERIFY(ConnectionAdvice::explain("some unrelated line").isEmpty());
        QVERIFY(ConnectionAdvice::generic().contains("Could not connect"));
    }
};

QTEST_GUILESS_MAIN(TestConnectionAdvice)
#include "tst_connectionadvice.moc"

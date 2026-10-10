#include "ConnectionAdvice.h"
#include <QStringList>

namespace {

bool has(const QString& text, const char* needle) {
    return text.contains(QLatin1String(needle), Qt::CaseInsensitive);
}

QString advice(const QString& title, const QStringList& hints) {
    QString out = title;
    for (const QString& h : hints) out += "\n- " + h;
    return out;
}

}  // namespace

QString ConnectionAdvice::explain(const QString& out) {
    // Wording taken from real probe-rs output.
    if (has(out, "was not found in the database") || has(out, "not found in the list of known targets")) {
        return advice("The chip name is not known to probe-rs.",
                      {"Pick the chip from the Model list (or type part of its name and choose a match).",
                       "Chips probe-rs lacks need a chip description file (Tools > Add Chip Description File)."});
    }

    if (has(out, "No connected probes were found")) {
        return advice("The selected probe was not found.",
                      {"Check that the probe is plugged in and shown in the Probe box.",
                       "Windows: the USB driver may be missing (Help > System Check).",
                       "Close other programs that use the probe (ST-LINK Utility, STM32CubeProgrammer, IDEs)."});
    }

    if (has(out, "JtagGetIdcodeError") || has(out, "Connecting to the chip was unsuccessful")) {
        return advice("The probe works, but the target does not answer.",
                      {"GND of the probe and the board must be connected.",
                       "SWDIO and SWCLK must go to the matching pins (they are easy to swap).",
                       "The board needs power; 3V3 of the probe is only a voltage reference for the target.",
                       "Try \"Connect under reset\" and connect NRST if the program on the chip disables the debug pins.",
                       "Lower the speed, and check the chip choice and the selected interface (SWD or JTAG)."});
    }

    if (has(out, "read protection") || has(out, "readout protection") || has(out, "locked")) {
        return advice("The chip may be read-protected.",
                      {"Debug access is blocked by the protection setting of the chip.",
                       "Removing the protection erases the whole flash; use Erase (chip erase) if that is acceptable."});
    }

    if (has(out, "access is denied") || has(out, "permission denied") || has(out, "libusb")) {
        return advice("The probe cannot be opened.",
                      {"Another program may be using it; close ST-LINK Utility, STM32CubeProgrammer and IDEs.",
                       "Windows: install the probe's USB driver. Linux: install the udev rules and re-plug the probe."});
    }

    return {};
}

QString ConnectionAdvice::generic() {
    return advice("Could not connect to the target.",
                  {"Check the cables (GND, SWDIO, SWCLK) and the power of the board.",
                   "Try \"Connect under reset\" or a lower speed.",
                   "See the log for the message of probe-rs."});
}

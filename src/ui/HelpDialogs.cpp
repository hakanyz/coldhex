#include "HelpDialogs.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace HelpDialogs {

void showAbout(QWidget* parent) {
    QMessageBox::about(parent, "About ColdHex",
        QString("<h3>ColdHex %1</h3>"
                "<p>Flash and manage ARM Cortex-M microcontrollers through ST-Link, J-Link and CMSIS-DAP probes. "
                "Flashing is done by <a href='https://probe.rs'>probe-rs</a>.</p>"
                "<p>Licensed under the GNU LGPL v3. Built with Qt %2.</p>")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

void showWiringGuide(QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Wiring Guide");
    dialog->resize(560, 520);

    auto* view = new QTextBrowser(dialog);
    view->setOpenExternalLinks(true);
    view->setHtml(
        "<h3>Connecting the probe to the target board</h3>"
        "<p>Pin positions differ between probes and boards. Follow the pin labels of your probe and "
        "your board; this guide only explains what each signal is for.</p>"
        "<h4>SWD (2 wires + ground)</h4>"
        "<table cellpadding='4' border='0'>"
        "<tr><td><b>GND</b></td><td>Common ground. <b>Always connect it</b>, an unconnected ground is the most common cause of failures.</td></tr>"
        "<tr><td><b>SWDIO</b></td><td>Bidirectional data line. Goes to the SWDIO pin of the chip.</td></tr>"
        "<tr><td><b>SWCLK</b></td><td>Clock. Goes to the SWCLK pin of the chip. SWDIO and SWCLK are easy to swap.</td></tr>"
        "<tr><td><b>3V3 / VTref</b></td><td>Tells the probe the voltage of the target. Many probes only measure it; the board needs its own power.</td></tr>"
        "<tr><td><b>NRST</b></td><td>Optional reset line. Needed for <i>Connect under reset</i> and helps with chips that disable their debug pins.</td></tr>"
        "</table>"
        "<h4>JTAG</h4>"
        "<p>TMS, TCK, TDI and TDO instead of SWDIO and SWCLK, plus GND, VTref and optionally NRST/TRST.</p>"
        "<h4>If Connect fails</h4>"
        "<ul>"
        "<li>Check GND first, then swap SWDIO and SWCLK if unsure.</li>"
        "<li>Make sure the board is powered and uses the same voltage level as the probe (3.3 V).</li>"
        "<li>Keep the wires short; with long wires lower the speed (e.g. 500 kHz).</li>"
        "<li>Try <i>Connect under reset</i> and connect NRST.</li>"
        "<li>Check that the right chip and interface (SWD/JTAG) are selected.</li>"
        "</ul>"
        "<p>With a probe that works but a board that does not answer, ColdHex reports that the target "
        "does not respond. That points to wiring, power or chip settings, not to the probe.</p>");

    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(view);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->open();
}

}  // namespace HelpDialogs

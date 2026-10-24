#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QIcon>
#include <QNetworkProxyFactory>
#include <QTimer>

namespace {
// Windows shows a new window white until the program has painted it. On Windows the window is
// therefore opened invisible, painted once and only then made visible. Other systems do not show
// such a white window, so they show it at once.
void showAfterFirstPaint(QWidget& window) {
#ifdef Q_OS_WIN
    window.setWindowOpacity(0.0);
    // Whatever happens, the window is visible after one second.
    QTimer::singleShot(1000, &window, [&window]() { window.setWindowOpacity(1.0); });
    window.show();
    window.repaint();
    QApplication::processEvents();
    window.setWindowOpacity(1.0);
#else
    window.show();
#endif
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // The update check should work behind the proxy of a company network.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    app.setApplicationName("ColdHex");
    app.setApplicationVersion(COLDHEX_VERSION);
    app.setOrganizationName("ColdHex Project");
    app.setWindowIcon(QIcon(":/icons/coldhex.png"));
    Theme::applyDark(app);

    MainWindow window;
    window.resize(1180, 760);
    showAfterFirstPaint(window);

    return app.exec();
}

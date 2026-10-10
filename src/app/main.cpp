#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("ColdHex");
    app.setApplicationVersion(COLDHEX_VERSION);
    app.setOrganizationName("ColdHex Project");
    app.setWindowIcon(QIcon(":/icons/coldhex.png"));
    Theme::applyDark(app);

    MainWindow window;
    window.resize(1180, 760);
    window.show();

    return app.exec();
}

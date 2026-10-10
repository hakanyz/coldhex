#include "ui/Theme.h"
#include <QColor>
#include <QFile>
#include <QRegularExpression>
#include <QtTest>

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void everyColorIsAValidHexValue() {
        const Theme::Color first = Theme::Color::window;
        const Theme::Color last = Theme::Color::logSuccess;
        for (int i = static_cast<int>(first); i <= static_cast<int>(last); ++i) {
            const QString hex = Theme::hex(static_cast<Theme::Color>(i));
            QVERIFY2(QColor(hex).isValid(), qPrintable(hex));  // isValidColorName needs Qt 6.4
            QVERIFY(hex.startsWith('#') && hex.size() == 7);
        }
    }

    void replacesColorNamesInTheTemplate() {
        const QString css = Theme::renderStyleSheet("QWidget { background: @window@; color: @text@; }");
        QVERIFY(!css.contains('@'));
        QVERIFY(css.contains(Theme::hex(Theme::Color::window)));
        QVERIFY(css.contains(Theme::hex(Theme::Color::text)));
    }

    void leavesUnknownNamesAlone() {
        QCOMPARE(Theme::renderStyleSheet("a @nonsense@ b"), QString("a @nonsense@ b"));
    }

    void logColorsFollowTheLevel() {
        QCOMPARE(Theme::logColor("ERROR"), Theme::hex(Theme::Color::logError));
        QCOMPARE(Theme::logColor("warn"), Theme::hex(Theme::Color::logWarning));
        QCOMPARE(Theme::logColor("WARNING"), Theme::hex(Theme::Color::logWarning));
        QCOMPARE(Theme::logColor("SUCCESS"), Theme::hex(Theme::Color::logSuccess));
        QCOMPARE(Theme::logColor("INFO"), Theme::hex(Theme::Color::logInfo));
        QCOMPARE(Theme::logColor("whatever"), Theme::hex(Theme::Color::text));
    }

    // Catches a typo such as @acent@ in the style sheet template.
    void styleSheetTemplateUsesOnlyKnownColors() {
        QFile file(QString(QT_TESTCASE_SOURCEDIR) + "/resources/theme/dark.qss.in");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString css = Theme::renderStyleSheet(QString::fromUtf8(file.readAll()));
        QVERIFY(!css.isEmpty());
        QVERIFY2(!css.contains('@'), "unresolved @name@ in dark.qss.in");
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"

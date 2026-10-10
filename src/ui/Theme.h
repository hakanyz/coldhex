#pragma once

#include <QString>

class QApplication;

// The look of ColdHex. All colors of the program are listed once, in COLDHEX_COLORS below;
// the Qt palette, the style sheet template (resources/theme/dark.qss.in, where @name@ stands
// for a color) and the colors of console lines and status messages are all taken from it.
//
// To change a color, change its line in the list.
#define COLDHEX_COLORS(X)                                                          \
    /* surfaces */                                                                 \
    X(window,        "#202124")  /* main window                    */              \
    X(toolbar,       "#27282c")  /* tool bar                      */              \
    X(surface,       "#2b2d30")  /* inputs, headers, panel titles */              \
    X(surfaceAlt,    "#222326")  /* every other table row         */              \
    X(tableBase,     "#1a1a1c")  /* table background              */              \
    X(consoleBase,   "#18191b")  /* console background            */              \
    X(paletteButton, "#2d2f33")  /* default button color          */              \
    X(grid,          "#2d2f33")  /* table grid lines              */              \
    /* lines and buttons */                                                        \
    X(border,        "#3c3f41")  /* frames                        */              \
    X(borderInput,   "#45494a")  /* input fields                  */              \
    X(buttonFace,    "#3b3e43")                                                    \
    X(buttonBorder,  "#4f5358")                                                    \
    X(buttonHover,   "#484c52")                                                    \
    X(hover,         "#3a3d42")  /* tool button under the mouse   */              \
    X(hoverBorder,   "#4a4e55")                                                    \
    X(pressed,       "#2a2c30")  /* tool button being pressed     */              \
    /* text */                                                                     \
    X(text,          "#dcdcdc")                                                    \
    X(textPrimary,   "#e1e1e1")                                                    \
    X(textBright,    "#ffffff")                                                    \
    X(textToolButton,"#e0e0e0")                                                    \
    X(textHeader,    "#bbbbbb")                                                    \
    X(textMuted,     "#888888")  /* time stamps in the console   */              \
    X(textDisabled,  "#6e6e6e")                                                    \
    /* accents */                                                                  \
    X(accent,        "#007acc")  /* selection, pressed buttons    */              \
    X(link,          "#2980b9")                                                    \
    X(progressStart, "#1976d2")                                                    \
    X(progressEnd,   "#42a5f5")                                                    \
    /* messages (console lines, System Check) */                                   \
    X(logDebug,      "#95a5a6")                                                    \
    X(logInfo,       "#5dade2")                                                    \
    X(logWarning,    "#f39c12")                                                    \
    X(logError,      "#e74c3c")                                                    \
    X(logSuccess,    "#2ecc71")

class Theme {
public:
    enum class Color {
#define COLDHEX_ENUM(name, hex) name,
        COLDHEX_COLORS(COLDHEX_ENUM)
#undef COLDHEX_ENUM
    };

    // "#rrggbb" of a color.
    static QString hex(Color color);

    // Color of a console line for a level name: "DEBUG", "INFO", "WARNING"/"WARN", "ERROR",
    // "SUCCESS" (anything else gets the normal text color).
    static QString logColor(const QString& level);

    // The style sheet text with every @name@ replaced by its color. Exposed for tests.
    static QString renderStyleSheet(const QString& templateText);

    static void applyDark(QApplication& app);
};

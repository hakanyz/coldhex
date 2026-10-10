#pragma once

class QApplication;

// The look of the application: Fusion style, a dark palette and the style sheet in
// resources/theme/dark.qss (edit that file to change colors and widget styles).
class Theme {
public:
    static void applyDark(QApplication& app);
};

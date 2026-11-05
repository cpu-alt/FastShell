// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/Style.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QStyleFactory>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace style {

namespace {

QColor hex(const char *value) {
    return QColor(QLatin1String(value));
}

// One theme from its colours: 9 interface colours, then the terminal
// (foreground, background, cursor, selection) and its 16 ANSI colours.
ColorTheme make(const char *id, const QString &name, bool dark, std::initializer_list<const char *> ui,
                std::initializer_list<const char *> term, std::initializer_list<const char *> ansi) {
    ColorTheme t;
    t.id = QLatin1String(id);
    t.name = name;
    t.dark = dark;
    const QList<const char *> u(ui);
    t.window = hex(u[0]);
    t.panel = hex(u[1]);
    t.surface = hex(u[2]);
    t.border = hex(u[3]);
    t.hover = hex(u[4]);
    t.text = hex(u[5]);
    t.muted = hex(u[6]);
    t.accent = hex(u[7]);
    t.strong = hex(u[8]);
    const QList<const char *> c(term);
    t.terminal.foreground = hex(c[0]);
    t.terminal.background = hex(c[1]);
    t.terminal.cursor = hex(c[2]);
    t.terminal.selection = hex(c[3]);
    std::size_t i = 0;
    for (const char *a : ansi) t.terminal.ansi[i++] = hex(a);
    return t;
}

const char *const GitHubLightAnsi[] = {"#24292F", "#CF222E", "#116329", "#4D2D00", "#0969DA", "#8250DF", "#1B7C83", "#6E7781",
                                       "#57606A", "#A40E26", "#1A7F37", "#633C01", "#218BFF", "#A475F9", "#3192AA", "#8C959F"};

QList<ColorTheme> buildThemes() {
    QList<ColorTheme> list;
    // FastShell's own themes first: the original dark one is the default.
    list << make("dark", QObject::tr("FastShell Dark"), true,
                 {"#13151A", "#181B21", "#1F232A", "#2C313A", "#272C35", "#D8DCE3", "#8A919E", "#3D9BF5", "#FFFFFF"},
                 {"#D4D7DD", "#1B1D23", "#5FB3FF", "#2F4F7A"},
                 {"#1B1D23", "#E06C75", "#98C379", "#E5C07B", "#61AFEF", "#C678DD", "#56B6C2", "#ABB2BF",
                  "#5C6370", "#FF7B86", "#B5E890", "#FFD68A", "#7DC4FF", "#E09AF5", "#6FD6E0", "#FFFFFF"});
    list << make("violet", QObject::tr("FastShell Violet"), true,
                 {"#0F0C15", "#15111D", "#1E1828", "#2E2540", "#2A2238", "#E6E1F0", "#9389A8", "#A855F7", "#FFFFFF"},
                 {"#E9E4F2", "#120E19", "#C084FC", "#4C2E7A"},
                 {"#120E19", "#F7768E", "#9ECE6A", "#E0AF68", "#7AA2F7", "#C084FC", "#7DCFFF", "#C9C2D8",
                  "#5B5170", "#FF8FA3", "#B9F27C", "#FFD089", "#9AB8FF", "#D8B4FE", "#A5E3FF", "#FFFFFF"});
    list << make("light", QObject::tr("FastShell Light"), false,
                 {"#ECEEF3", "#F6F7FA", "#FFFFFF", "#D5D9E0", "#E2E5EC", "#1F2328", "#6B7280", "#7C3AED", "#111318"},
                 {"#24292F", "#FFFFFF", "#7C3AED", "#DCD0FA"},
                 {GitHubLightAnsi[0], GitHubLightAnsi[1], GitHubLightAnsi[2], GitHubLightAnsi[3], GitHubLightAnsi[4],
                  GitHubLightAnsi[5], GitHubLightAnsi[6], GitHubLightAnsi[7], GitHubLightAnsi[8], GitHubLightAnsi[9],
                  GitHubLightAnsi[10], GitHubLightAnsi[11], GitHubLightAnsi[12], GitHubLightAnsi[13], GitHubLightAnsi[14],
                  GitHubLightAnsi[15]});
    // Inspired by the most popular VS Code themes.
    list << make("dracula", QStringLiteral("Dracula"), true,
                 {"#1E1F29", "#21222C", "#282A36", "#3A3C4E", "#343746", "#F8F8F2", "#8B93B8", "#BD93F9", "#FFFFFF"},
                 {"#F8F8F2", "#282A36", "#F8F8F2", "#44475A"},
                 {"#21222C", "#FF5555", "#50FA7B", "#F1FA8C", "#BD93F9", "#FF79C6", "#8BE9FD", "#F8F8F2",
                  "#6272A4", "#FF6E6E", "#69FF94", "#FFFFA5", "#D6ACFF", "#FF92DF", "#A4FFFF", "#FFFFFF"});
    list << make("onedark", QStringLiteral("One Dark Pro"), true,
                 {"#1E2227", "#21252B", "#282C34", "#3B4048", "#2C313A", "#D7DAE0", "#7F848E", "#61AFEF", "#FFFFFF"},
                 {"#ABB2BF", "#282C34", "#528BFF", "#3E4451"},
                 {"#3F4451", "#E06C75", "#98C379", "#D19A66", "#61AFEF", "#C678DD", "#56B6C2", "#D7DAE0",
                  "#4F5666", "#BE5046", "#A5E075", "#E5C07B", "#4DC4FF", "#DE73FF", "#4CD1E0", "#E6E6E6"});
    list << make("monokai", QStringLiteral("Monokai"), true,
                 {"#1E1F1C", "#222320", "#272822", "#3B3C35", "#33342D", "#F8F8F2", "#90908A", "#F92672", "#FFFFFF"},
                 {"#F8F8F2", "#272822", "#F8F8F0", "#49483E"},
                 {"#272822", "#F92672", "#A6E22E", "#F4BF75", "#66D9EF", "#AE81FF", "#A1EFE4", "#F8F8F2",
                  "#75715E", "#F92672", "#A6E22E", "#F4BF75", "#66D9EF", "#AE81FF", "#A1EFE4", "#F9F8F5"});
    list << make("nord", QStringLiteral("Nord"), true,
                 {"#2B303B", "#2E3440", "#353C4A", "#434C5E", "#3E4656", "#ECEFF4", "#9AA3B5", "#88C0D0", "#FFFFFF"},
                 {"#D8DEE9", "#2E3440", "#D8DEE9", "#434C5E"},
                 {"#3B4252", "#BF616A", "#A3BE8C", "#EBCB8B", "#81A1C1", "#B48EAD", "#88C0D0", "#E5E9F0",
                  "#4C566A", "#BF616A", "#A3BE8C", "#EBCB8B", "#81A1C1", "#B48EAD", "#8FBCBB", "#ECEFF4"});
    list << make("tokyonight", QStringLiteral("Tokyo Night"), true,
                 {"#16161E", "#1A1B26", "#1F2335", "#292E42", "#24283B", "#C0CAF5", "#737AA2", "#7AA2F7", "#FFFFFF"},
                 {"#C0CAF5", "#1A1B26", "#C0CAF5", "#33467C"},
                 {"#15161E", "#F7768E", "#9ECE6A", "#E0AF68", "#7AA2F7", "#BB9AF7", "#7DCFFF", "#A9B1D6",
                  "#414868", "#F7768E", "#9ECE6A", "#E0AF68", "#7AA2F7", "#BB9AF7", "#7DCFFF", "#C0CAF5"});
    list << make("catppuccin", QStringLiteral("Catppuccin Mocha"), true,
                 {"#11111B", "#181825", "#1E1E2E", "#313244", "#2A2B3C", "#CDD6F4", "#9399B2", "#CBA6F7", "#FFFFFF"},
                 {"#CDD6F4", "#1E1E2E", "#F5E0DC", "#45475A"},
                 {"#45475A", "#F38BA8", "#A6E3A1", "#F9E2AF", "#89B4FA", "#F5C2E7", "#94E2D5", "#BAC2DE",
                  "#585B70", "#F38BA8", "#A6E3A1", "#F9E2AF", "#89B4FA", "#F5C2E7", "#94E2D5", "#A6ADC8"});
    list << make("githubdark", QStringLiteral("GitHub Dark"), true,
                 {"#010409", "#0D1117", "#161B22", "#30363D", "#21262D", "#E6EDF3", "#8D96A0", "#2F81F7", "#FFFFFF"},
                 {"#E6EDF3", "#0D1117", "#2F81F7", "#264F78"},
                 {"#484F58", "#FF7B72", "#3FB950", "#D29922", "#58A6FF", "#BC8CFF", "#39C5CF", "#B1BAC4",
                  "#6E7681", "#FFA198", "#56D364", "#E3B341", "#79C0FF", "#D2A8FF", "#56D4DD", "#FFFFFF"});
    list << make("githublight", QStringLiteral("GitHub Light"), false,
                 {"#F6F8FA", "#FFFFFF", "#FFFFFF", "#D0D7DE", "#EAEEF2", "#1F2328", "#656D76", "#0969DA", "#0D1117"},
                 {"#1F2328", "#FFFFFF", "#0969DA", "#B6E3FF"},
                 {GitHubLightAnsi[0], GitHubLightAnsi[1], GitHubLightAnsi[2], GitHubLightAnsi[3], GitHubLightAnsi[4],
                  GitHubLightAnsi[5], GitHubLightAnsi[6], GitHubLightAnsi[7], GitHubLightAnsi[8], GitHubLightAnsi[9],
                  GitHubLightAnsi[10], GitHubLightAnsi[11], GitHubLightAnsi[12], GitHubLightAnsi[13], GitHubLightAnsi[14],
                  GitHubLightAnsi[15]});
    list << make("solarized", QStringLiteral("Solarized Dark"), true,
                 {"#00212B", "#002B36", "#073642", "#0E4A58", "#0A3F4C", "#EEE8D5", "#93A1A1", "#268BD2", "#FFFFFF"},
                 {"#93A1A1", "#002B36", "#93A1A1", "#0E4A58"},
                 {"#073642", "#DC322F", "#859900", "#B58900", "#268BD2", "#D33682", "#2AA198", "#EEE8D5",
                  "#586E75", "#CB4B16", "#586E75", "#657B83", "#839496", "#6C71C4", "#93A1A1", "#FDF6E3"});
    return list;
}

int g_current = 0;
bool g_glow = false;

QColor mix(const QColor &a, const QColor &b, qreal t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QString rgba(const QColor &c, int alpha) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// A bit further from the background: lighter on dark themes, darker on light ones.
QColor raise(const QColor &c, int percent) {
    return current().dark ? c.lighter(100 + percent) : c.darker(100 + percent / 2);
}

QColor glyphColor() {
    return mix(current().text, current().muted, 0.25);
}

// The style sheet needs files for its small images (check mark, arrows): draw
// them once in the app's own cache folder (the app has no resource file). The
// colour is in the name, so each theme gets its own images.
QString styleImage(const QString &name, const QColor &color, const QPolygonF &line, qreal width) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) dir = QDir::tempPath();
    const QString path = dir + QStringLiteral("/style-") + name + QLatin1Char('-') + color.name().mid(1) + QStringLiteral(".png");
    QPixmap pixmap(30, 30);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(line);
    p.end();
    pixmap.save(path, "PNG");
    return QDir::fromNativeSeparators(path);
}

QString checkImage() {
    return styleImage(QStringLiteral("check"), Qt::white,
                      QPolygonF({QPointF(7, 15.5), QPointF(12.5, 21), QPointF(23, 9)}), 3.6);
}
QString chevronDown() {
    return styleImage(QStringLiteral("down"), current().muted, QPolygonF({QPointF(7, 11), QPointF(15, 19), QPointF(23, 11)}), 3.4);
}
QString chevronLeft() {
    return styleImage(QStringLiteral("left"), current().text, QPolygonF({QPointF(19, 7), QPointF(11, 15), QPointF(19, 23)}), 3.4);
}
QString chevronRight() {
    return styleImage(QStringLiteral("right"), current().text, QPolygonF({QPointF(11, 7), QPointF(19, 15), QPointF(11, 23)}), 3.4);
}
QString chevronUp() {
    return styleImage(QStringLiteral("up"), current().muted, QPolygonF({QPointF(7, 19), QPointF(15, 11), QPointF(23, 19)}), 3.4);
}

QString styleSheet() {
    QString css = QStringLiteral(R"(
QMainWindow, QDialog { background: @window; }
QMainWindow::separator { background: @window; width: 6px; height: 6px; }
QMainWindow::separator:hover { background: @accentSoft; }

QMenuBar { background: @window; padding: 3px 6px; border: none; }
QMenuBar::item { padding: 5px 11px; border-radius: 6px; background: transparent; color: @text; }
QMenuBar::item:selected { background: @hover; }
QMenuBar::item:pressed { background: @accentSoft; }

QMenu { background: @surface; border: 1px solid @border; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 6px 26px 6px 12px; border-radius: 6px; color: @text; }
QMenu::item:selected { background: @accentSoft; color: @strong; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
QMenu::icon { padding-left: 6px; }

QToolTip { background: @surface; color: @text; border: 1px solid @border; border-radius: 6px; padding: 5px 8px; }

QToolBar { background: @window; border: none; spacing: 2px; padding: 3px 8px; }
QToolBar::separator { background: @border; width: 1px; margin: 6px 6px; }
QToolButton { background: transparent; border: none; border-radius: 6px; padding: 5px; color: @text; }
QToolButton:hover { background: @hover; }
QToolButton:pressed, QToolButton:checked { background: @accentSoft; }
QToolButton[popupMode="1"] { padding-right: 14px; }
QToolButton::menu-button { background: transparent; border: none; width: 12px; border-radius: 4px; }
QToolButton::menu-button:hover { background: @hover; }

QTabWidget::pane { border: none; background: @terminal; }
QTabWidget::tab-bar { left: 4px; }
QTabBar { background: @window; qproperty-drawBase: 0; }
QTabBar::tab {
    background: transparent; color: @muted; border: none;
    padding: 7px 12px 7px 12px; margin: 5px 1px 0 1px; min-width: 90px; max-width: 260px;
    border-top-left-radius: 9px; border-top-right-radius: 9px;
}
QTabBar::tab:hover:!selected { background: @hover; color: @text; }
QTabBar::tab:selected { background: @terminal; color: @strong; }
QTabBar::scroller { width: 48px; }
QTabBar QToolButton { background: @window; border: none; border-radius: 6px; margin: 6px 1px 2px 1px; padding: 0; }
QTabBar QToolButton:hover { background: @hover; }
QTabBar QToolButton::left-arrow { image: url(@chevronLeft); width: 10px; height: 10px; }
QTabBar QToolButton::right-arrow { image: url(@chevronRight); width: 10px; height: 10px; }
QTabBar QToolButton:disabled { background: @window; }
QToolButton#tabClose { background: transparent; margin: 0; padding: 1px; border-radius: 5px; }
QToolButton#tabClose:hover { background: #E5484D; }

QDockWidget { color: @muted; font-weight: 600; }
QDockWidget::title { background: @panel; padding: 8px 10px; text-align: left; border-bottom: 1px solid @window; }
QDockWidget > QWidget { background: @panel; }

QTreeView, QListView, QTableView, QTreeWidget, QListWidget, QTableWidget {
    background: @panel; alternate-background-color: @alt; border: none; outline: 0;
    selection-background-color: @accentSoft; selection-color: @strong;
}
QTreeView::item, QListView::item { padding: 3px 2px; border-radius: 5px; }
QTreeView::item:hover, QListView::item:hover { background: @hover; }
QTreeView::item:selected, QListView::item:selected { background: @accentSoft; color: @strong; }
QHeaderView::section { background: @panel; color: @muted; border: none; border-bottom: 1px solid @border; padding: 5px 8px; }

QLineEdit, QPlainTextEdit, QTextEdit {
    background: @surface; color: @text; border: 1px solid @border; border-radius: 7px;
    padding: 5px 8px; selection-background-color: @accent;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
QComboBox { background: @surface; color: @text; border: 1px solid @border; border-radius: 7px; padding: 4px 26px 4px 8px; min-height: 20px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 24px; border: none; background: transparent; }
QComboBox::down-arrow { image: url(@chevronDown); width: 10px; height: 10px; }
QComboBox::down-arrow:on { top: 1px; }
QSpinBox { background: @surface; color: @text; border: 1px solid @border; border-radius: 7px; padding-left: 6px; padding-right: 24px; min-height: 26px; }
QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 20px; border: none; background: transparent; }
QSpinBox::up-button { subcontrol-position: top right; margin: 3px 3px 0 0; border-top-right-radius: 5px; }
QSpinBox::down-button { subcontrol-position: bottom right; margin: 0 3px 3px 0; border-bottom-right-radius: 5px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: @hover; }
QSpinBox::up-arrow { image: url(@chevronUp); width: 8px; height: 8px; }
QSpinBox::down-arrow { image: url(@chevronDown); width: 8px; height: 8px; }
QSpinBox::up-arrow:disabled, QSpinBox::down-arrow:disabled { image: none; }
QToolButton::menu-indicator { image: url(@chevronDown); width: 8px; height: 8px; subcontrol-origin: padding; subcontrol-position: center right; right: 4px; }
QToolButton#osButton { background: @surface; border: 1px solid @border; border-radius: 7px; padding: 2px 16px 2px 6px; }
QToolButton#osButton:hover { border-color: @borderHover; }
QComboBox:hover, QSpinBox:hover { border-color: @borderHover; }
QComboBox:focus, QSpinBox:focus { border-color: @accent; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @border; border-radius: 8px; padding: 4px; selection-background-color: @accentSoft; }

QPushButton {
    background: @surface; color: @text; border: 1px solid @border; border-radius: 7px;
    padding: 6px 16px; min-height: 18px;
}
QPushButton:hover { background: @hover; border-color: @borderHover; }
QPushButton:pressed { background: @pressed; }
QPushButton:default, QPushButton#accentButton { background: @accent; border-color: @accent; color: white; }
QPushButton#accentButton { font-weight: 600; }
QPushButton:default:hover, QPushButton#accentButton:hover { background: @accentHover; border-color: @accentHover; }
QPushButton:disabled { color: @muted; background: @panel; }

QCheckBox, QRadioButton, QLabel { color: @text; background: transparent; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid @borderHover; border-radius: 4px; background: @surface; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(@checkImage); }

QScrollBar:vertical { background: transparent; width: 11px; margin: 2px 1px 2px 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0 2px 1px 2px; }
QScrollBar::handle { background: @scroll; border-radius: 4px; min-height: 28px; min-width: 28px; margin: 2px; }
QScrollBar::handle:hover { background: @scrollHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QProgressBar { background: @surface; border: none; border-radius: 5px; text-align: center; color: @text; min-height: 10px; }
QProgressBar::chunk { background: @accent; border-radius: 5px; }

QStatusBar { background: @window; color: @muted; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @muted; }
QStatusBar QToolButton { padding: 2px 8px; color: @muted; }
QStatusBar QToolButton:checked { color: @strong; }

QSplitter::handle { background: @window; }

QWidget#rdpView { background: @terminal; }
QWidget#rdpCard { background: @panel; border: 1px solid @border; border-radius: 14px; }
QLabel#rdpTitle { font-size: 16pt; font-weight: 600; color: @strong; }
QLabel#rdpAddress { color: @muted; }
QLabel#rdpStatus { color: @text; }
)");
    if (g_glow) {
        // Contrasted edges: crisp 1 px neutral lines, a step lighter than the
        // background on dark themes (darker on light ones), that separate
        // every area and outline the controls. No colour, no glow.
        css += QStringLiteral(R"(
QToolBar { border-bottom: 1px solid @edge; }
QStatusBar { border-top: 1px solid @edge; }
QMainWindow::separator { background: @edge; width: 1px; height: 1px; }
QSplitter::handle { background: @edge; }
QDockWidget::title { border-bottom: 1px solid @edge; }
QHeaderView::section { border-bottom: 1px solid @edge; }
QTabBar::tab:selected { border: 1px solid @edge; border-bottom: none; }
QMenu, QToolTip, QComboBox QAbstractItemView { border: 1px solid @edgeStrong; }
QPushButton, QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTextEdit { border-color: @edge; }
QPushButton:hover, QComboBox:hover, QSpinBox:hover { border-color: @edgeStrong; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @edgeStrong; }
QWidget#rdpCard { border: 1px solid @edge; }
)");
    }
    const ColorTheme &t = current();
    const std::pair<const char *, QString> tokens[] = {
        // Longest names first: "@accentSoft" must not be read as "@accent".
        {"@accentSoft", rgba(t.accent, 90)},
        {"@accentHover", raise(t.accent, 15).name()},
        {"@borderHover", raise(t.border, 25).name()},
        {"@scrollHover", raise(t.border, 60).name()},
        {"@edgeStrong", raise(t.border, 70).name()},
        {"@edge", raise(t.border, 35).name()},
        {"@glowSoft", rgba(t.accent, 22)},
        {"@terminal", t.terminal.background.name()},
        {"@surface", t.surface.name()},
        {"@pressed", raise(t.hover, 15).name()},
        {"@window", t.window.name()},
        {"@border", t.border.name()},
        {"@strong", t.strong.name()},
        {"@accent", t.accent.name()},
        {"@scroll", raise(t.border, 30).name()},
        {"@panel", t.panel.name()},
        {"@hover", t.hover.name()},
        {"@muted", t.muted.name()},
        {"@glow", rgba(t.accent, 200)},
        {"@text", t.text.name()},
        {"@alt", mix(t.panel, t.surface, 0.4).name()},
    };
    css.replace(QLatin1String("@checkImage"), checkImage());
    css.replace(QLatin1String("@chevronDown"), chevronDown());
    css.replace(QLatin1String("@chevronUp"), chevronUp());
    css.replace(QLatin1String("@chevronLeft"), chevronLeft());
    css.replace(QLatin1String("@chevronRight"), chevronRight());
    for (const auto &[token, value] : tokens) css.replace(QLatin1String(token), value);
    return css;
}

void applyPalette() {
    const ColorTheme &t = current();
    const QColor disabled = mix(t.muted, t.panel, 0.35);
    QPalette palette;
    palette.setColor(QPalette::Window, t.window);
    palette.setColor(QPalette::WindowText, t.text);
    palette.setColor(QPalette::Base, t.surface);
    palette.setColor(QPalette::AlternateBase, t.panel);
    palette.setColor(QPalette::Text, t.text);
    palette.setColor(QPalette::Button, t.surface);
    palette.setColor(QPalette::ButtonText, t.text);
    palette.setColor(QPalette::BrightText, t.strong);
    palette.setColor(QPalette::Highlight, t.accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, t.dark ? t.accent.lighter(120) : t.accent);
    palette.setColor(QPalette::ToolTipBase, t.surface);
    palette.setColor(QPalette::ToolTipText, t.text);
    palette.setColor(QPalette::PlaceholderText, mix(t.muted, t.surface, 0.25));
    palette.setColor(QPalette::Mid, t.border);
    palette.setColor(QPalette::Dark, t.window);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    QApplication::setPalette(palette);
}

void applyAll() {
    applyPalette();
    qApp->setStyleSheet(styleSheet());
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (w->property("fs_decorated").toBool()) decorate(w);
    }
}

// Decorates every top-level window (main window, dialogs) when first shown.
class WindowDecorator : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Show && watched->isWidgetType()) {
            auto *widget = static_cast<QWidget *>(watched);
            const Qt::WindowType type = widget->windowType();
            if (widget->isWindow() && (type == Qt::Window || type == Qt::Dialog)
                && !widget->property("fs_decorated").toBool()) {
                widget->setProperty("fs_decorated", true);
                decorate(widget);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

}  // namespace

const QList<ColorTheme> &themes() {
    static const QList<ColorTheme> list = buildThemes();
    return list;
}

const ColorTheme &current() {
    return themes().at(g_current);
}

bool glow() {
    return g_glow;
}

Notifier *notifier() {
    static Notifier instance;
    return &instance;
}

// Title bar in the theme colours (dark or light), rounded corners on
// Windows 11. Older versions ignore the attributes they do not know.
void decorate(QWidget *window) {
#ifdef Q_OS_WIN
    const ColorTheme &t = current();
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = t.dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark))) {
        DwmSetWindowAttribute(hwnd, 19 /* same, before Windows 10 20H1 */, &dark, sizeof dark);
    }
    const DWORD corners = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof corners);
    const COLORREF caption = RGB(t.window.red(), t.window.green(), t.window.blue());
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
    const COLORREF captionText = RGB(t.text.red(), t.text.green(), t.text.blue());
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &captionText, sizeof captionText);
    const QColor edge = t.border;  // the window frame stays neutral, glow or not
    const COLORREF border = RGB(edge.red(), edge.green(), edge.blue());
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof border);
#else
    Q_UNUSED(window);
#endif
}

void apply(QApplication &app) {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Segoe UI Variable (Windows 11), else Segoe UI; elsewhere the system font.
    const QStringList families = QFontDatabase::families();
    for (const QString &family : {QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}) {
        if (families.contains(family)) {
            QFont font(family);
            font.setPointSizeF(9.5);
            QApplication::setFont(font);
            break;
        }
    }

    const QSettings settings;
    const QString id = settings.value(QStringLiteral("appearance/theme")).toString();
    for (int i = 0; i < themes().size(); ++i) {
        if (themes().at(i).id == id) g_current = i;
    }
    g_glow = settings.value(QStringLiteral("appearance/edges"), false).toBool();

    applyPalette();
    app.setStyleSheet(styleSheet());
    app.installEventFilter(new WindowDecorator(&app));
}

void setTheme(const QString &id) {
    for (int i = 0; i < themes().size(); ++i) {
        if (themes().at(i).id != id) continue;
        g_current = i;
        QSettings().setValue(QStringLiteral("appearance/theme"), id);
        applyAll();
        Q_EMIT notifier()->changed();
        return;
    }
}

void setGlow(bool on) {
    g_glow = on;
    QSettings().setValue(QStringLiteral("appearance/edges"), on);
    applyAll();
    Q_EMIT notifier()->changed();
}

QIcon icon(Glyph glyph) {
    constexpr int size = 40;  // drawn at 2x for high-DPI screens
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(glyphColor(), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QRectF box(7, 9, 26, 22);
    auto accentPen = [&] {
        QPen a = pen;
        a.setColor(current().accent);
        p.setPen(a);
    };

    switch (glyph) {
    case Glyph::NewSession:
        p.drawRoundedRect(box, 4, 4);
        accentPen();
        p.drawLine(QPointF(20, 14), QPointF(20, 26));
        p.drawLine(QPointF(14, 20), QPointF(26, 20));
        break;
    case Glyph::Terminal:
        p.drawRoundedRect(box, 4, 4);
        accentPen();
        p.drawPolyline(QPolygonF({QPointF(12, 15), QPointF(17, 20), QPointF(12, 25)}));
        p.drawLine(QPointF(20, 25), QPointF(27, 25));
        break;
    case Glyph::SplitRight:
        p.drawRoundedRect(box, 4, 4);
        accentPen();
        p.drawLine(QPointF(20, 9), QPointF(20, 31));
        break;
    case Glyph::SplitDown:
        p.drawRoundedRect(box, 4, 4);
        accentPen();
        p.drawLine(QPointF(7, 20), QPointF(33, 20));
        break;
    case Glyph::Broadcast: {
        accentPen();
        p.setBrush(current().accent);
        p.drawEllipse(QPointF(20, 20), 2.6, 2.6);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        for (const qreal r : {8.0, 14.0}) {
            p.drawArc(QRectF(20 - r, 20 - r, 2 * r, 2 * r), 30 * 16, 120 * 16);
            p.drawArc(QRectF(20 - r, 20 - r, 2 * r, 2 * r), 210 * 16, 120 * 16);
        }
        break;
    }
    case Glyph::RemoteDesktop:
        p.drawRoundedRect(QRectF(6, 8, 28, 19), 3, 3);
        p.drawLine(QPointF(20, 27), QPointF(20, 32));
        p.drawLine(QPointF(14, 32), QPointF(26, 32));
        accentPen();
        p.drawLine(QPointF(14, 18), QPointF(25, 18));
        p.drawPolyline(QPolygonF({QPointF(21, 14), QPointF(25, 18), QPointF(21, 22)}));
        break;
    case Glyph::Panels:
        p.drawRoundedRect(box, 4, 4);
        accentPen();
        p.drawLine(QPointF(15, 9), QPointF(15, 31));
        break;
    case Glyph::Close:
        pen.setWidthF(3);
        p.setPen(pen);
        p.drawLine(QPointF(13, 13), QPointF(27, 27));
        p.drawLine(QPointF(27, 13), QPointF(13, 27));
        break;
    case Glyph::ZoomIn:
    case Glyph::ZoomOut:  // a magnifier with + or -
        p.drawEllipse(QPointF(17, 17), 9, 9);
        p.drawLine(QPointF(24, 24), QPointF(32, 32));
        accentPen();
        p.drawLine(QPointF(12.5, 17), QPointF(21.5, 17));
        if (glyph == Glyph::ZoomIn) p.drawLine(QPointF(17, 12.5), QPointF(17, 21.5));
        break;
    case Glyph::Font:  // a capital A
        p.drawPolyline(QPolygonF({QPointF(9, 32), QPointF(20, 8), QPointF(31, 32)}));
        accentPen();
        p.drawLine(QPointF(13.5, 24), QPointF(26.5, 24));
        break;
    case Glyph::Clock:
        p.drawEllipse(QPointF(20, 20), 12, 12);
        accentPen();
        p.drawPolyline(QPolygonF({QPointF(20, 12), QPointF(20, 20), QPointF(26, 23)}));
        break;
    case Glyph::LineNumbers:  // a page with a numbered margin
        p.drawRoundedRect(box, 4, 4);
        p.drawLine(QPointF(15, 9), QPointF(15, 31));
        for (const qreal y : {15.0, 20.0, 25.0}) p.drawLine(QPointF(19, y), QPointF(28, y));
        accentPen();
        for (const qreal y : {15.0, 20.0, 25.0}) p.drawPoint(QPointF(11, y));
        break;
    case Glyph::SessionList:  // the session panel: a list in a side column
        p.drawRoundedRect(box, 4, 4);
        p.drawLine(QPointF(22, 9), QPointF(22, 31));
        accentPen();
        for (const qreal y : {15.0, 20.0, 25.0}) p.drawLine(QPointF(25.5, y), QPointF(29.5, y));
        break;
    case Glyph::Filer:  // a folder
        p.drawPolyline(QPolygonF({QPointF(7, 29), QPointF(7, 11), QPointF(15, 11), QPointF(18, 14), QPointF(33, 14),
                                  QPointF(33, 29), QPointF(7, 29)}));
        accentPen();
        p.drawLine(QPointF(7, 19), QPointF(33, 19));
        break;
    case Glyph::Outline:  // a bulleted list
        for (const qreal y : {12.0, 20.0, 28.0}) p.drawLine(QPointF(15, y), QPointF(33, y));
        accentPen();
        for (const qreal y : {12.0, 20.0, 28.0}) p.drawPoint(QPointF(8.5, y));
        break;
    case Glyph::FullScreen:  // four corners
        p.drawPolyline(QPolygonF({QPointF(7, 15), QPointF(7, 8), QPointF(14, 8)}));
        p.drawPolyline(QPolygonF({QPointF(26, 8), QPointF(33, 8), QPointF(33, 15)}));
        p.drawPolyline(QPolygonF({QPointF(33, 25), QPointF(33, 32), QPointF(26, 32)}));
        p.drawPolyline(QPolygonF({QPointF(14, 32), QPointF(7, 32), QPointF(7, 25)}));
        accentPen();
        p.drawRoundedRect(QRectF(14, 15, 12, 10), 2, 2);
        break;
    }
    p.end();
    pixmap.setDevicePixelRatio(2.0);
    return QIcon(pixmap);
}

}  // namespace style

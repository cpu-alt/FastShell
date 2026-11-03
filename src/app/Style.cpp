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
#include <QStyleFactory>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace style {

namespace {

QString rgba(const QColor &c, int alpha) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// The style sheet needs a file for the check mark of the check boxes: draw it
// once in the temporary folder (the app has no resource file).
QString checkImage() {
    const QString path = QDir::temp().filePath(QStringLiteral("emfhexa-check.png"));
    QPixmap pixmap(30, 30);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, 3.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF({QPointF(7, 15.5), QPointF(12.5, 21), QPointF(23, 9)}));
    p.end();
    pixmap.save(path, "PNG");
    return QDir::fromNativeSeparators(path);
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
QMenu::item:selected { background: @accentSoft; color: white; }
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
QToolButton#tabClose { padding: 1px; border-radius: 5px; }
QToolButton#tabClose:hover { background: #E5484D; }

QTabWidget::pane { border: none; background: @terminal; }
QTabWidget::tab-bar { left: 4px; }
QTabBar { background: @window; qproperty-drawBase: 0; }
QTabBar::tab {
    background: transparent; color: @muted; border: none;
    padding: 7px 12px 7px 12px; margin: 5px 1px 0 1px; min-width: 90px; max-width: 260px;
    border-top-left-radius: 9px; border-top-right-radius: 9px;
}
QTabBar::tab:hover:!selected { background: @hover; color: @text; }
QTabBar::tab:selected { background: @terminal; color: white; }
QTabBar::scroller { width: 26px; }

QDockWidget { color: @muted; font-weight: 600; }
QDockWidget::title { background: @panel; padding: 8px 10px; text-align: left; border-bottom: 1px solid @window; }
QDockWidget > QWidget { background: @panel; }

QTreeView, QListView, QTableView, QTreeWidget, QListWidget, QTableWidget {
    background: @panel; alternate-background-color: #1C2027; border: none; outline: 0;
    selection-background-color: @accentSoft; selection-color: white;
}
QTreeView::item, QListView::item { padding: 3px 2px; border-radius: 5px; }
QTreeView::item:hover, QListView::item:hover { background: @hover; }
QTreeView::item:selected, QListView::item:selected { background: @accentSoft; color: white; }
QHeaderView::section { background: @panel; color: @muted; border: none; border-bottom: 1px solid @border; padding: 5px 8px; }

QLineEdit, QPlainTextEdit, QTextEdit {
    background: @surface; color: @text; border: 1px solid @border; border-radius: 7px;
    padding: 5px 8px; selection-background-color: @accent;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
QComboBox { background: @surface; color: @text; border: 1px solid @border; border-radius: 7px; padding: 4px 8px; min-height: 20px; }
QSpinBox { background: @surface; color: @text; border: 1px solid @border; border-radius: 7px; padding-left: 6px; min-height: 26px; }
QComboBox:hover, QSpinBox:hover { border-color: #3A414D; }
QComboBox:focus, QSpinBox:focus { border-color: @accent; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @border; border-radius: 8px; padding: 4px; selection-background-color: @accentSoft; }

QPushButton {
    background: @surface; color: @text; border: 1px solid @border; border-radius: 7px;
    padding: 6px 16px; min-height: 18px;
}
QPushButton:hover { background: @hover; border-color: #3A414D; }
QPushButton:pressed { background: #2E343E; }
QPushButton:default, QPushButton#accentButton { background: @accent; border-color: @accent; color: white; font-weight: 600; }
QPushButton:default:hover, QPushButton#accentButton:hover { background: #5AACF7; border-color: #5AACF7; }
QPushButton:disabled { color: @muted; background: @panel; }

QCheckBox, QRadioButton, QLabel { color: @text; background: transparent; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #4A515E; border-radius: 4px; background: @surface; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(@checkImage); }

QScrollBar:vertical { background: transparent; width: 11px; margin: 2px 1px 2px 0; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0 2px 1px 2px; }
QScrollBar::handle { background: #3A404B; border-radius: 4px; min-height: 28px; min-width: 28px; margin: 2px; }
QScrollBar::handle:hover { background: #4C5462; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QProgressBar { background: @surface; border: none; border-radius: 5px; text-align: center; color: @text; min-height: 10px; }
QProgressBar::chunk { background: @accent; border-radius: 5px; }

QStatusBar { background: @window; color: @muted; border-top: 1px solid #1C1F25; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @muted; }
QStatusBar QToolButton { padding: 2px 8px; color: @muted; }
QStatusBar QToolButton:checked { color: white; }

QSplitter::handle { background: @window; }

QWidget#rdpView { background: @terminal; }
QWidget#rdpCard { background: @panel; border: 1px solid @border; border-radius: 14px; }
QLabel#rdpTitle { font-size: 16pt; font-weight: 600; color: white; }
QLabel#rdpAddress { color: @muted; }
QLabel#rdpStatus { color: @text; }
)");
    const std::pair<const char *, QString> tokens[] = {
        {"@window", Window.name()},     {"@panel", Panel.name()},   {"@surface", Surface.name()},
        {"@border", Border.name()},     {"@hover", Hover.name()},   {"@text", Text.name()},
        {"@muted", Muted.name()},       {"@accentSoft", rgba(Accent, 90)},
        {"@accent", Accent.name()},     {"@terminal", TerminalBackground.name()},
    };
    for (const auto &[token, value] : tokens) css.replace(QLatin1String(token), value);
    css.replace(QLatin1String("@checkImage"), checkImage());
    return css;
}

#ifdef Q_OS_WIN
// Dark title bar in the app colours, rounded corners on Windows 11. Older
// versions ignore the attributes they do not know.
void decorate(QWidget *window) {
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark))) {
        DwmSetWindowAttribute(hwnd, 19 /* same, before Windows 10 20H1 */, &dark, sizeof dark);
    }
    const DWORD corners = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof corners);
    const COLORREF caption = RGB(Window.red(), Window.green(), Window.blue());
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
    const COLORREF captionText = RGB(Text.red(), Text.green(), Text.blue());
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &captionText, sizeof captionText);
}
#endif

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
                && !widget->property("emfhexa_decorated").toBool()) {
                widget->setProperty("emfhexa_decorated", true);
#ifdef Q_OS_WIN
                decorate(widget);
#endif
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

}  // namespace

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

    const QColor disabled(0x5E, 0x64, 0x6F);
    QPalette palette;
    palette.setColor(QPalette::Window, Window);
    palette.setColor(QPalette::WindowText, Text);
    palette.setColor(QPalette::Base, Surface);
    palette.setColor(QPalette::AlternateBase, Panel);
    palette.setColor(QPalette::Text, Text);
    palette.setColor(QPalette::Button, Surface);
    palette.setColor(QPalette::ButtonText, Text);
    palette.setColor(QPalette::Highlight, Accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, Accent.lighter(120));
    palette.setColor(QPalette::ToolTipBase, Surface);
    palette.setColor(QPalette::ToolTipText, Text);
    palette.setColor(QPalette::PlaceholderText, QColor(0x6C, 0x73, 0x80));
    palette.setColor(QPalette::Mid, Border);
    palette.setColor(QPalette::Dark, Window);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    QApplication::setPalette(palette);

    app.setStyleSheet(styleSheet());
    app.installEventFilter(new WindowDecorator(&app));
}

QIcon icon(Glyph glyph) {
    constexpr int size = 40;  // drawn at 2x for high-DPI screens
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(0xC9, 0xCE, 0xD6), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QRectF box(7, 9, 26, 22);
    auto accentPen = [&] {
        QPen a = pen;
        a.setColor(Accent);
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
        p.setBrush(Accent);
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

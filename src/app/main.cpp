// Copyright 2026 EMF Hexa contributors. Apache-2.0.
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>

#include "app/Logo.h"
#include "app/MainWindow.h"

namespace {

// The app used to be called "WindTerm NG". Bring its sessions and settings
// along the first time EMF Hexa starts, so nothing has to be re-entered.
int savedItemCount(const QString &file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    return int(root.value(QStringLiteral("sessions")).toArray().size()
               + root.value(QStringLiteral("credentials")).toArray().size());
}

void migrateFromWindTermNG() {
    QSettings current;
    if (!current.value(QStringLiteral("migratedFromWindTermNG")).toBool()) {
        QSettings old(QStringLiteral("WindTermNG"), QStringLiteral("WindTermNG"));
        for (const QString &key : old.allKeys()) {
            if (!current.contains(key)) current.setValue(key, old.value(key));
        }
        current.setValue(QStringLiteral("migratedFromWindTermNG"), true);
    }

    // Sessions: as long as EMF Hexa has none of its own, take the old ones
    // (an old WindTerm NG window may still be open and saving them).
    const QString newDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString oldDir = newDir;
    oldDir.replace(QStringLiteral("EMF NET/EMFHexa"), QStringLiteral("WindTermNG/WindTermNG"));
    const QString oldFile = oldDir + QStringLiteral("/sessions.json");
    const QString newFile = newDir + QStringLiteral("/sessions.json");
    if (oldDir != newDir && savedItemCount(oldFile) > 0 && savedItemCount(newFile) == 0) {
        QDir().mkpath(newDir);
        QFile::remove(newFile);
        QFile::copy(oldFile, newFile);
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("EMF NET"));
    QApplication::setApplicationName(QStringLiteral("EMFHexa"));
    QApplication::setApplicationDisplayName(QStringLiteral("EMF Hexa"));
    QApplication::setApplicationVersion(QStringLiteral(WTNG_VERSION));
    QApplication::setWindowIcon(logo::icon());
    migrateFromWindTermNG();

    // Dark Fusion look that matches the terminal and the logo.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const QColor text(0xD4, 0xD7, 0xDD);
    const QColor disabled(0x6A, 0x6E, 0x78);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x1E, 0x22, 0x2A));
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, QColor(0x17, 0x1B, 0x22));
    palette.setColor(QPalette::AlternateBase, QColor(0x25, 0x2A, 0x33));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, QColor(0x25, 0x2A, 0x33));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, logo::Blue);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, logo::Cyan);
    palette.setColor(QPalette::ToolTipBase, QColor(0x25, 0x2A, 0x33));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, QColor(0x7F, 0x84, 0x8E));
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    QApplication::setPalette(palette);

    // Command line: targets in quick-connect syntax ("admin@10.0.0.1",
    // "COM3 115200", "local"), optionally all in one tab side by side.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("EMF Hexa terminal"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption sideBySide(QStringLiteral("side-by-side"),
                                        QStringLiteral("Open all targets side by side in one tab."));
    parser.addOption(sideBySide);
    parser.addPositionalArgument(QStringLiteral("targets"),
                                 QStringLiteral("user@host[:port], telnet host, \"COM3 115200\" or local"));
    parser.process(app);

    QList<SessionInfo> targets;
    for (const QString &arg : parser.positionalArguments()) {
        if (arg.compare(QLatin1String("local"), Qt::CaseInsensitive) == 0) {
            SessionInfo s;
            s.type = SessionInfo::Type::Local;
            s.name = QStringLiteral("local");
            targets << s;
        } else if (auto s = SessionStore::parseQuickConnect(arg)) {
            targets << *s;
        }
    }

    MainWindow window;
    window.show();
    if (targets.isEmpty()) {
        window.newLocalTab();
    } else if (parser.isSet(sideBySide) && targets.size() > 1) {
        window.openSideBySide(targets);
    } else {
        for (const SessionInfo &s : targets) window.openSession(s);
    }
    return app.exec();
}

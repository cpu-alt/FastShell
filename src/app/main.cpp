// Copyright 2026 FastShell contributors. Apache-2.0.
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

#include "app/Logo.h"
#include "app/MainWindow.h"
#include "app/Style.h"

namespace {

// The app used to be called "EMF Hexa", and "WindTerm NG" before that. Bring
// their sessions and settings along the first time FastShell starts, so
// nothing has to be re-entered (passwords are DPAPI blobs of the Windows
// user, they stay readable).
int savedItemCount(const QString &file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    return int(root.value(QStringLiteral("sessions")).toArray().size()
               + root.value(QStringLiteral("credentials")).toArray().size());
}

void migrateFromOldNames() {
    struct OldName {
        const char *organization;
        const char *application;
    };
    const OldName oldNames[] = {{"EMF NET", "EMFHexa"}, {"WindTermNG", "WindTermNG"}};  // newest first

    // Settings: keys missing here are taken from the old names, once each.
    QSettings current;
    for (const OldName &old : oldNames) {
        const QString flag = QStringLiteral("migratedFrom") + QLatin1String(old.application);
        if (current.value(flag).toBool()) continue;
        QSettings settings(QLatin1String(old.organization), QLatin1String(old.application));
        for (const QString &key : settings.allKeys()) {
            if (!key.startsWith(QLatin1String("migratedFrom")) && !current.contains(key)) {
                current.setValue(key, settings.value(key));
            }
        }
        current.setValue(flag, true);
    }

    // Sessions: as long as FastShell has none of its own, take the newest old
    // ones (an old window may still be open and saving them).
    const QString newDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString newFile = newDir + QStringLiteral("/sessions.json");
    if (savedItemCount(newFile) > 0) return;
    const QString here = QStringLiteral("%1/%2").arg(QApplication::organizationName(), QApplication::applicationName());
    for (const OldName &old : oldNames) {
        QString oldDir = newDir;
        oldDir.replace(here, QStringLiteral("%1/%2").arg(QLatin1String(old.organization), QLatin1String(old.application)));
        const QString oldFile = oldDir + QStringLiteral("/sessions.json");
        if (oldDir != newDir && savedItemCount(oldFile) > 0) {
            QDir().mkpath(newDir);
            QFile::remove(newFile);
            QFile::copy(oldFile, newFile);
            return;
        }
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("EMF NET"));
    QApplication::setApplicationName(QStringLiteral("FastShell"));
    QApplication::setApplicationDisplayName(QStringLiteral("FastShell"));
    QApplication::setApplicationVersion(QStringLiteral(WTNG_VERSION));
    QApplication::setWindowIcon(logo::icon());
    migrateFromOldNames();

    // Modern dark look that matches the terminal and the logo.
    style::apply(app);

    // Command line: targets in quick-connect syntax ("admin@10.0.0.1",
    // "COM3 115200", "rdp host", "local"), optionally all in one tab side by side.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("FastShell terminal"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption sideBySide(QStringLiteral("side-by-side"),
                                        QStringLiteral("Open all targets side by side in one tab."));
    parser.addOption(sideBySide);
    parser.addPositionalArgument(QStringLiteral("targets"),
                                 QStringLiteral("user@host[:port], telnet host, rdp host, \"COM3 115200\" or local"));
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

// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Checks GitHub for a newer FastShell (at most once a day, or on demand),
// downloads the package over HTTPS, verifies its SHA-256 and installs it:
// a small PowerShell script waits for FastShell to close, copies the new
// files over the installed ones and starts the new version.
#pragma once

#include <QObject>
#include <QPointer>

#include "app/UpdateCheck.h"

class QNetworkAccessManager;
class QNetworkReply;
class QProgressDialog;
class QWidget;

class Updater : public QObject {
    Q_OBJECT

public:
    explicit Updater(QWidget *window);

    // Silent unless a new version is found; skipped if checked today.
    void checkAutomatically();
    // Help > Check for updates: always says something.
    void checkNow();

    static bool automaticChecks();
    static void setAutomaticChecks(bool on);

private:
    void check(bool interactive);
    void offer(const update::Release &release, bool interactive);
    void install(const update::Release &release);
    void fail(const QString &message);
    QNetworkReply *get(const QUrl &url);

    QWidget *m_window;
    QNetworkAccessManager *m_network;
    QPointer<QProgressDialog> m_progress;
    bool m_busy = false;
};

// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Session > File server: the built-in TFTP / HTTP server (net/FileServer),
// to send a firmware or any file from this PC to a switch. Folder, network
// interface, ports and uploads are remembered (QSettings "files/...").
#pragma once

#include <QDialog>

#include "net/FileServer.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;

class FileServerDialog : public QDialog {
    Q_OBJECT

public:
    FileServerDialog(FileServer *server, QWidget *parent = nullptr);

    // The settings saved by this window (folder: the given one if not empty).
    static FileServer::Config savedConfig(const QString &folder = QString());
    static void saveConfig(const FileServer::Config &config);
    // Starts the server with these settings; a message box explains a failure
    // (port 69 taken by another TFTP server...).
    static bool start(FileServer *server, const FileServer::Config &config, QWidget *parent);

private:
    void toggle();
    void refresh();
    FileServer::Config fromFields() const;

    FileServer *m_server;
    QLineEdit *m_folder;
    QComboBox *m_address;
    QSpinBox *m_tftpPort;
    QCheckBox *m_http;
    QSpinBox *m_httpPort;
    QCheckBox *m_upload;
    QPushButton *m_startStop;
    QLabel *m_hint;
    QTreeWidget *m_list;
    QTimer *m_timer;
};

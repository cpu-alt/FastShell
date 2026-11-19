// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Built-in text editor, as simple as Notepad: mouse to place the cursor and
// select, Ctrl+C/X/V and right-click menu, undo/redo, find/replace, go to
// line, line numbers, Ctrl+S to save. Opens local files, or remote files
// over the SFTP connection of an SSH tab (read and written in memory only,
// never through a local temporary file: config files hold secrets).
#pragma once

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>

#include "app/EditorText.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class SftpClient;

class CodeEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEdit(QWidget *parent = nullptr);
    int gutterWidth() const;
    void paintGutter(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *m_gutter;
};

class EditorWindow : public QMainWindow {
    Q_OBJECT

public:
    // Opens a local file. Returns the window (already shown).
    static EditorWindow *openLocal(const QString &path, QWidget *parent = nullptr);
    // Opens a remote file through the SFTP client of an SSH tab.
    static EditorWindow *openRemote(SftpClient *client, const QString &path, QWidget *parent = nullptr);

    bool save();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    EditorWindow(QString path, SftpClient *client, QWidget *parent);
    void load(const QByteArray &data);
    void loadFailed(const QString &message);
    void saveFinished(const QString &error);
    QByteArray askSudoPassword(const QString &question);
    void showFind(bool replace);
    bool findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void goToLine();
    void updateTitle();
    void updateStatus();

    QString m_path;
    QPointer<SftpClient> m_client;
    bool m_remote;
    quint64 m_pending = 0;  // id of the SFTP read / write in progress
    bool m_closeAfterSave = false;
    bool m_sudo = false;  // protected file: read and saved through sudo
    bool m_loaded = false;
    editor::Decoded m_format;

    CodeEdit *m_edit;
    QWidget *m_findBar;
    QLineEdit *m_find;
    QLineEdit *m_replace;
    QWidget *m_replaceRow;
    QCheckBox *m_case;
    QLabel *m_position;
    QLabel *m_info;
};

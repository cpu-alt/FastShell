// Copyright 2026 FastShell contributors. Apache-2.0.
//
// File browser. Shows the local disk for local tabs and the remote server
// (over SFTP) for SSH tabs, with download, upload (also by drag and drop
// from Explorer), rename, delete and new folder.
#pragma once

#include <QPointer>
#include <QWidget>

#include "net/SftpClient.h"

class QFileSystemModel;
class QLabel;
class QLineEdit;
class QStandardItemModel;
class QToolButton;
class QTreeView;
#include "app/TerminalView.h"

class FilerPanel : public QWidget {
    Q_OBJECT

public:
    explicit FilerPanel(QWidget *parent = nullptr);

    void showLocal(TerminalView *view);
    void showRemote(TerminalView *view, SftpClient *client);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void goTo(const QString &path);
    void goUp();
    void refresh();
    void activate(const QModelIndex &index);
    void showMenu(const QPoint &pos);
    void uploadFiles(const QStringList &localFiles);
    void downloadSelected();
    QString pathOf(const QModelIndex &index) const;
    bool isDirectory(const QModelIndex &index) const;
    void onListed(const QString &path, const QList<RemoteEntry> &entries);

    QPointer<TerminalView> m_view;
    QPointer<SftpClient> m_client;
    QList<QMetaObject::Connection> m_clientConnections;
    bool m_remote = false;
    QString m_remotePath;

    QLineEdit *m_path;
    QToolButton *m_up;
    QToolButton *m_refresh;
    QToolButton *m_home;
    QToolButton *m_upload;
    QToolButton *m_newFolder;
    QTreeView *m_tree;
    QLabel *m_status;
    QFileSystemModel *m_localModel;
    QStandardItemModel *m_remoteModel;
};

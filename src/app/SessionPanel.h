// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Session tree: saved sessions grouped in folders (drag and drop to
// reorganise), plus the shells detected on this machine.
#pragma once

#include <QSet>
#include <QWidget>

#include "app/SessionStore.h"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class SessionPanel : public QWidget {
    Q_OBJECT

public:
    SessionPanel(SessionStore *store, QWidget *parent = nullptr);

    // Opens the editor for a new session; saves and/or connects it.
    void newSession(const QString &folder = QString(), SessionInfo::Type type = SessionInfo::Type::Ssh);

Q_SIGNALS:
    // password is a one-off password typed in the dialog but not saved.
    void openRequested(const SessionInfo &session, const QString &password);
    void openInSplitRequested(const SessionInfo &session, Qt::Orientation orientation);
    void openSideBySideRequested(const QList<SessionInfo> &sessions);

private:
    enum Kind { FolderItem = 1, SessionItem, ShellFolderItem };

    void rebuild();
    void applyFilter();
    void showMenu(const QPoint &pos);
    void editSession(const QString &id);
    void openItem(QTreeWidgetItem *item);
    void moveItems(const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *target);
    QString folderOf(QTreeWidgetItem *item) const;
    QList<SessionInfo> selectedSessions() const;
    QList<SessionInfo> sessionsUnder(const QString &folder) const;
    void exportFolder(const QString &folder);
    void importInto(const QString &folder);

    SessionStore *m_store;
    QLineEdit *m_quick;
    QLineEdit *m_filter;
    QTreeWidget *m_tree;
    QSet<QString> m_collapsed;

    friend class SessionTree;
};

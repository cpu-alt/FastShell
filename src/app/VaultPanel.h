// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Vault panel: saved accounts; a click logs in the current terminal.
#pragma once

#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedLayout;
class SessionStore;

namespace vault {
class Store;
struct Entry;
}  // namespace vault

class VaultPanel : public QWidget {
    Q_OBJECT

public:
    VaultPanel(vault::Store *store, SessionStore *sessions, QWidget *parent = nullptr);
    // The saved session of the current pane: its accounts come first, in bold.
    void setCurrentSession(const QString &sessionId, const QString &folder);

Q_SIGNALS:
    // Type this entry's user / password at the prompt of the current terminal.
    void loginRequested(const QString &id);

private:
    void refresh();
    void addEntry();
    void editEntry(const QString &id);
    bool editDialog(vault::Entry &entry, const QString &title);
    void copyLater(const QString &text);

    QString linksText(const vault::Entry &entry) const;

    vault::Store *m_store;
    SessionStore *m_sessions;
    QString m_currentSession;
    QString m_currentFolder;
    QLineEdit *m_filter;
    QListWidget *m_list;
    QWidget *m_empty;  // the safe, while the vault is empty
    QStackedLayout *m_stack;
};

// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// Side and bottom panels: command outline, file transfers and the
// quick-command bar.
#pragma once

#include <QHash>
#include <QPointer>
#include <QToolBar>
#include <QWidget>

class QLineEdit;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class SftpClient;
#include "app/TerminalView.h"

// Commands typed in the current tab; click one to jump to it.
class OutlinePanel : public QWidget {
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget *parent = nullptr);
    void setView(TerminalView *view);

private:
    void refresh();

    QPointer<TerminalView> m_view;
    QLineEdit *m_filter;
    QListWidget *m_list;
    QMetaObject::Connection m_connection;
};

// Progress of SFTP downloads and uploads.
class TransferPanel : public QWidget {
    Q_OBJECT

public:
    explicit TransferPanel(QWidget *parent = nullptr);
    void attach(SftpClient *client);

Q_SIGNALS:
    void activity();  // a transfer started (lets the window show the panel)

private:
    QTreeWidget *m_list;
    QHash<QString, QTreeWidgetItem *> m_items;  // "client:id"
};

// Buttons that send a saved command to the current tab.
class QuickCommandBar : public QToolBar {
    Q_OBJECT

public:
    explicit QuickCommandBar(QWidget *parent = nullptr);

Q_SIGNALS:
    void commandTriggered(const QString &command);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct Entry {
        QString name;
        QString command;
    };
    void load();
    void save();
    void rebuild();
    bool editEntry(Entry &entry, const QString &title);

    QList<Entry> m_entries;
};

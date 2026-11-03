// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>

#include "app/SessionStore.h"

class FilerPanel;
class OutlinePanel;
class QDockWidget;
class QLabel;
class QTabWidget;
class QuickCommandBar;
class SessionPanel;
class SftpClient;
class TabPage;
class TerminalView;
class TransferPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openSession(const SessionInfo &session, const QString &password = QString());
    void newLocalTab();
    void openSideBySide(const QList<SessionInfo> &sessions);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createDocks();
    void createStatusBar();
    QDockWidget *addPanel(const QString &title, const QString &name, QWidget *widget, Qt::DockWidgetArea area);
    TerminalView *current() const;  // focused pane of the current tab
    TabPage *pageAt(int index) const;
    TabPage *pageOf(TerminalView *view) const;
    QList<TerminalView *> allViews() const;
    TerminalView *createView(const SessionInfo &session, const QString &password);
    void refreshTabTitle(TabPage *page);
    void splitCurrent(Qt::Orientation orientation, const SessionInfo *session = nullptr,
                      const QString &password = QString());
    void closePane();
    void addPage(TabPage *page, const SessionInfo &first);
    void setBroadcastCurrent(bool on);
    void dropSessions(TerminalView *target, const QStringList &ids, Qt::Edge edge);
    void closeTab(int index);
    void showTabMenu(const QPoint &pos);
    void updatePanels();
    void updateStatus();
    SftpClient *sftpFor(TerminalView *view);
    void applyViewOptions(TerminalView *view);

    SessionStore *m_store;
    QTabWidget *m_tabs;
    SessionPanel *m_sessionPanel = nullptr;
    OutlinePanel *m_outline = nullptr;
    FilerPanel *m_filer = nullptr;
    TransferPanel *m_transfers = nullptr;
    QuickCommandBar *m_quickCommands = nullptr;
    QDockWidget *m_filerDock = nullptr;
    QDockWidget *m_transferDock = nullptr;

    QHash<TerminalView *, SessionInfo> m_sessions;
    QHash<TerminalView *, QPointer<SftpClient>> m_sftp;

    QLabel *m_statusMessage = nullptr;
    QLabel *m_statusCursor = nullptr;
    QLabel *m_statusSize = nullptr;
    QLabel *m_statusType = nullptr;
    QLabel *m_statusClock = nullptr;

    QAction *m_timestampsAction = nullptr;
    QAction *m_broadcastAction = nullptr;
    QLabel *m_statusBroadcast = nullptr;
    QAction *m_lineNumbersAction = nullptr;
    bool m_showTimestamps = true;
    bool m_showLineNumbers = true;
};

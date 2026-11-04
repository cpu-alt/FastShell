// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/Panels.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "app/TerminalView.h"
#include "net/SftpClient.h"

namespace {

QString humanSize(qint64 bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024 && unit < 4) {
        value /= 1024;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit ? 1 : 0).arg(QLatin1String(units[unit]));
}

}  // namespace

// --- Outline -------------------------------------------------------------------

OutlinePanel::OutlinePanel(QWidget *parent) : QWidget(parent) {
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter"));
    m_filter->setClearButtonEnabled(true);
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(m_filter);
    layout->addWidget(m_list);

    connect(m_filter, &QLineEdit::textChanged, this, &OutlinePanel::refresh);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (m_view) {
            m_view->scrollToLine(item->data(Qt::UserRole).toLongLong());
            m_view->setFocus();
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (m_view) {
            m_view->sendText(item->text().mid(2) + QLatin1Char('\r'));  // run it again
            m_view->setFocus();
        }
    });
}

void OutlinePanel::setView(TerminalView *view) {
    disconnect(m_connection);
    m_view = view;
    if (view) {
        m_connection = connect(view, &TerminalView::commandsChanged, this, &OutlinePanel::refresh);
    }
    refresh();
}

void OutlinePanel::refresh() {
    m_list->clear();
    if (!m_view) return;
    const QString filter = m_filter->text().trimmed();
    for (const TerminalView::Command &c : m_view->commands()) {
        if (!filter.isEmpty() && !c.text.contains(filter, Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem(QStringLiteral("• ") + c.text, m_list);
        item->setData(Qt::UserRole, qlonglong(c.line));
        item->setToolTip(tr("%1 — click to show, double-click to run again")
                             .arg(c.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
    m_list->scrollToBottom();
}

// --- Transfers -----------------------------------------------------------------

TransferPanel::TransferPanel(QWidget *parent) : QWidget(parent) {
    m_list = new QTreeWidget(this);
    m_list->setColumnCount(4);
    m_list->setHeaderLabels({tr("File"), tr("Progress"), tr("Size"), tr("Status")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_list->header()->resizeSection(1, 260);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);

    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [](QTreeWidgetItem *item) {
        const QString local = item->data(0, Qt::UserRole).toString();
        if (!local.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(local).absolutePath()));
    });
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.addAction(tr("Clear finished"), this, [this] {
            for (int i = m_list->topLevelItemCount() - 1; i >= 0; --i) {
                QTreeWidgetItem *item = m_list->topLevelItem(i);
                if (item->data(1, Qt::UserRole).toBool()) {
                    m_items.remove(m_items.key(item));
                    delete item;
                }
            }
        });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
}

void TransferPanel::attach(SftpClient *client) {
    const QString prefix = QString::number(quintptr(client), 16) + QLatin1Char(':');
    connect(client, &SftpClient::transferStarted, this,
            [this, prefix, client](quint64 id, const QString &name, bool upload, qint64 size) {
                auto *item = new QTreeWidgetItem(m_list);
                item->setText(0, (upload ? QStringLiteral("↑ ") : QStringLiteral("↓ ")) + name);
                item->setToolTip(0, client->host());
                item->setText(2, humanSize(size));
                item->setText(3, upload ? tr("Uploading") : tr("Downloading"));
                auto *bar = new QProgressBar(m_list);
                bar->setRange(0, 1000);
                bar->setTextVisible(true);
                m_list->setItemWidget(item, 1, bar);
                m_items.insert(prefix + QString::number(id), item);
                m_list->scrollToItem(item);
                Q_EMIT activity();
            });
    connect(client, &SftpClient::transferProgress, this, [this, prefix](quint64 id, qint64 done, qint64 total) {
        if (QTreeWidgetItem *item = m_items.value(prefix + QString::number(id))) {
            if (auto *bar = qobject_cast<QProgressBar *>(m_list->itemWidget(item, 1))) {
                bar->setValue(total > 0 ? int(done * 1000 / total) : 0);
                bar->setFormat(QStringLiteral("%1 / %2").arg(humanSize(done), humanSize(total)));
            }
        }
    });
    connect(client, &SftpClient::transferFinished, this, [this, prefix](quint64 id, bool ok, const QString &message) {
        if (QTreeWidgetItem *item = m_items.value(prefix + QString::number(id))) {
            item->setText(3, ok ? tr("Done") : tr("Failed: %1").arg(message));
            item->setForeground(3, ok ? QColor(0x98, 0xC3, 0x79) : QColor(0xE0, 0x6C, 0x75));
            item->setData(1, Qt::UserRole, true);
            if (ok && !item->text(0).startsWith(QStringLiteral("↑"))) {
                item->setData(0, Qt::UserRole, message);  // local path, double-click opens the folder
                item->setToolTip(3, message);
            }
            if (auto *bar = qobject_cast<QProgressBar *>(m_list->itemWidget(item, 1)); bar && ok) {
                bar->setValue(1000);
            }
        }
    });
}

// --- Quick commands --------------------------------------------------------------

QuickCommandBar::QuickCommandBar(QWidget *parent) : QToolBar(tr("Quick commands"), parent) {
    setObjectName(QStringLiteral("quickCommands"));
    setMovable(false);
    setToolButtonStyle(Qt::ToolButtonTextOnly);
    load();
    rebuild();
}

void QuickCommandBar::load() {
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("quickCommands"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        m_entries.append({settings.value(QStringLiteral("name")).toString(),
                          settings.value(QStringLiteral("command")).toString()});
    }
    settings.endArray();
    if (count == 0 && !settings.contains(QStringLiteral("quickCommandsInitialized"))) {
        m_entries = {
            {QStringLiteral("ls"), QStringLiteral("ls -la")},
            {QStringLiteral("df"), QStringLiteral("df -h")},
            {QStringLiteral("free"), QStringLiteral("free -h")},
            {QStringLiteral("top"), QStringLiteral("top")},
            {QStringLiteral("ip"), QStringLiteral("ip -br a")},
            {QStringLiteral("ping"), QStringLiteral("ping -c 4 1.1.1.1")},
            {QStringLiteral("dir"), QStringLiteral("dir")},
            {QStringLiteral("clear"), QStringLiteral("clear")},
        };
        save();
    }
}

void QuickCommandBar::save() {
    QSettings settings;
    settings.setValue(QStringLiteral("quickCommandsInitialized"), true);
    settings.beginWriteArray(QStringLiteral("quickCommands"), int(m_entries.size()));
    for (int i = 0; i < m_entries.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), m_entries[i].name);
        settings.setValue(QStringLiteral("command"), m_entries[i].command);
    }
    settings.endArray();
}

void QuickCommandBar::rebuild() {
    clear();
    auto *label = new QLabel(QStringLiteral("  ") + tr("Commands") + QStringLiteral("  "), this);
    label->setStyleSheet(QStringLiteral("color:#7F848E"));
    addWidget(label);
    for (int i = 0; i < m_entries.size(); ++i) {
        QAction *action = addAction(m_entries[i].name);
        action->setToolTip(m_entries[i].command);
        action->setData(i);
        connect(action, &QAction::triggered, this, [this, i] {
            if (i < m_entries.size()) Q_EMIT commandTriggered(m_entries[i].command);
        });
    }
    QAction *add = addAction(QStringLiteral("+"));
    add->setToolTip(tr("Add a quick command"));
    connect(add, &QAction::triggered, this, [this] {
        Entry entry;
        if (editEntry(entry, tr("New quick command"))) {
            m_entries.append(entry);
            save();
            rebuild();
        }
    });
}

bool QuickCommandBar::editEntry(Entry &entry, const QString &title) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto *name = new QLineEdit(entry.name, &dialog);
    auto *command = new QLineEdit(entry.command, &dialog);
    command->setPlaceholderText(tr("sent to the current tab, followed by Enter"));
    auto *form = new QFormLayout(&dialog);
    form->addRow(tr("Button"), name);
    form->addRow(tr("Command"), command);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(420, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted || command->text().trimmed().isEmpty()) return false;
    entry.command = command->text();
    entry.name = name->text().trimmed().isEmpty() ? entry.command.section(QLatin1Char(' '), 0, 0) : name->text().trimmed();
    return true;
}

void QuickCommandBar::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    QAction *target = actionAt(event->pos());
    const int index = target && target->data().isValid() ? target->data().toInt() : -1;
    if (index >= 0) {
        menu.addAction(tr("Edit..."), this, [this, index] {
            Entry entry = m_entries[index];
            if (editEntry(entry, tr("Edit quick command"))) {
                m_entries[index] = entry;
                save();
                rebuild();
            }
        });
        menu.addAction(tr("Move left"), this, [this, index] {
            if (index > 0) {
                m_entries.swapItemsAt(index, index - 1);
                save();
                rebuild();
            }
        });
        menu.addAction(tr("Remove"), this, [this, index] {
            m_entries.removeAt(index);
            save();
            rebuild();
        });
        menu.addSeparator();
    }
    menu.addAction(tr("Add command..."), this, [this] {
        Entry entry;
        if (editEntry(entry, tr("New quick command"))) {
            m_entries.append(entry);
            save();
            rebuild();
        }
    });
    menu.exec(event->globalPos());
}

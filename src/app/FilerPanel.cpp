// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include "app/FilerPanel.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "app/Icons.h"
#include "app/TerminalView.h"

namespace {

constexpr int PathRole = Qt::UserRole + 1;
constexpr int DirRole = Qt::UserRole + 2;
constexpr int SortRole = Qt::UserRole + 3;

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

QString remoteJoin(const QString &dir, const QString &name) {
    return dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name;
}

QString remoteParent(const QString &path) {
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

QString quoteForShell(const QString &path, bool windows) {
    if (windows) return QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"');
    QString p = path;
    p.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + p + QLatin1Char('\'');
}

QToolButton *toolButton(QWidget *parent, QStyle::StandardPixmap icon, const QString &tip) {
    auto *b = new QToolButton(parent);
    b->setIcon(parent->style()->standardIcon(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

}  // namespace

FilerPanel::FilerPanel(QWidget *parent) : QWidget(parent) {
    m_path = new QLineEdit(this);
    m_up = toolButton(this, QStyle::SP_FileDialogToParent, tr("Parent folder"));
    m_refresh = toolButton(this, QStyle::SP_BrowserReload, tr("Refresh"));
    m_home = toolButton(this, QStyle::SP_DirHomeIcon, tr("Home folder"));
    m_upload = toolButton(this, QStyle::SP_ArrowUp, tr("Upload files..."));
    m_newFolder = toolButton(this, QStyle::SP_FileDialogNewFolder, tr("New folder"));

    m_tree = new QTreeView(this);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAcceptDrops(true);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->viewport()->installEventFilter(this);
    m_tree->setIconSize(QSize(16, 16));

    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color:#7F848E; padding: 0 4px"));

    m_localModel = new QFileSystemModel(this);
    m_localModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    m_localModel->setReadOnly(true);
    m_remoteModel = new QStandardItemModel(this);
    m_remoteModel->setSortRole(SortRole);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(2);
    bar->addWidget(m_path, 1);
    bar->addWidget(m_up);
    bar->addWidget(m_home);
    bar->addWidget(m_refresh);
    bar->addWidget(m_newFolder);
    bar->addWidget(m_upload);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(bar);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_status);

    connect(m_path, &QLineEdit::returnPressed, this, [this] { goTo(m_path->text().trimmed()); });
    connect(m_up, &QToolButton::clicked, this, &FilerPanel::goUp);
    connect(m_refresh, &QToolButton::clicked, this, &FilerPanel::refresh);
    connect(m_home, &QToolButton::clicked, this, [this] {
        if (m_remote && m_client) m_client->open();
        else goTo(QDir::homePath());
    });
    connect(m_upload, &QToolButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Upload files"));
        if (!files.isEmpty()) uploadFiles(files);
    });
    connect(m_newFolder, &QToolButton::clicked, this, [this] {
        const QString name = QInputDialog::getText(this, tr("New folder"), tr("Name:"));
        if (name.trimmed().isEmpty()) return;
        if (m_remote && m_client) {
            m_client->makeDirectory(remoteJoin(m_remotePath, name.trimmed()));
        } else {
            QDir(m_localModel->rootPath()).mkdir(name.trimmed());
        }
    });
    connect(m_tree, &QTreeView::activated, this, &FilerPanel::activate);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &FilerPanel::showMenu);
    connect(m_localModel, &QFileSystemModel::directoryLoaded, this, [this] {
        const QModelIndex root = m_tree->rootIndex();
        m_status->setText(tr("%n item(s)", nullptr, m_localModel->rowCount(root)));
    });

    showLocal(nullptr);
}

void FilerPanel::showLocal(TerminalView *view) {
    m_view = view;
    for (const auto &c : m_clientConnections) disconnect(c);
    m_clientConnections.clear();
    m_client = nullptr;
    if (!m_remote && m_tree->model() == m_localModel) return;  // keep the current folder
    m_remote = false;
    m_upload->setVisible(false);
    m_tree->setModel(m_localModel);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->setColumnHidden(2, true);  // type column
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    goTo(QDir::homePath());
}

void FilerPanel::showRemote(TerminalView *view, SftpClient *client) {
    m_view = view;
    if (m_remote && m_client == client) return;
    for (const auto &c : m_clientConnections) disconnect(c);
    m_clientConnections.clear();
    m_client = client;
    m_remote = true;
    m_upload->setVisible(true);
    m_remoteModel->clear();
    m_remoteModel->setHorizontalHeaderLabels({tr("Name"), tr("Size"), tr("Modified")});
    m_tree->setModel(m_remoteModel);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_status->setText(tr("Connecting to %1...").arg(client->host()));
    m_path->clear();

    m_clientConnections << connect(client, &SftpClient::opened, this, [this](const QString &home) { goTo(home); });
    m_clientConnections << connect(client, &SftpClient::listed, this, &FilerPanel::onListed);
    m_clientConnections << connect(client, &SftpClient::failed, this, [this](const QString &message) {
        m_status->setText(message);
        m_status->setToolTip(message);
    });
    m_clientConnections << connect(client, &SftpClient::changed, this, [this](const QString &dir) {
        if (dir == m_remotePath) refresh();
    });
    if (client->property("wtng_path").toString().isEmpty()) {
        client->open();
    } else {
        goTo(client->property("wtng_path").toString());
    }
}

void FilerPanel::onListed(const QString &path, const QList<RemoteEntry> &entries) {
    if (path != m_remotePath) return;
    m_remoteModel->removeRows(0, m_remoteModel->rowCount());
    for (const RemoteEntry &e : entries) {
        auto *name = new QStandardItem(e.isDirectory ? icons::folder() : icons::file(), e.name);
        name->setData(remoteJoin(path, e.name), PathRole);
        name->setData(e.isDirectory, DirRole);
        // Folders first, then by name.
        name->setData(QString((e.isDirectory ? QStringLiteral("0") : QStringLiteral("1")) + e.name.toLower()), SortRole);
        auto *size = new QStandardItem(e.isDirectory ? QString() : humanSize(e.size));
        size->setData(e.size, SortRole);
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *modified = new QStandardItem(e.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        modified->setData(e.modified, SortRole);
        for (QStandardItem *item : {name, size, modified}) item->setEditable(false);
        m_remoteModel->appendRow({name, size, modified});
    }
    m_tree->sortByColumn(m_tree->header()->sortIndicatorSection(), m_tree->header()->sortIndicatorOrder());
    m_status->setText(tr("%n item(s)", nullptr, int(entries.size())) + QStringLiteral(" — ") + m_client->host());
    m_status->setToolTip(QString());
}

void FilerPanel::goTo(const QString &path) {
    if (path.isEmpty()) return;
    if (m_remote) {
        if (!m_client) return;
        m_remotePath = path;
        m_client->setProperty("wtng_path", path);
        m_path->setText(path);
        m_status->setText(tr("Loading..."));
        m_client->list(path);
    } else {
        if (!QFileInfo(path).isDir()) return;
        const QString clean = QDir::cleanPath(path);
        m_tree->setRootIndex(m_localModel->setRootPath(clean));
        m_path->setText(QDir::toNativeSeparators(clean));
        m_status->setText(tr("%n item(s)", nullptr, m_localModel->rowCount(m_tree->rootIndex())));
    }
}

void FilerPanel::goUp() {
    if (m_remote) {
        goTo(remoteParent(m_remotePath));
    } else {
        QDir dir(m_localModel->rootPath());
        if (dir.cdUp()) goTo(dir.absolutePath());
    }
}

void FilerPanel::refresh() {
    if (m_remote) {
        goTo(m_remotePath);
    } else {
        // QFileSystemModel watches the folder; re-setting the root forces a re-read.
        const QString root = m_localModel->rootPath();
        m_localModel->setRootPath(QString());
        goTo(root);
    }
}

QString FilerPanel::pathOf(const QModelIndex &index) const {
    const QModelIndex first = index.siblingAtColumn(0);
    return m_remote ? first.data(PathRole).toString() : m_localModel->filePath(first);
}

bool FilerPanel::isDirectory(const QModelIndex &index) const {
    const QModelIndex first = index.siblingAtColumn(0);
    return m_remote ? first.data(DirRole).toBool() : m_localModel->isDir(first);
}

void FilerPanel::activate(const QModelIndex &index) {
    if (!index.isValid()) return;
    if (isDirectory(index)) {
        goTo(pathOf(index));
    } else if (m_remote) {
        downloadSelected();
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(pathOf(index)));
    }
}

void FilerPanel::downloadSelected() {
    if (!m_client) return;
    const QString target = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    for (const QModelIndex &index : m_tree->selectionModel()->selectedRows(0)) {
        if (isDirectory(index)) continue;
        const QString remote = pathOf(index);
        m_client->download(remote, QDir(target).filePath(QFileInfo(remote).fileName()));
    }
}

void FilerPanel::uploadFiles(const QStringList &localFiles) {
    if (!m_remote || !m_client) return;
    for (const QString &file : localFiles) {
        if (QFileInfo(file).isFile()) {
            m_client->upload(file, remoteJoin(m_remotePath, QFileInfo(file).fileName()));
        }
    }
}

bool FilerPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_tree->viewport() && m_remote) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *drag = static_cast<QDropEvent *>(event);
            if (drag->mimeData()->hasUrls()) {
                drag->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
            QStringList files;
            for (const QUrl &url : drop->mimeData()->urls()) {
                if (url.isLocalFile()) files << url.toLocalFile();
            }
            uploadFiles(files);
            drop->acceptProposedAction();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FilerPanel::showMenu(const QPoint &pos) {
    const QModelIndex index = m_tree->indexAt(pos);
    const QString path = index.isValid() ? pathOf(index) : (m_remote ? m_remotePath : m_localModel->rootPath());
    const bool directory = !index.isValid() || isDirectory(index);
    QMenu menu(this);

    if (index.isValid()) {
        menu.addAction(directory ? tr("Open") : (m_remote ? tr("Download") : tr("Open")), this,
                       [this, index] { activate(index); });
    }
    if (m_view) {
        const QString dir = directory ? path : (m_remote ? remoteParent(path) : QFileInfo(path).absolutePath());
        menu.addAction(tr("cd here in the terminal"), this, [this, dir] {
#ifdef _WIN32
            const bool windows = !m_remote;
#else
            const bool windows = false;
#endif
            m_view->sendText(QStringLiteral("cd ") + quoteForShell(dir, windows) + QLatin1Char('\r'));
            m_view->setFocus();
        });
    }
    menu.addAction(tr("Copy path"), this, [path, this] {
        QApplication::clipboard()->setText(m_remote ? path : QDir::toNativeSeparators(path));
    });
    if (!m_remote && index.isValid()) {
        menu.addAction(tr("Show in Explorer"), this, [path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        });
    }
    if (m_remote) {
        menu.addSeparator();
        menu.addAction(tr("Upload files here..."), this, [this] {
            const QStringList files = QFileDialog::getOpenFileNames(this, tr("Upload files"));
            if (!files.isEmpty()) uploadFiles(files);
        });
        menu.addAction(tr("New folder..."), m_newFolder, &QToolButton::click);
        if (index.isValid()) {
            menu.addAction(tr("Rename..."), this, [this, path] {
                const QString old = path.section(QLatin1Char('/'), -1);
                const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, old);
                if (!name.trimmed().isEmpty() && name != old) {
                    m_client->rename(path, remoteJoin(remoteParent(path), name.trimmed()));
                }
            });
            menu.addAction(tr("Delete"), this, [this, path, directory] {
                if (QMessageBox::question(this, tr("Delete"), tr("Delete %1 on the server?").arg(path))
                    == QMessageBox::Yes) {
                    m_client->remove(path, directory);
                }
            });
        }
    }
    menu.addSeparator();
    menu.addAction(tr("Refresh"), this, &FilerPanel::refresh);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

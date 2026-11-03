// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include "app/SessionPanel.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDropEvent>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QTimer>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "app/Icons.h"
#include "app/ManageDialogs.h"
#include "app/SessionDialog.h"
#include "app/TerminalView.h"

namespace {
constexpr int KindRole = Qt::UserRole;
constexpr int KeyRole = Qt::UserRole + 1;  // folder path or session id
constexpr int TagRole = Qt::UserRole + 2;    // device tag text ("CISCO")
constexpr int TagColorRole = Qt::UserRole + 3;
const QString ShellFolderKey = QStringLiteral("\x01shells");
}  // namespace

// Draws the device tag as a small colored pill after the session name.
class TagDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        const QString tag = index.data(TagRole).toString();
        if (tag.isEmpty()) return;

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QRect textRect = opt.widget->style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        QFont font = opt.font;
        font.setPointSizeF(std::max(6.5, font.pointSizeF() - 1.5));
        font.setBold(true);
        const QFontMetrics fm(font);
        const int textWidth = opt.fontMetrics.horizontalAdvance(opt.text);
        const int w = fm.horizontalAdvance(tag) + 10;
        const int h = fm.height() + 2;
        int x = textRect.left() + textWidth + 8;
        if (x + w > textRect.right()) x = std::max(textRect.left(), textRect.right() - w);
        const QRect pill(x, textRect.center().y() - h / 2 + 1, w, h);

        QColor color = index.data(TagColorRole).value<QColor>();
        if (!color.isValid()) color = QColor(0x5C, 0x63, 0x70);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRoundedRect(pill, h / 2.0, h / 2.0);
        painter->setFont(font);
        painter->setPen(color.lightness() > 150 ? QColor(0x10, 0x14, 0x1A) : Qt::white);
        painter->drawText(pill, Qt::AlignCenter, tag);
        painter->restore();
    }
};

// QTreeWidget whose drops update the store instead of moving items itself.
class SessionTree : public QTreeWidget {
public:
    SessionTree(SessionPanel *panel) : QTreeWidget(panel), m_panel(panel) {}

protected:
    // Dragging sessions out of the tree carries their ids, so they can be
    // dropped on a terminal pane to open them next to it.
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override {
        QMimeData *data = QTreeWidget::mimeData(items);
        QStringList ids;
        for (QTreeWidgetItem *item : items) {
            if (item->data(0, KindRole).toInt() == SessionPanel::SessionItem) ids << item->data(0, KeyRole).toString();
        }
        if (data && !ids.isEmpty()) data->setData(QLatin1String(SessionMimeType), ids.join(QLatin1Char('\n')).toUtf8());
        return data;
    }

    void dropEvent(QDropEvent *event) override {
        QTreeWidgetItem *target = itemAt(event->position().toPoint());
        const QList<QTreeWidgetItem *> items = selectedItems();
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        m_panel->moveItems(items, target);
    }

private:
    SessionPanel *m_panel;
};

SessionPanel::SessionPanel(SessionStore *store, QWidget *parent) : QWidget(parent), m_store(store) {
    m_quick = new QLineEdit(this);
    m_quick->setPlaceholderText(tr("Quick connect: user@host, host:2222, telnet host, COM3 115200"));
    m_quick->setClearButtonEnabled(true);
    m_quick->setToolTip(tr("Press Enter to connect without saving a session"));
    connect(m_quick, &QLineEdit::returnPressed, this, [this] {
        if (auto s = SessionStore::parseQuickConnect(m_quick->text())) {
            Q_EMIT openRequested(*s, QString());
            m_quick->clear();
        } else {
            m_quick->setStyleSheet(QStringLiteral("QLineEdit { border: 1px solid #E06C75; }"));
            QTimer::singleShot(1200, m_quick, [q = m_quick] { q->setStyleSheet(QString()); });
        }
    });

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter"));
    m_filter->setClearButtonEnabled(true);

    m_tree = new SessionTree(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setAnimated(true);
    m_tree->setIndentation(14);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setItemDelegate(new TagDelegate(m_tree));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(m_quick);
    layout->addWidget(m_filter);
    layout->addWidget(m_tree);

    connect(m_filter, &QLineEdit::textChanged, this, &SessionPanel::applyFilter);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) { openItem(item); });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SessionPanel::showMenu);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        m_collapsed.insert(item->data(0, KeyRole).toString());
        if (item->data(0, KindRole).toInt() != SessionItem) item->setIcon(0, icons::folder(false, m_store->folderColor(item->data(0, KeyRole).toString())));
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        m_collapsed.remove(item->data(0, KeyRole).toString());
        if (item->data(0, KindRole).toInt() != SessionItem) item->setIcon(0, icons::folder(true, m_store->folderColor(item->data(0, KeyRole).toString())));
    });
    connect(m_store, &SessionStore::changed, this, &SessionPanel::rebuild);
    rebuild();
}

void SessionPanel::rebuild() {
    m_tree->clear();
    QMap<QString, QTreeWidgetItem *> folders;

    std::function<QTreeWidgetItem *(const QString &)> folderItem = [&](const QString &path) -> QTreeWidgetItem * {
        if (path.isEmpty()) return nullptr;
        if (QTreeWidgetItem *existing = folders.value(path)) return existing;
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem *parent = folderItem(slash > 0 ? path.left(slash) : QString());
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, path.mid(slash + 1));
        item->setData(0, KindRole, FolderItem);
        item->setData(0, KeyRole, path);
        item->setFlags(item->flags() | Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled);
        folders.insert(path, item);
        return item;
    };

    QStringList sortedFolders = m_store->folders();
    sortedFolders.sort(Qt::CaseInsensitive);
    for (const QString &f : sortedFolders) folderItem(f);

    QList<SessionInfo> sessions = m_store->sessions();
    std::sort(sessions.begin(), sessions.end(), [](const SessionInfo &a, const SessionInfo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    for (const SessionInfo &s : sessions) {
        QTreeWidgetItem *parent = folderItem(s.folder);
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        const DeviceProfile *profile = m_store->findProfile(s.profileId);
        const Credential *credential = m_store->findCredential(s.credentialId);
        SessionInfo shown = s;
        if (!shown.color.isValid() && profile) shown.color = profile->color;  // device color by default
        const QString user = credential ? credential->user : s.user;
        QString tip = s.type == SessionInfo::Type::Local    ? s.program
                    : s.type == SessionInfo::Type::Serial ? QStringLiteral("Serial %1 @ %2 baud").arg(s.serialPort).arg(s.baudRate)
                        : QStringLiteral("%1 %2%3:%4").arg(s.typeName(), user.isEmpty() ? QString() : user + QLatin1Char('@'),
                                                           s.host, QString::number(s.port));
        if (credential) tip += QLatin1Char('\n') + tr("User: %1").arg(credential->name);
        if (profile) tip += QStringLiteral("\n[%1]").arg(profile->tag);
        item->setText(0, s.name);
        item->setIcon(0, icons::session(shown));
        item->setData(0, KindRole, SessionItem);
        item->setData(0, KeyRole, s.id);
        if (profile) {
            item->setData(0, TagRole, profile->tag);
            item->setData(0, TagColorRole, profile->color);
        }
        item->setToolTip(0, tip);
        item->setFlags((item->flags() | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
    }

    // Detected shells, read-only.
    auto *shells = new QTreeWidgetItem(m_tree);
    shells->setText(0, tr("Shell sessions"));
    shells->setData(0, KindRole, ShellFolderItem);
    shells->setData(0, KeyRole, ShellFolderKey);
    shells->setFlags(shells->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
    for (const SessionInfo &s : m_store->detectedShells()) {
        auto *item = new QTreeWidgetItem(shells);
        item->setText(0, s.name);
        item->setIcon(0, icons::session(s));
        item->setToolTip(0, s.program + QLatin1Char(' ') + s.arguments.join(QLatin1Char(' ')));
        item->setData(0, KindRole, SessionItem);
        item->setData(0, KeyRole, s.id);
        item->setFlags(item->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
    }

    // Restore the expanded state.
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem *item = *it;
        if (item->data(0, KindRole).toInt() != SessionItem) {
            const bool expanded = !m_collapsed.contains(item->data(0, KeyRole).toString());
            item->setExpanded(expanded);
            item->setIcon(0, icons::folder(expanded, m_store->folderColor(item->data(0, KeyRole).toString())));
        }
    }
    applyFilter();
}

void SessionPanel::applyFilter() {
    const QString text = m_filter->text().trimmed();
    std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) -> bool {
        bool any = false;
        for (int i = 0; i < item->childCount(); ++i) {
            any |= visit(item->child(i));
        }
        bool match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive)
                  || item->toolTip(0).contains(text, Qt::CaseInsensitive);
        if (item->data(0, KindRole).toInt() != SessionItem) {
            match = text.isEmpty() || any;
            if (!text.isEmpty() && any) item->setExpanded(true);
        }
        item->setHidden(!match);
        return match;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        visit(m_tree->topLevelItem(i));
    }
}

QString SessionPanel::folderOf(QTreeWidgetItem *item) const {
    if (!item) return {};
    switch (item->data(0, KindRole).toInt()) {
    case FolderItem: return item->data(0, KeyRole).toString();
    case ShellFolderItem: return {};
    default: return item->parent() ? folderOf(item->parent()) : QString();
    }
}

void SessionPanel::moveItems(const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *target) {
    if (target && target->data(0, KindRole).toInt() == ShellFolderItem) return;
    if (target && target->parent() && target->parent()->data(0, KindRole).toInt() == ShellFolderItem) return;
    const QString destination = folderOf(target);

    for (QTreeWidgetItem *item : items) {
        const int kind = item->data(0, KindRole).toInt();
        const QString key = item->data(0, KeyRole).toString();
        if (kind == SessionItem && !key.startsWith(QLatin1String("builtin:"))) {
            m_store->moveToFolder(key, destination);
        } else if (kind == FolderItem) {
            if (destination == key || destination.startsWith(key + QLatin1Char('/'))) continue;  // into itself
            const QString name = key.section(QLatin1Char('/'), -1);
            m_store->renameFolder(key, destination.isEmpty() ? name : destination + QLatin1Char('/') + name);
        }
    }
}

void SessionPanel::openItem(QTreeWidgetItem *item) {
    if (!item || item->data(0, KindRole).toInt() != SessionItem) return;
    if (const SessionInfo *s = m_store->find(item->data(0, KeyRole).toString())) {
        Q_EMIT openRequested(*s, QString());
    }
}

void SessionPanel::newSession(const QString &folder) {
    SessionDialog dialog(m_store, this);
    SessionInfo seed;
    seed.folder = folder;
    seed.type = SessionInfo::Type::Ssh;
    dialog.setWindowTitle(tr("New session"));
    // setSession() would hide the "save" checkbox; only pre-fill the folder.
    if (!folder.isEmpty()) {
        dialog.setSession(seed);
        dialog.setWindowTitle(tr("New session"));
    }
    if (dialog.exec() != QDialog::Accepted) return;
    SessionInfo s = dialog.session();
    s.id.clear();
    if (dialog.saveRequested() || !folder.isEmpty()) {
        m_store->upsert(s);
        s = m_store->sessions().last();
    }
    if (s.type == SessionInfo::Type::Local || !s.host.isEmpty()) {
        Q_EMIT openRequested(s, dialog.typedPassword());
    }
}

void SessionPanel::editSession(const QString &id) {
    const SessionInfo *existing = m_store->find(id);
    if (!existing || existing->builtin) return;
    SessionDialog dialog(m_store, this);
    dialog.setSession(*existing);
    if (dialog.exec() == QDialog::Accepted) {
        m_store->upsert(dialog.session());
    }
}

QList<SessionInfo> SessionPanel::selectedSessions() const {
    QList<SessionInfo> result;
    for (QTreeWidgetItem *item : m_tree->selectedItems()) {
        if (item->data(0, KindRole).toInt() != SessionItem) continue;
        if (const SessionInfo *s = m_store->find(item->data(0, KeyRole).toString())) result << *s;
    }
    return result;
}

QList<SessionInfo> SessionPanel::sessionsUnder(const QString &folder) const {
    QList<SessionInfo> result;
    for (const SessionInfo &s : m_store->sessions()) {
        if (s.folder == folder || s.folder.startsWith(folder + QLatin1Char('/'))) result << s;
    }
    std::sort(result.begin(), result.end(), [](const SessionInfo &a, const SessionInfo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

void SessionPanel::exportFolder(const QString &folder) {
    QDialog dialog(this);
    dialog.setWindowTitle(folder.isEmpty() ? tr("Export all sessions") : tr("Export \"%1\"").arg(folder));
    auto *namesOnly = new QRadioButton(tr("Names and addresses only (IP / DNS), no user"), &dialog);
    auto *users = new QRadioButton(tr("Also users and key paths, without passwords"), &dialog);
    auto *passwords = new QRadioButton(tr("Also passwords, protected by an export password"), &dialog);
    namesOnly->setChecked(true);
    auto *pass1 = new QLineEdit(&dialog);
    auto *pass2 = new QLineEdit(&dialog);
    for (QLineEdit *e : {pass1, pass2}) e->setEchoMode(QLineEdit::Password);
    pass1->setPlaceholderText(tr("export password"));
    pass2->setPlaceholderText(tr("repeat it"));
    auto *hint = new QLabel(tr("Passwords are encrypted with AES-256-GCM. Without this password the file "
                               "cannot reveal them; keep it somewhere safe."), &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#7F848E"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export..."));
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("<b>What to include</b>"), &dialog));
    for (QWidget *w : {static_cast<QWidget *>(namesOnly), static_cast<QWidget *>(users),
                       static_cast<QWidget *>(passwords), static_cast<QWidget *>(pass1),
                       static_cast<QWidget *>(pass2), static_cast<QWidget *>(hint)}) {
        layout->addWidget(w);
    }
    layout->addWidget(buttons);
    auto update = [&] {
        for (QWidget *w : {static_cast<QWidget *>(pass1), static_cast<QWidget *>(pass2), static_cast<QWidget *>(hint)}) {
            w->setVisible(passwords->isChecked());
        }
        dialog.adjustSize();
    };
    connect(passwords, &QRadioButton::toggled, &dialog, update);
    update();
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (passwords->isChecked() && (pass1->text().isEmpty() || pass1->text() != pass2->text())) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), tr("The two export passwords are empty or different."));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;

    const auto logins = namesOnly->isChecked() ? SessionStore::ExportLogins::None
                      : users->isChecked()     ? SessionStore::ExportLogins::UsersOnly
                                               : SessionStore::ExportLogins::UsersAndPasswords;
    const QString suggested = QDir::home().filePath(
        (folder.isEmpty() ? QStringLiteral("sessions") : folder.section(QLatin1Char('/'), -1)) + QStringLiteral(".emfhexa"));
    const QString file = QFileDialog::getSaveFileName(this, dialog.windowTitle(), suggested,
                                                      tr("EMF Hexa sessions (*.emfhexa);;JSON (*.json)"));
    if (file.isEmpty()) return;
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, dialog.windowTitle(), out.errorString());
        return;
    }
    out.write(m_store->exportSessions(folder, logins, pass1->text()));
    if (!out.commit()) {
        QMessageBox::warning(this, dialog.windowTitle(), out.errorString());
        return;
    }
    QMessageBox::information(this, dialog.windowTitle(),
                             tr("%n session(s) exported to %1.", nullptr, int(sessionsUnder(folder).size()))
                                 .arg(QDir::toNativeSeparators(file)));
}

void SessionPanel::importInto(const QString &folder) {
    const QString file = QFileDialog::getOpenFileName(this, tr("Import sessions"), QDir::homePath(),
                                                      tr("EMF Hexa sessions (*.emfhexa *.json);;All files (*)"));
    if (file.isEmpty()) return;
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import sessions"), in.errorString());
        return;
    }
    const QByteArray data = in.readAll();
    SessionStore::ImportResult result = m_store->importSessions(data, folder);
    while (result.needsPassphrase || result.error == tr("Wrong export password.")) {
        bool ok = false;
        const QString pass = QInputDialog::getText(
            this, tr("Import sessions"),
            result.needsPassphrase ? tr("This file contains passwords. Export password:")
                                   : tr("Wrong password, try again:"),
            QLineEdit::Password, QString(), &ok);
        if (!ok) {
            // Import without the passwords is still useful.
            if (QMessageBox::question(this, tr("Import sessions"), tr("Import without the passwords?"))
                != QMessageBox::Yes) {
                return;
            }
            QJsonObject stripped = QJsonDocument::fromJson(data).object();
            stripped.remove(QStringLiteral("secrets"));
            result = m_store->importSessions(QJsonDocument(stripped).toJson(), folder);
            break;
        }
        result = m_store->importSessions(data, folder, pass);
    }
    if (!result.error.isEmpty()) {
        QMessageBox::warning(this, tr("Import sessions"), result.error);
        return;
    }
    QMessageBox::information(this, tr("Import sessions"),
                             tr("Imported %1 session(s) and %2 user(s)%3.")
                                 .arg(result.sessions)
                                 .arg(result.credentials)
                                 .arg(folder.isEmpty() ? QString() : tr(" into \"%1\"").arg(folder)));
}

void SessionPanel::showMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (item && !item->isSelected()) {
        m_tree->setCurrentItem(item);  // right-click outside the selection selects that item
    }
    const int kind = item ? item->data(0, KindRole).toInt() : 0;
    const QString key = item ? item->data(0, KeyRole).toString() : QString();
    const bool builtin = key.startsWith(QLatin1String("builtin:"));
    const QList<SessionInfo> selection = selectedSessions();
    QMenu menu(this);

    if (kind == SessionItem && selection.size() > 1) {
        // Several sessions selected: open them together.
        menu.addAction(tr("Open %n sessions side by side", nullptr, int(selection.size())), this,
                       [this, selection] { Q_EMIT openSideBySideRequested(selection); });
        menu.addAction(tr("Open %n sessions in tabs", nullptr, int(selection.size())), this, [this, selection] {
            for (const SessionInfo &s : selection) Q_EMIT openRequested(s, QString());
        });
        menu.addSeparator();
        QMenu *tagMenu = menu.addMenu(tr("Tag"));
        auto setTag = [this, selection](const QString &profileId) {
            for (SessionInfo s : selection) {
                if (s.builtin) continue;
                s.profileId = profileId;
                m_store->upsert(s);
            }
        };
        tagMenu->addAction(tr("None"), this, [setTag] { setTag(QString()); });
        for (const DeviceProfile &p : m_store->profiles()) {
            QPixmap dot(12, 12);
            dot.fill(p.color.isValid() ? p.color : Qt::gray);
            tagMenu->addAction(QIcon(dot), p.tag + QStringLiteral("  ") + p.name, this, [setTag, id = p.id] { setTag(id); });
        }
    } else if (kind == SessionItem) {
        menu.addAction(tr("Open"), this, [this, item] { openItem(item); });
        menu.addAction(tr("Open in split right"), this, [this, key] {
            if (const SessionInfo *s = m_store->find(key)) Q_EMIT openInSplitRequested(*s, Qt::Horizontal);
        });
        menu.addAction(tr("Open in split down"), this, [this, key] {
            if (const SessionInfo *s = m_store->find(key)) Q_EMIT openInSplitRequested(*s, Qt::Vertical);
        });
        menu.addSeparator();
        if (builtin) {
            menu.addAction(tr("Copy to my sessions"), this, [this, key] {
                SessionInfo s = *m_store->find(key);
                s.id.clear();
                s.builtin = false;
                s.folder = tr("Shells");
                m_store->upsert(s);
            });
        } else {
            menu.addAction(tr("Edit..."), this, [this, key] { editSession(key); });
            menu.addAction(tr("Duplicate"), this, [this, key] {
                SessionInfo s = *m_store->find(key);
                s.id.clear();
                s.name += tr(" (copy)");
                m_store->upsert(s);
            });
            menu.addAction(tr("Icon color..."), this, [this, key] {
                SessionInfo s = *m_store->find(key);
                const QColor c = QColorDialog::getColor(s.color.isValid() ? s.color : QColor(0x1E, 0x88, 0xE5), this,
                                                        tr("Icon and tab color"));
                if (c.isValid()) {
                    s.color = c;
                    m_store->upsert(s);
                }
            });
            if (m_store->find(key)->color.isValid()) {
                menu.addAction(tr("Default icon color"), this, [this, key] {
                    SessionInfo s = *m_store->find(key);
                    s.color = QColor();
                    m_store->upsert(s);
                });
            }
            QMenu *tagMenu = menu.addMenu(tr("Tag"));
            const QString current = m_store->find(key)->profileId;
            auto setTag = [this, key](const QString &profileId) {
                SessionInfo s = *m_store->find(key);
                s.profileId = profileId;
                m_store->upsert(s);
            };
            QAction *none = tagMenu->addAction(tr("None"), this, [setTag] { setTag(QString()); });
            none->setCheckable(true);
            none->setChecked(current.isEmpty());
            for (const DeviceProfile &p : m_store->profiles()) {
                QPixmap dot(12, 12);
                dot.fill(p.color.isValid() ? p.color : Qt::gray);
                QAction *a = tagMenu->addAction(QIcon(dot), p.tag + QStringLiteral("  ") + p.name, this,
                                                [setTag, id = p.id] { setTag(id); });
                a->setCheckable(true);
                a->setChecked(p.id == current);
            }
            tagMenu->addSeparator();
            tagMenu->addAction(tr("New tag..."), this, [this] { ProfilesDialog(m_store, this).exec(); });
            menu.addSeparator();
            menu.addAction(tr("Delete"), this, [this, key, item] {
                if (QMessageBox::question(this, tr("Delete session"), tr("Delete \"%1\"?").arg(item->text(0)))
                    == QMessageBox::Yes) {
                    m_store->remove(key);
                }
            });
        }
    } else if (kind == FolderItem) {
        const QList<SessionInfo> inside = sessionsUnder(key);
        if (!inside.isEmpty()) {
            menu.addAction(tr("Open all %n side by side", nullptr, int(inside.size())), this,
                           [this, inside] { Q_EMIT openSideBySideRequested(inside); });
            menu.addAction(tr("Open all %n in tabs", nullptr, int(inside.size())), this, [this, inside] {
                for (const SessionInfo &s : inside) Q_EMIT openRequested(s, QString());
            });
            menu.addSeparator();
        }
        menu.addAction(tr("New session here..."), this, [this, key] { newSession(key); });
        menu.addAction(tr("New subfolder..."), this, [this, key] {
            const QString name = QInputDialog::getText(this, tr("New folder"), tr("Name:"));
            if (!name.trimmed().isEmpty()) m_store->addFolder(key + QLatin1Char('/') + name);
        });
        menu.addAction(tr("Rename..."), this, [this, key] {
            const QString old = key.section(QLatin1Char('/'), -1);
            const QString name = QInputDialog::getText(this, tr("Rename folder"), tr("Name:"), QLineEdit::Normal, old);
            if (!name.trimmed().isEmpty() && name != old) {
                const QString parent = key.section(QLatin1Char('/'), 0, -2);
                m_store->renameFolder(key, parent.isEmpty() ? name : parent + QLatin1Char('/') + name);
            }
        });
        menu.addAction(tr("Folder color..."), this, [this, key] {
            const QColor current = m_store->folderColor(key);
            const QColor c = QColorDialog::getColor(current.isValid() ? current : QColor(0xE5, 0xB5, 0x4B), this,
                                                    tr("Folder color"));
            if (c.isValid()) m_store->setFolderColor(key, c);
        });
        if (m_store->folderColor(key).isValid()) {
            menu.addAction(tr("Default folder color"), this, [this, key] { m_store->setFolderColor(key, QColor()); });
        }
        menu.addSeparator();
        menu.addAction(tr("Export this folder..."), this, [this, key] { exportFolder(key); });
        menu.addAction(tr("Import into this folder..."), this, [this, key] { importInto(key); });
        menu.addSeparator();
        menu.addAction(tr("Delete folder"), this, [this, key] {
            if (QMessageBox::question(this, tr("Delete folder"),
                                      tr("Delete \"%1\" and all the sessions inside?").arg(key)) == QMessageBox::Yes) {
                m_store->removeFolder(key);
            }
        });
    }

    // Empty area or the read-only "Shell sessions" folder: generic actions.
    // (A real folder already offers "New session here" and "New subfolder".)
    if (kind != SessionItem && kind != FolderItem) {
        if (!menu.isEmpty()) menu.addSeparator();
        menu.addAction(tr("New session..."), this, [this] { newSession(); });
        menu.addAction(tr("New folder..."), this, [this] {
            const QString name = QInputDialog::getText(this, tr("New folder"), tr("Name:"));
            if (!name.trimmed().isEmpty()) m_store->addFolder(name);
        });
        if (kind == 0) {
            menu.addSeparator();
            menu.addAction(tr("Export all sessions..."), this, [this] { exportFolder(QString()); });
            menu.addAction(tr("Import sessions..."), this, [this] { importInto(QString()); });
        }
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/VaultPanel.h"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "app/Fun.h"
#include "app/SessionStore.h"
#include "app/Style.h"
#include "app/Vault.h"

namespace {
constexpr int IdRole = Qt::UserRole;
constexpr int ClipboardClearMs = 30000;

// Shown instead of the list while the vault is empty: a safe whose dial
// turns under the mouse. A click adds an account.
class EmptyVault : public QWidget {
public:
    explicit EmptyVault(std::function<void()> add, QWidget *parent) : QWidget(parent), m_add(std::move(add)) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QObject::tr("Add an account"));
        m_spin.setStartValue(0.0);
        m_spin.setEndValue(1.0);
        m_spin.setDuration(1100);
        m_spin.setEasingCurve(QEasingCurve::OutBack);
        QObject::connect(&m_spin, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_turn = v.toReal();
            update();
        });
    }

protected:
    void enterEvent(QEnterEvent *) override {
        if (fun::enabled() && m_spin.state() != QAbstractAnimation::Running) m_spin.start();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) m_add();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const qreal side = std::min(width() * 0.55, height() * 0.45);
        const QRectF door(width() / 2.0 - side / 2, height() * 0.1, side, side);
        fun::paintVaultDoor(p, door, m_turn);
        const style::ColorTheme &theme = style::current();
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(theme.strong);
        const QRectF text(8, door.bottom() + 10, width() - 16, height() - door.bottom() - 10);
        p.drawText(text, Qt::AlignHCenter | Qt::AlignTop, QObject::tr("Your vault is empty"));
        f.setBold(false);
        p.setFont(f);
        p.setPen(theme.muted);
        p.drawText(text.adjusted(0, p.fontMetrics().height() + 6, 0, 0), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   QObject::tr("Click the safe to add your first account."));
    }

private:
    std::function<void()> m_add;
    QVariantAnimation m_spin;
    qreal m_turn = 0;
};

}  // namespace

VaultPanel::VaultPanel(vault::Store *store, SessionStore *sessions, QWidget *parent)
    : QWidget(parent), m_store(store), m_sessions(sessions) {
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter"));
    m_filter->setClearButtonEnabled(true);
    auto *add = new QToolButton(this);
    add->setText(QStringLiteral("+"));
    add->setToolTip(tr("Add an account"));
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(m_filter, 1);
    top->addWidget(add);

    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setToolTip(tr("Click an account at a login or password prompt to log in"));
    auto *hint = new QLabel(tr("Click an account to log in at the prompt of the current terminal. Ctrl+Shift+L uses the account linked to it (in bold)."), this);
    hint->setWordWrap(true);
    auto mute = [hint] { hint->setStyleSheet(QStringLiteral("color:%1").arg(style::current().muted.name())); };
    mute();
    connect(style::notifier(), &style::Notifier::changed, hint, mute);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(top);
    m_empty = new EmptyVault([this] { addEntry(); }, this);
    m_stack = new QStackedLayout;
    m_stack->addWidget(m_list);
    m_stack->addWidget(m_empty);
    layout->addLayout(m_stack, 1);
    layout->addWidget(hint);

    connect(add, &QToolButton::clicked, this, &VaultPanel::addEntry);
    connect(m_filter, &QLineEdit::textChanged, this, &VaultPanel::refresh);
    connect(m_store, &vault::Store::changed, this, &VaultPanel::refresh);
    connect(m_sessions, &SessionStore::changed, this, &VaultPanel::refresh);
    connect(style::notifier(), &style::Notifier::changed, this, &VaultPanel::refresh);  // icon colours
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString id = item->data(IdRole).toString();
        if (!id.isEmpty()) Q_EMIT loginRequested(id);
    });
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_list->itemAt(pos);
        if (item && item->data(IdRole).toString().isEmpty()) item = nullptr;
        QMenu menu(this);
        if (item) {
            const QString id = item->data(IdRole).toString();
            menu.addAction(tr("Log in"), this, [this, id] { Q_EMIT loginRequested(id); });
            menu.addSeparator();
            menu.addAction(tr("Copy user name"), this, [this, id] {
                if (const vault::Entry *e = m_store->find(id)) QGuiApplication::clipboard()->setText(e->user);
            });
            menu.addAction(tr("Copy password (cleared after 30 s)"), this, [this, id] {
                if (const vault::Entry *e = m_store->find(id)) copyLater(e->password());
            });
            menu.addSeparator();
            menu.addAction(tr("Edit..."), this, [this, id] { editEntry(id); });
            menu.addAction(tr("Delete"), this, [this, id] {
                const vault::Entry *e = m_store->find(id);
                if (e && QMessageBox::question(this, tr("Delete account"), tr("Delete \"%1\" from the vault?").arg(e->name))
                             == QMessageBox::Yes) {
                    m_store->remove(id);
                }
            });
            menu.addSeparator();
        }
        menu.addAction(tr("Add an account..."), this, &VaultPanel::addEntry);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    refresh();
}

void VaultPanel::setCurrentSession(const QString &sessionId, const QString &folder) {
    if (sessionId == m_currentSession && folder == m_currentFolder) return;
    m_currentSession = sessionId;
    m_currentFolder = folder;
    refresh();
}

QString VaultPanel::linksText(const vault::Entry &e) const {
    QStringList used;
    for (const QString &f : e.folders) used << tr("folder %1").arg(f);
    for (const QString &id : e.sessions) {
        if (const SessionInfo *s = m_sessions->find(id)) used << s->name;
    }
    QString text = used.isEmpty() ? tr("Not linked to any session") : tr("Used for: %1").arg(used.join(QStringLiteral(", ")));
    if (e.claude) text += QLatin1Char('\n') + tr("Claude may log in with this account");
    if (!e.note.isEmpty()) text += QLatin1Char('\n') + e.note;
    return text;
}

void VaultPanel::refresh() {
    const QString filter = m_filter->text().trimmed();
    const QString selected = m_list->currentItem() ? m_list->currentItem()->data(IdRole).toString() : QString();
    m_list->clear();
    // Accounts of the current session first (direct link, then folder).
    QList<const vault::Entry *> order;
    for (const vault::Entry &e : m_store->entries()) order << &e;
    std::stable_sort(order.begin(), order.end(), [this](const vault::Entry *a, const vault::Entry *b) {
        return a->linkStrength(m_currentSession, m_currentFolder) > b->linkStrength(m_currentSession, m_currentFolder);
    });
    const QIcon key = style::icon(style::Glyph::Vault);
    for (const vault::Entry *e : order) {
        if (!filter.isEmpty() && !e->name.contains(filter, Qt::CaseInsensitive)
            && !e->user.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        QString label = e->user.isEmpty() ? e->name : QStringLiteral("%1  (%2)").arg(e->name, e->user);
        if (e->claude) label += QStringLiteral("  ") + tr("[Claude]");
        auto *item = new QListWidgetItem(key, label, m_list);
        item->setData(IdRole, e->id);
        item->setToolTip(linksText(*e));
        if (e->linkStrength(m_currentSession, m_currentFolder) > 0) {
            QFont bold = item->font();
            bold.setBold(true);
            item->setFont(bold);
        }
        if (e->id == selected) m_list->setCurrentItem(item);
    }
    m_stack->setCurrentWidget(m_store->entries().isEmpty() ? m_empty : static_cast<QWidget *>(m_list));
    m_empty->update();
}

void VaultPanel::addEntry() {
    vault::Entry e;
    if (!editDialog(e, tr("Add an account"))) return;
    m_store->upsert(std::move(e));
    fun::confetti(window());
}

void VaultPanel::editEntry(const QString &id) {
    const vault::Entry *found = m_store->find(id);
    if (!found) return;
    vault::Entry e = *found;
    if (editDialog(e, tr("Edit account"))) m_store->upsert(std::move(e));
}

bool VaultPanel::editDialog(vault::Entry &entry, const QString &title) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto *name = new QLineEdit(entry.name, &dialog);
    name->setPlaceholderText(tr("Core switches"));
    auto *user = new QLineEdit(entry.user, &dialog);
    user->setPlaceholderText(tr("admin"));
    auto *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(entry.protectedPassword.isEmpty() ? QString() : tr("(unchanged)"));
    QAction *show = password->addAction(style::icon(style::Glyph::Eye), QLineEdit::TrailingPosition);
    show->setText(tr("Show"));
    show->setToolTip(tr("Show the password"));
    show->setCheckable(true);
    connect(show, &QAction::toggled, password, [password](bool on) {
        password->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    auto *note = new QPlainTextEdit(entry.note, &dialog);
    note->setFixedHeight(48);

    // Where the account is used: tick folders (with their sub-folders) or sessions.
    auto *links = new QTreeWidget(&dialog);
    links->setHeaderHidden(true);
    links->setMinimumHeight(180);
    links->setToolTip(tr("Tick the folders or sessions that use this account. A folder covers its sub-folders."));
    QHash<QString, QTreeWidgetItem *> folderItems;
    std::function<QTreeWidgetItem *(const QString &)> folderItem = [&](const QString &path) -> QTreeWidgetItem * {
        if (path.isEmpty()) return nullptr;
        if (QTreeWidgetItem *existing = folderItems.value(path)) return existing;
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem *parentItem = folderItem(slash < 0 ? QString() : path.left(slash));
        auto *item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(links);
        item->setText(0, slash < 0 ? path : path.mid(slash + 1));
        item->setData(0, Qt::UserRole, QStringLiteral("folder:") + path);
        item->setCheckState(0, entry.folders.contains(path) ? Qt::Checked : Qt::Unchecked);
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        folderItems.insert(path, item);
        return item;
    };
    QStringList folders = m_sessions->folders();
    for (const SessionInfo &s : m_sessions->sessions()) {
        if (!s.folder.isEmpty() && !folders.contains(s.folder)) folders << s.folder;
    }
    folders.sort(Qt::CaseInsensitive);
    for (const QString &f : folders) folderItem(f);
    for (const SessionInfo &s : m_sessions->sessions()) {
        QTreeWidgetItem *parentItem = folderItem(s.folder);
        auto *item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(links);
        item->setText(0, s.host.isEmpty() ? s.name : QStringLiteral("%1  (%2)").arg(s.name, s.host));
        item->setData(0, Qt::UserRole, QStringLiteral("session:") + s.id);
        item->setCheckState(0, entry.sessions.contains(s.id) ? Qt::Checked : Qt::Unchecked);
    }
    links->expandAll();
    auto *claude = new QCheckBox(tr("Claude may log in with this account (FastShell types it, Claude never sees it)"), &dialog);
    claude->setChecked(entry.claude);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *form = new QFormLayout(&dialog);
    form->addRow(tr("Name:"), name);
    form->addRow(tr("User:"), user);
    form->addRow(tr("Password:"), password);
    form->addRow(tr("Note:"), note);
    auto *linksHint = new QLabel(tr("A ticked folder covers its sub-folders and their sessions. The account of a "
                                    "session comes first in the Vault panel, in bold."),
                                 &dialog);
    linksHint->setWordWrap(true);
    linksHint->setStyleSheet(QStringLiteral("color:%1").arg(style::current().muted.name()));
    auto *linksBox = new QVBoxLayout;
    linksBox->setContentsMargins(0, 0, 0, 0);
    linksBox->addWidget(links);
    linksBox->addWidget(linksHint);
    form->addRow(tr("Used for:"), linksBox);
    form->addRow(QString(), claude);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(460, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted) return false;
    entry.name = name->text().trimmed();
    entry.user = user->text().trimmed();
    if (entry.name.isEmpty()) entry.name = entry.user.isEmpty() ? tr("Account") : entry.user;
    if (!password->text().isEmpty()) entry.setPassword(password->text());
    entry.note = note->toPlainText().trimmed();
    entry.folders.clear();
    entry.sessions.clear();
    for (QTreeWidgetItemIterator it(links); *it; ++it) {
        if ((*it)->checkState(0) != Qt::Checked) continue;
        const QString key = (*it)->data(0, Qt::UserRole).toString();
        if (key.startsWith(QLatin1String("folder:"))) entry.folders << key.mid(7);
        else if (key.startsWith(QLatin1String("session:"))) entry.sessions << key.mid(8);
    }
    entry.claude = claude->isChecked();
    return true;
}

void VaultPanel::copyLater(const QString &text) {
    if (text.isEmpty()) return;
    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setText(text);
    // Clear it later, unless something else was copied since.
    const QString copied = text;
    QTimer::singleShot(ClipboardClearMs, clipboard, [clipboard, copied] {
        if (clipboard->text() == copied) clipboard->clear();
    });
}

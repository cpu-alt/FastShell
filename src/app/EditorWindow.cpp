// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/EditorWindow.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSaveFile>
#include <QStatusBar>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/Style.h"
#include "net/SftpClient.h"

namespace {

QFont editorFont() {
    const QStringList families = QFontDatabase::families();
    for (const char *name : {"Cascadia Mono", "Consolas", "DejaVu Sans Mono"}) {
        if (families.contains(QLatin1String(name))) {
            QFont font(QLatin1String(name), 11);
            font.setStyleHint(QFont::Monospace);
            return font;
        }
    }
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    return font;
}

class Gutter : public QWidget {
public:
    explicit Gutter(CodeEdit *edit) : QWidget(edit), m_edit(edit) {}
    QSize sizeHint() const override { return {m_edit->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *event) override { m_edit->paintGutter(event); }

private:
    CodeEdit *m_edit;
};

}  // namespace

// --- CodeEdit: plain text with line numbers ------------------------------

CodeEdit::CodeEdit(QWidget *parent) : QPlainTextEdit(parent), m_gutter(new Gutter(this)) {
    setFont(editorFont());
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(gutterWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy) m_gutter->scroll(0, dy);
        else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    });
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

int CodeEdit::gutterWidth() const {
    int digits = 1;
    for (int n = qMax(1, blockCount()); n >= 10; n /= 10) ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEdit::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect r = contentsRect();
    m_gutter->setGeometry(QRect(r.left(), r.top(), gutterWidth(), r.height()));
}

void CodeEdit::paintGutter(QPaintEvent *event) {
    const style::ColorTheme &theme = style::current();
    QPainter p(m_gutter);
    p.fillRect(event->rect(), theme.panel);
    p.setFont(font());
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    const int current = textCursor().blockNumber();
    while (block.isValid() && top <= event->rect().bottom()) {
        const int height = qRound(blockBoundingRect(block).height());
        if (block.isVisible()) {
            p.setPen(block.blockNumber() == current ? theme.text : theme.muted);
            p.drawText(0, top, m_gutter->width() - 6, height, Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(block.blockNumber() + 1));
        }
        top += height;
        block = block.next();
    }
}

// --- EditorWindow --------------------------------------------------------

EditorWindow *EditorWindow::openLocal(const QString &path, QWidget *parent) {
    auto *w = new EditorWindow(path, nullptr, parent);
    QFile file(path);
    if (file.exists() && !file.open(QIODevice::ReadOnly)) {
        w->loadFailed(file.errorString());
    } else if (file.size() > 16 * 1024 * 1024) {
        w->loadFailed(tr("The file is too large to edit (over 16 MB)."));
    } else {
        w->load(file.isOpen() ? file.readAll() : QByteArray());
    }
    return w;
}

EditorWindow *EditorWindow::openRemote(SftpClient *client, const QString &path, QWidget *parent) {
    auto *w = new EditorWindow(path, client, parent);
    connect(client, &SftpClient::fileRead, w, [w](quint64 id, const QByteArray &data, const QString &error, bool denied) {
        if (id != w->m_pending) return;
        w->m_pending = 0;
        if (error.isEmpty()) {
            w->load(data);
            return;
        }
        // Like `sudo nano`: protected file, offer to open it with sudo.
        if (denied && !w->m_sudo && w->m_client) {
            const QByteArray password = w->askSudoPassword(tr("You may not read this file. Open it with sudo?"));
            if (!password.isEmpty()) {
                w->m_sudo = true;
                w->m_edit->setPlaceholderText(tr("Loading with sudo..."));
                w->m_pending = w->m_client->readFile(w->m_path, password);
                return;
            }
        }
        w->loadFailed(error);
    });
    connect(client, &SftpClient::fileWritten, w, [w](quint64 id, const QString &error, bool denied) {
        if (id != w->m_pending) return;
        w->m_pending = 0;
        if (denied && !w->m_sudo) {
            w->m_sudo = true;  // the next save (now) goes through sudo
            if (w->save()) return;
            w->m_sudo = false;
        }
        w->saveFinished(error);
    });
    w->m_pending = client->readFile(path);
    return w;
}

EditorWindow::EditorWindow(QString path, SftpClient *client, QWidget *parent)
    : QMainWindow(parent, Qt::Window), m_path(std::move(path)), m_client(client), m_remote(client != nullptr) {
    setAttribute(Qt::WA_DeleteOnClose);
    resize(900, 640);

    m_edit = new CodeEdit(this);
    m_edit->setReadOnly(true);
    m_edit->setPlaceholderText(tr("Loading..."));

    // Find / replace bar (Ctrl+F, Ctrl+H), hidden by default.
    m_findBar = new QWidget(this);
    m_find = new QLineEdit(m_findBar);
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(true);
    m_replace = new QLineEdit(m_findBar);
    m_replace->setPlaceholderText(tr("Replace with"));
    m_case = new QCheckBox(tr("Match case"), m_findBar);
    auto button = [this](const QString &text, auto slot) {
        auto *b = new QToolButton(m_findBar);
        b->setText(text);
        connect(b, &QToolButton::clicked, this, slot);
        return b;
    };
    auto *findRow = new QHBoxLayout;
    findRow->addWidget(m_find, 1);
    findRow->addWidget(button(tr("Previous"), [this] { findNext(true); }));
    findRow->addWidget(button(tr("Next"), [this] { findNext(false); }));
    findRow->addWidget(m_case);
    findRow->addWidget(button(tr("Close"), [this] {
        m_findBar->hide();
        m_edit->setFocus();
    }));
    m_replaceRow = new QWidget(m_findBar);
    auto *replaceRow = new QHBoxLayout(m_replaceRow);
    replaceRow->setContentsMargins(0, 0, 0, 0);
    replaceRow->addWidget(m_replace, 1);
    replaceRow->addWidget(button(tr("Replace"), [this] { replaceOne(); }));
    replaceRow->addWidget(button(tr("Replace all"), [this] { replaceAll(); }));
    auto *barLayout = new QVBoxLayout(m_findBar);
    barLayout->setContentsMargins(6, 4, 6, 4);
    barLayout->addLayout(findRow);
    barLayout->addWidget(m_replaceRow);
    m_findBar->hide();
    connect(m_find, &QLineEdit::returnPressed, this, [this] { findNext(false); });
    connect(m_replace, &QLineEdit::returnPressed, this, [this] { replaceOne(); });

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_findBar);
    setCentralWidget(central);

    m_position = new QLabel(this);
    m_info = new QLabel(this);
    statusBar()->addWidget(m_position);
    statusBar()->addPermanentWidget(m_info);

    // Menus (the shortcuts also work without opening them).
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *saveAction = file->addAction(tr("&Save"), this, [this] { save(); });
    saveAction->setShortcut(QKeySequence::Save);
    file->addAction(tr("Re&load"), this, [this] {
        if (m_edit->document()->isModified()
            && QMessageBox::question(this, tr("Reload"), tr("Discard your changes and reload the file?"))
                   != QMessageBox::Yes) {
            return;
        }
        if (m_remote) {
            if (!m_client) return;
            QByteArray password;
            if (m_sudo) {
                password = askSudoPassword(tr("Reload the file with sudo?"));
                if (password.isEmpty()) return;
            }
            m_pending = m_client->readFile(m_path, password);
            password.fill('\0');
        } else {
            QFile f(m_path);
            if (f.open(QIODevice::ReadOnly)) load(f.readAll());
            else loadFailed(f.errorString());
        }
    });
    file->addSeparator();
    QAction *closeAction = file->addAction(tr("&Close"), this, &QWidget::close);
    closeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Undo"), QKeySequence::Undo, m_edit, &QPlainTextEdit::undo);
    edit->addAction(tr("&Redo"), QKeySequence(Qt::CTRL | Qt::Key_Y), m_edit, &QPlainTextEdit::redo);
    edit->addSeparator();
    edit->addAction(tr("Cu&t"), QKeySequence::Cut, m_edit, &QPlainTextEdit::cut);
    edit->addAction(tr("&Copy"), QKeySequence::Copy, m_edit, &QPlainTextEdit::copy);
    edit->addAction(tr("&Paste"), QKeySequence::Paste, m_edit, &QPlainTextEdit::paste);
    edit->addAction(tr("Select &all"), QKeySequence::SelectAll, m_edit, &QPlainTextEdit::selectAll);
    edit->addSeparator();
    edit->addAction(tr("&Find..."), QKeySequence::Find, this, [this] { showFind(false); });
    edit->addAction(tr("Find &next"), QKeySequence(Qt::Key_F3), this, [this] { findNext(false); });
    edit->addAction(tr("Find pre&vious"), QKeySequence(Qt::SHIFT | Qt::Key_F3), this, [this] { findNext(true); });
    edit->addAction(tr("R&eplace..."), QKeySequence(Qt::CTRL | Qt::Key_H), this, [this] { showFind(true); });
    edit->addAction(tr("&Go to line..."), QKeySequence(Qt::CTRL | Qt::Key_G), this, &EditorWindow::goToLine);
    // Shortcuts of the menu must win over the editor's own (Ctrl+Z etc. are the same anyway).
    for (QAction *a : edit->actions()) a->setShortcutContext(Qt::WindowShortcut);

    auto *escape = new QAction(this);
    escape->setShortcut(Qt::Key_Escape);
    connect(escape, &QAction::triggered, this, [this] {
        m_findBar->hide();
        m_edit->setFocus();
    });
    addAction(escape);

    connect(m_edit, &QPlainTextEdit::cursorPositionChanged, this, &EditorWindow::updateStatus);
    connect(m_edit->document(), &QTextDocument::modificationChanged, this, &EditorWindow::updateTitle);

    updateTitle();
    updateStatus();
    style::decorate(this);
    show();
}

void EditorWindow::load(const QByteArray &data) {
    m_format = editor::decode(data);
    if (m_format.binary) {
        loadFailed(tr("This is not a text file."));
        return;
    }
    m_edit->setPlainText(m_format.text);
    m_edit->document()->setModified(false);
    m_edit->setReadOnly(false);
    m_edit->setPlaceholderText(QString());
    m_loaded = true;
    m_edit->setFocus();
    updateTitle();
    updateStatus();
}

void EditorWindow::loadFailed(const QString &message) {
    m_edit->setPlaceholderText(message);
    m_edit->setReadOnly(true);
    m_loaded = false;
    statusBar()->showMessage(message);
    QMessageBox::warning(this, tr("Open file"), tr("Cannot open %1:\n%2").arg(m_path, message));
}

bool EditorWindow::save() {
    if (!m_loaded || m_pending) return false;
    const QByteArray data = editor::encode(m_edit->toPlainText(), m_format.crlf, m_format.bom, m_format.notUtf8);
    if (m_remote) {
        if (!m_client) {
            saveFinished(tr("The SSH tab of this file was closed. Copy your text before closing this window."));
            return false;
        }
        QByteArray password;
        if (m_sudo) {
            password = askSudoPassword(tr("This file belongs to another account. Save it with sudo?\n"
                                          "The previous version is kept as %1.bak.")
                                           .arg(m_path.section(QLatin1Char('/'), -1)));
            if (password.isEmpty()) return false;
        }
        statusBar()->showMessage(m_sudo ? tr("Saving with sudo...") : tr("Saving..."));
        m_pending = m_client->writeFile(m_path, data, password);
        password.fill('\0');
        return true;
    }
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        saveFinished(file.errorString());
        return false;
    }
    saveFinished(QString());
    return true;
}

QByteArray EditorWindow::askSudoPassword(const QString &question) {
    // Typed by the user in a FastShell window (the server cannot imitate it);
    // used once, never stored.
    bool ok = false;
    const QString host = m_client ? m_client->host() : QString();
    QString text = QInputDialog::getText(this, tr("sudo on %1").arg(host),
                                         question + QStringLiteral("\n\n") + tr("Your sudo password:"), QLineEdit::Password,
                                         QString(), &ok);
    QByteArray password = ok ? text.toUtf8() : QByteArray();
    text.fill(QLatin1Char(' '));
    return password;
}

void EditorWindow::saveFinished(const QString &error) {
    if (!error.isEmpty()) {
        m_closeAfterSave = false;
        statusBar()->showMessage(tr("Not saved"));
        QMessageBox::warning(this, tr("Save"), tr("Cannot save %1:\n%2").arg(m_path, error));
        return;
    }
    m_edit->document()->setModified(false);
    statusBar()->showMessage(tr("Saved"), 4000);
    if (m_closeAfterSave) close();
}

void EditorWindow::closeEvent(QCloseEvent *event) {
    if (m_loaded && m_edit->document()->isModified()) {
        const auto answer = QMessageBox::question(this, tr("Close"), tr("Save the changes to %1?").arg(m_path),
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Save) {
            event->ignore();
            m_closeAfterSave = true;  // closes once the save succeeded
            if (!save()) m_closeAfterSave = false;
            return;
        }
        m_edit->document()->setModified(false);
    }
    event->accept();
}

void EditorWindow::showFind(bool replace) {
    m_findBar->show();
    m_replaceRow->setVisible(replace);
    const QString selected = m_edit->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar(0x2029))) m_find->setText(selected);
    m_find->setFocus();
    m_find->selectAll();
}

bool EditorWindow::findNext(bool backward) {
    if (m_find->text().isEmpty()) {
        showFind(m_replaceRow->isVisible());
        return false;
    }
    const QTextCursor cursor = m_edit->textCursor();
    const QString text = m_edit->toPlainText();
    const qsizetype at = editor::find(text, m_find->text(), backward ? cursor.selectionStart() : cursor.selectionEnd(),
                                      backward, m_case->isChecked());
    if (at < 0) {
        statusBar()->showMessage(tr("Not found: %1").arg(m_find->text()), 4000);
        return false;
    }
    QTextCursor found(m_edit->document());
    found.setPosition(int(at));
    found.setPosition(int(at + m_find->text().size()), QTextCursor::KeepAnchor);
    m_edit->setTextCursor(found);
    return true;
}

void EditorWindow::replaceOne() {
    if (m_edit->isReadOnly()) return;
    QTextCursor cursor = m_edit->textCursor();
    const Qt::CaseSensitivity cs = m_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection() && cursor.selectedText().compare(m_find->text(), cs) == 0) {
        cursor.insertText(m_replace->text());
    }
    findNext(false);
}

void EditorWindow::replaceAll() {
    if (m_edit->isReadOnly() || m_find->text().isEmpty()) return;
    const Qt::CaseSensitivity cs = m_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QString text = m_edit->toPlainText();
    const qsizetype count = text.count(m_find->text(), cs);
    if (!count) {
        statusBar()->showMessage(tr("Not found: %1").arg(m_find->text()), 4000);
        return;
    }
    text.replace(m_find->text(), m_replace->text(), cs);
    // One undo step for the whole replacement.
    QTextCursor cursor(m_edit->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
    statusBar()->showMessage(tr("%n replacement(s)", nullptr, int(count)), 4000);
}

void EditorWindow::goToLine() {
    bool ok = false;
    const int line = QInputDialog::getInt(this, tr("Go to line"), tr("Line:"), m_edit->textCursor().blockNumber() + 1, 1,
                                          m_edit->blockCount(), 1, &ok);
    if (!ok) return;
    QTextCursor cursor(m_edit->document()->findBlockByNumber(line - 1));
    m_edit->setTextCursor(cursor);
    m_edit->centerCursor();
}

void EditorWindow::updateTitle() {
    const QString name = m_remote ? m_path.section(QLatin1Char('/'), -1) : QFileInfo(m_path).fileName();
    const QString where = m_remote ? (m_client ? m_client->host() + QLatin1Char(':') : QString()) + m_path : m_path;
    setWindowTitle((m_edit->document()->isModified() ? QStringLiteral("* ") : QString()) + name + QStringLiteral(" - ")
                   + where + QStringLiteral(" - FastShell"));
}

void EditorWindow::updateStatus() {
    const QTextCursor cursor = m_edit->textCursor();
    m_position->setText(tr("Line %1, column %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    QString encoding = m_format.notUtf8 ? QStringLiteral("Latin-1") : QStringLiteral("UTF-8");
    if (m_format.bom) encoding += QStringLiteral(" BOM");
    m_info->setText(QStringLiteral("%1   %2").arg(m_format.crlf ? QStringLiteral("CRLF (Windows)") : QStringLiteral("LF (Unix)"),
                                                 encoding));
}

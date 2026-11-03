// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/ManageDialogs.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace serialui {

QComboBox *baudCombo(QWidget *parent) {
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    for (int rate : {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600}) {
        combo->addItem(QString::number(rate));
    }
    combo->setCurrentText(QStringLiteral("9600"));
    combo->setValidator(new QIntValidator(50, 10000000, combo));
    return combo;
}

QComboBox *dataBitsCombo(QWidget *parent) {
    auto *combo = new QComboBox(parent);
    combo->addItems({QStringLiteral("8"), QStringLiteral("7"), QStringLiteral("6"), QStringLiteral("5")});
    return combo;
}

QComboBox *parityCombo(QWidget *parent) {
    auto *combo = new QComboBox(parent);
    combo->addItem(QObject::tr("None"), QStringLiteral("N"));
    combo->addItem(QObject::tr("Even"), QStringLiteral("E"));
    combo->addItem(QObject::tr("Odd"), QStringLiteral("O"));
    combo->addItem(QObject::tr("Mark"), QStringLiteral("M"));
    combo->addItem(QObject::tr("Space"), QStringLiteral("S"));
    return combo;
}

QComboBox *stopBitsCombo(QWidget *parent) {
    auto *combo = new QComboBox(parent);
    combo->addItems({QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2")});
    return combo;
}

QComboBox *flowCombo(QWidget *parent) {
    auto *combo = new QComboBox(parent);
    combo->addItem(QObject::tr("None"), QStringLiteral("none"));
    combo->addItem(QObject::tr("Hardware (RTS/CTS)"), QStringLiteral("rtscts"));
    combo->addItem(QObject::tr("Software (XON/XOFF)"), QStringLiteral("xonxoff"));
    return combo;
}

void selectData(QComboBox *combo, const QString &value) {
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

}  // namespace serialui

namespace {

QDialogButtonBox *closeButtons(QDialog *dialog) {
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::accept);
    return buttons;
}

void paintSwatch(QPushButton *button, const QColor &color) {
    button->setText(color.isValid() ? color.name() : QObject::tr("Default"));
    button->setStyleSheet(color.isValid() ? QStringLiteral("QPushButton { background:%1; color:white; }").arg(color.name())
                                          : QString());
}

}  // namespace

// --- Users -----------------------------------------------------------------------

UsersDialog::UsersDialog(SessionStore *store, QWidget *parent) : QDialog(parent), m_store(store) {
    setWindowTitle(tr("Users"));
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(180);
    auto *add = new QPushButton(tr("New user"), this);
    m_delete = new QPushButton(tr("Delete"), this);

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("e.g. Admin switches"));
    m_user = new QLineEdit(this);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_identity = new QLineEdit(this);
    m_identity->setPlaceholderText(tr("optional private key"));
    auto *browse = new QPushButton(tr("Browse..."), this);
    auto *identityRow = new QHBoxLayout;
    identityRow->addWidget(m_identity);
    identityRow->addWidget(browse);
    auto *save = new QPushButton(tr("Save"), this);
    save->setDefault(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("User"), m_user);
    form->addRow(tr("Password"), m_password);
    form->addRow(tr("Private key"), identityRow);
    auto *hint = new QLabel(tr("The password is encrypted for your Windows account."), this);
    hint->setStyleSheet(QStringLiteral("color:#7F848E"));
    form->addRow(QString(), hint);
    form->addRow(QString(), save);

    auto *left = new QVBoxLayout;
    left->addWidget(m_list);
    auto *leftButtons = new QHBoxLayout;
    leftButtons->addWidget(add);
    leftButtons->addWidget(m_delete);
    left->addLayout(leftButtons);
    auto *columns = new QHBoxLayout;
    columns->addLayout(left);
    columns->addLayout(form, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(columns);
    layout->addWidget(closeButtons(this));

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        showCredential(row >= 0 ? m_list->item(row)->data(Qt::UserRole).toString() : QString());
    });
    connect(add, &QPushButton::clicked, this, [this] {
        m_list->clearSelection();
        m_list->setCurrentRow(-1);
        showCredential(QString());
        m_name->setFocus();
    });
    connect(save, &QPushButton::clicked, this, &UsersDialog::saveCurrent);
    connect(m_delete, &QPushButton::clicked, this, [this] {
        if (m_currentId.isEmpty()) return;
        if (QMessageBox::question(this, tr("Delete user"),
                                  tr("Delete \"%1\"? Sessions using it keep working but will ask for a login.")
                                      .arg(m_name->text())) == QMessageBox::Yes) {
            m_store->removeCredential(m_currentId);
            reload();
        }
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Private key"), QDir::homePath() + QStringLiteral("/.ssh"));
        if (!f.isEmpty()) m_identity->setText(f);
    });

    reload();
    resize(620, 300);
}

void UsersDialog::reload(const QString &select) {
    m_list->blockSignals(true);
    m_list->clear();
    for (const Credential &c : m_store->credentials()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(c.name, c.user), m_list);
        item->setData(Qt::UserRole, c.id);
        if (c.id == select) m_list->setCurrentItem(item);
    }
    m_list->blockSignals(false);
    showCredential(select);
}

void UsersDialog::showCredential(const QString &id) {
    m_currentId = id;
    const Credential *c = m_store->findCredential(id);
    m_name->setText(c ? c->name : QString());
    m_user->setText(c ? c->user : QString());
    m_password->clear();
    m_keptPassword = c ? c->protectedPassword : QByteArray();
    m_password->setPlaceholderText(m_keptPassword.isEmpty() ? tr("optional, asked when needed")
                                                            : tr("saved (leave empty to keep)"));
    m_identity->setText(c ? c->identityFile : QString());
    m_delete->setEnabled(c != nullptr);
}

void UsersDialog::saveCurrent() {
    if (m_user->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Users"), tr("The user name is required."));
        return;
    }
    Credential c;
    c.id = m_currentId;
    c.name = m_name->text().trimmed();
    c.user = m_user->text().trimmed();
    c.identityFile = m_identity->text().trimmed();
    if (!m_password->text().isEmpty()) c.setPassword(m_password->text());
    else c.protectedPassword = m_keptPassword;
    m_store->upsertCredential(c);
    // A new entry gets its id from the store: it is the last one.
    const QString id = c.id.isEmpty() ? m_store->credentials().last().id : c.id;
    reload(id);
}

// --- Device profiles ------------------------------------------------------------------

ProfilesDialog::ProfilesDialog(SessionStore *store, QWidget *parent) : QDialog(parent), m_store(store) {
    setWindowTitle(tr("Device profiles"));
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(160);
    auto *add = new QPushButton(tr("New profile"), this);
    m_delete = new QPushButton(tr("Delete"), this);

    m_name = new QLineEdit(this);
    m_tag = new QLineEdit(this);
    m_tag->setMaxLength(8);
    m_tag->setPlaceholderText(tr("label in the session tree"));
    m_colorButton = new QPushButton(this);
    m_baudRate = serialui::baudCombo(this);
    m_dataBits = serialui::dataBitsCombo(this);
    m_parity = serialui::parityCombo(this);
    m_stopBits = serialui::stopBitsCombo(this);
    m_flowControl = serialui::flowCombo(this);
    m_crlf = new QCheckBox(tr("Send CR LF for Enter"), this);
    m_backspace = new QCheckBox(tr("Backspace sends Ctrl+H (^H) instead of DEL"), this);
    auto *save = new QPushButton(tr("Save"), this);
    save->setDefault(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Tag"), m_tag);
    form->addRow(tr("Color"), m_colorButton);
    auto *serialTitle = new QLabel(tr("<b>Serial console</b> (SSH is not changed, as in PuTTY)"), this);
    form->addRow(serialTitle);
    form->addRow(tr("Speed (baud)"), m_baudRate);
    form->addRow(tr("Data bits"), m_dataBits);
    form->addRow(tr("Parity"), m_parity);
    form->addRow(tr("Stop bits"), m_stopBits);
    form->addRow(tr("Flow control"), m_flowControl);
    form->addRow(QString(), m_crlf);
    form->addRow(QString(), m_backspace);
    form->addRow(QString(), save);

    auto *left = new QVBoxLayout;
    left->addWidget(m_list);
    auto *leftButtons = new QHBoxLayout;
    leftButtons->addWidget(add);
    leftButtons->addWidget(m_delete);
    left->addLayout(leftButtons);
    auto *columns = new QHBoxLayout;
    columns->addLayout(left);
    columns->addLayout(form, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(columns);
    layout->addWidget(closeButtons(this));

    connect(m_colorButton, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color.isValid() ? m_color : QColor(0x1E, 0x88, 0xE5), this);
        if (c.isValid()) {
            m_color = c;
            paintSwatch(m_colorButton, m_color);
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        showProfile(row >= 0 ? m_list->item(row)->data(Qt::UserRole).toString() : QString());
    });
    connect(add, &QPushButton::clicked, this, [this] {
        m_list->setCurrentRow(-1);
        showProfile(QString());
        m_name->setFocus();
    });
    connect(save, &QPushButton::clicked, this, &ProfilesDialog::saveCurrent);
    connect(m_delete, &QPushButton::clicked, this, [this] {
        if (!m_currentId.isEmpty()
            && QMessageBox::question(this, tr("Delete profile"), tr("Delete \"%1\"?").arg(m_name->text()))
                   == QMessageBox::Yes) {
            m_store->removeProfile(m_currentId);
            reload();
        }
    });

    reload(m_store->profiles().isEmpty() ? QString() : m_store->profiles().first().id);
    resize(640, 420);
}

void ProfilesDialog::reload(const QString &select) {
    m_list->blockSignals(true);
    m_list->clear();
    for (const DeviceProfile &p : m_store->profiles()) {
        auto *item = new QListWidgetItem(p.name + (p.builtin ? tr("  (built-in)") : QString()), m_list);
        item->setData(Qt::UserRole, p.id);
        item->setForeground(p.color.isValid() ? p.color : palette().text().color());
        if (p.id == select) m_list->setCurrentItem(item);
    }
    m_list->blockSignals(false);
    showProfile(select);
}

void ProfilesDialog::showProfile(const QString &id) {
    m_currentId = id;
    const DeviceProfile *p = m_store->findProfile(id);
    const DeviceProfile defaults;
    const DeviceProfile &v = p ? *p : defaults;
    m_name->setText(v.name);
    m_tag->setText(v.tag);
    m_color = v.color;
    paintSwatch(m_colorButton, m_color);
    m_baudRate->setCurrentText(QString::number(v.baudRate));
    m_dataBits->setCurrentText(QString::number(v.dataBits));
    serialui::selectData(m_parity, QString(v.parity));
    m_stopBits->setCurrentText(v.stopBits);
    serialui::selectData(m_flowControl, v.flowControl);
    m_crlf->setChecked(v.sendCrLf);
    m_backspace->setChecked(v.backspaceCtrlH);
    m_delete->setEnabled(p && !p->builtin);
}

void ProfilesDialog::saveCurrent() {
    if (m_name->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Device profiles"), tr("The name is required."));
        return;
    }
    DeviceProfile p;
    p.id = m_currentId;
    p.name = m_name->text().trimmed();
    p.tag = m_tag->text().trimmed().toUpper();
    if (p.tag.isEmpty()) p.tag = p.name.left(6).toUpper();
    p.color = m_color;
    p.baudRate = m_baudRate->currentText().toInt() > 0 ? m_baudRate->currentText().toInt() : 9600;
    p.dataBits = m_dataBits->currentText().toInt();
    p.parity = m_parity->currentData().toString().at(0);
    p.stopBits = m_stopBits->currentText();
    p.flowControl = m_flowControl->currentData().toString();
    p.sendCrLf = m_crlf->isChecked();
    p.backspaceCtrlH = m_backspace->isChecked();
    m_store->upsertProfile(p);
    reload(p.id.isEmpty() ? m_store->profiles().last().id : p.id);
}

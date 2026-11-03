// Copyright 2026 EMF Hexa contributors. Apache-2.0.
#include "app/SessionDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSpinBox>

#include "app/ManageDialogs.h"

namespace {

enum TypeIndex { SshIndex, TelnetIndex, LocalIndex, SerialIndex };
const QString PortSeparator = QStringLiteral(" — ");

QWidget *rowWith(QWidget *parent, QWidget *main, QPushButton *button) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(main, 1);
    layout->addWidget(button);
    return row;
}

void paintSwatch(QPushButton *button, const QColor &color) {
    button->setText(color.isValid() ? color.name() : QObject::tr("Default (device color)"));
    button->setStyleSheet(color.isValid()
                              ? QStringLiteral("QPushButton { background:%1; color:%2; }")
                                    .arg(color.name(), color.lightness() > 140 ? QStringLiteral("black")
                                                                               : QStringLiteral("white"))
                              : QString());
}

}  // namespace

SessionDialog::SessionDialog(SessionStore *store, QWidget *parent) : QDialog(parent), m_store(store) {
    setWindowTitle(tr("Session"));

    m_type = new QComboBox(this);
    m_type->addItem(QStringLiteral("SSH"));
    m_type->addItem(QStringLiteral("Telnet"));
    m_type->addItem(tr("Local shell"));
    m_type->addItem(tr("Serial (COM port)"));
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("default: user@host"));
    m_folder = new QComboBox(this);
    m_folder->setEditable(true);
    m_folder->addItem(QString());
    m_folder->addItems(store->folders());
    m_folder->lineEdit()->setPlaceholderText(tr("folder, e.g. Switches/Building A"));

    // Device profile (tag + serial preset).
    m_profile = new QComboBox(this);
    auto *manageProfiles = new QPushButton(tr("Manage..."), this);
    m_profileRow = rowWith(this, m_profile, manageProfiles);
    connect(manageProfiles, &QPushButton::clicked, this, [this] {
        const QString current = m_profile->currentData().toString();
        ProfilesDialog(m_store, this).exec();
        fillProfiles(current);
    });

    // Remote
    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(tr("host, or user@host"));
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(22);

    m_credential = new QComboBox(this);
    auto *manageUsers = new QPushButton(tr("Manage..."), this);
    m_credentialRow = rowWith(this, m_credential, manageUsers);
    connect(manageUsers, &QPushButton::clicked, this, [this] {
        const QString current = m_credential->currentData().toString();
        UsersDialog dialog(m_store, this);
        dialog.exec();
        fillUsers(dialog.selectedId().isEmpty() ? current : dialog.selectedId());
    });

    m_user = new QLineEdit(this);
    m_user->setPlaceholderText(tr("default: current user"));
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("optional, asked when needed"));
    m_savePasswordBox = new QCheckBox(tr("Remember password (encrypted for your Windows account)"), this);
    m_identity = new QLineEdit(this);
    m_identity->setPlaceholderText(tr("optional, agent and ~/.ssh keys are tried"));
    auto *browseKey = new QPushButton(tr("Browse..."), this);
    m_identityRow = rowWith(this, m_identity, browseKey);
    connect(browseKey, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Private key"), QDir::homePath() + QStringLiteral("/.ssh"));
        if (!f.isEmpty()) m_identity->setText(f);
    });
    m_legacyBox = new QCheckBox(tr("Compatibility with old devices (SHA-1, CBC, group1)"), this);
    m_legacyBox->setToolTip(tr("SHA-1 and CBC algorithms are already tried automatically when needed; "
                               "this also allows diffie-hellman-group1-sha1, which is weak."));

    // Local shell
    m_program = new QLineEdit(this);
    m_program->setPlaceholderText(tr("default shell"));
    auto *browseProgram = new QPushButton(tr("Browse..."), this);
    m_programRow = rowWith(this, m_program, browseProgram);
    connect(browseProgram, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Program"));
        if (!f.isEmpty()) m_program->setText(QDir::toNativeSeparators(f));
    });
    m_arguments = new QLineEdit(this);
    m_workingDirectory = new QLineEdit(this);
    m_workingDirectory->setPlaceholderText(tr("default: home directory"));

    // Serial
    m_serialPort = new QComboBox(this);
    m_serialPort->setEditable(true);
    m_serialPort->lineEdit()->setPlaceholderText(QStringLiteral("COM3"));
    auto refreshPorts = [this] {
        const QString current = m_serialPort->currentText();
        m_serialPort->clear();
        for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
            m_serialPort->addItem(info.description().isEmpty() ? info.portName()
                                                                : info.portName() + PortSeparator + info.description(),
                                  info.portName());
        }
        if (!current.isEmpty()) m_serialPort->setCurrentText(current);
    };
    refreshPorts();
    auto *refresh = new QPushButton(tr("Refresh"), this);
    m_serialRow = rowWith(this, m_serialPort, refresh);
    connect(refresh, &QPushButton::clicked, this, refreshPorts);
    m_baudRate = serialui::baudCombo(this);
    m_dataBits = serialui::dataBitsCombo(this);
    m_parity = serialui::parityCombo(this);
    m_stopBits = serialui::stopBitsCombo(this);
    m_flowControl = serialui::flowCombo(this);
    m_crlfBox = new QCheckBox(tr("Send CR LF for Enter (default: CR only)"), this);
    m_backspaceBox = new QCheckBox(tr("Backspace sends Ctrl+H (^H) instead of DEL"), this);

    m_colorButton = new QPushButton(this);
    paintSwatch(m_colorButton, m_color);
    connect(m_colorButton, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color.isValid() ? m_color : QColor(0x1E, 0x88, 0xE5), this);
        if (c.isValid()) {
            m_color = c;
            paintSwatch(m_colorButton, m_color);
        }
    });
    m_saveBox = new QCheckBox(tr("Save in the session list"), this);
    m_saveBox->setChecked(true);

    m_form = new QFormLayout(this);
    m_form->addRow(tr("Type"), m_type);
    m_form->addRow(tr("Device"), m_profileRow);
    m_form->addRow(tr("Name"), m_name);
    m_form->addRow(tr("Folder"), m_folder);
    m_form->addRow(tr("Host"), m_host);
    m_form->addRow(tr("Port"), m_port);
    m_form->addRow(tr("Saved user"), m_credentialRow);
    m_form->addRow(tr("User"), m_user);
    m_form->addRow(tr("Password"), m_password);
    m_form->addRow(QString(), m_savePasswordBox);
    m_form->addRow(tr("Private key"), m_identityRow);
    m_form->addRow(QString(), m_legacyBox);
    m_form->addRow(tr("Program"), m_programRow);
    m_form->addRow(tr("Arguments"), m_arguments);
    m_form->addRow(tr("Start in"), m_workingDirectory);
    m_form->addRow(tr("Serial port"), m_serialRow);
    m_form->addRow(tr("Speed (baud)"), m_baudRate);
    m_form->addRow(tr("Data bits"), m_dataBits);
    m_form->addRow(tr("Parity"), m_parity);
    m_form->addRow(tr("Stop bits"), m_stopBits);
    m_form->addRow(tr("Flow control"), m_flowControl);
    m_form->addRow(QString(), m_crlfBox);
    m_form->addRow(QString(), m_backspaceBox);
    m_form->addRow(tr("Tab color"), m_colorButton);
    m_form->addRow(QString(), m_saveBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    fillUsers(QString());
    fillProfiles(QString());
    connect(m_type, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == SshIndex && m_port->value() == 23) m_port->setValue(22);
        if (index == TelnetIndex && m_port->value() == 22) m_port->setValue(23);
        updateVisibility();
    });
    connect(m_credential, &QComboBox::currentIndexChanged, this, &SessionDialog::updateVisibility);
    connect(m_profile, &QComboBox::activated, this, &SessionDialog::applyProfile);
    updateVisibility();
    resize(540, sizeHint().height());
}

int SessionDialog::typeIndex() const {
    return m_type->currentIndex();
}

void SessionDialog::fillUsers(const QString &select) {
    m_credential->blockSignals(true);
    m_credential->clear();
    m_credential->addItem(tr("None (type below)"), QString());
    for (const Credential &c : m_store->credentials()) {
        m_credential->addItem(QStringLiteral("%1  (%2)").arg(c.name, c.user), c.id);
    }
    serialui::selectData(m_credential, select);
    m_credential->blockSignals(false);
    updateVisibility();
}

void SessionDialog::fillProfiles(const QString &select) {
    m_profile->blockSignals(true);
    m_profile->clear();
    m_profile->addItem(tr("Generic"), QString());
    for (const DeviceProfile &p : m_store->profiles()) {
        QPixmap dot(12, 12);
        dot.fill(p.color.isValid() ? p.color : Qt::gray);
        m_profile->addItem(QIcon(dot), QStringLiteral("%1  [%2]").arg(p.name, p.tag), p.id);
    }
    serialui::selectData(m_profile, select);
    m_profile->blockSignals(false);
}

// Choosing a device fills the serial settings from its profile. SSH and
// Telnet keep their defaults, like PuTTY.
void SessionDialog::applyProfile() {
    const DeviceProfile *p = m_store->findProfile(m_profile->currentData().toString());
    if (!p) return;
    m_baudRate->setCurrentText(QString::number(p->baudRate));
    m_dataBits->setCurrentText(QString::number(p->dataBits));
    serialui::selectData(m_parity, QString(p->parity));
    m_stopBits->setCurrentText(p->stopBits);
    serialui::selectData(m_flowControl, p->flowControl);
    m_crlfBox->setChecked(p->sendCrLf);
    m_backspaceBox->setChecked(p->backspaceCtrlH);
}

void SessionDialog::updateVisibility() {
    const int type = typeIndex();
    const bool ssh = type == SshIndex;
    const bool remote = type == SshIndex || type == TelnetIndex;
    const bool local = type == LocalIndex;
    const bool serial = type == SerialIndex;
    const bool sharedUser = ssh && !m_credential->currentData().toString().isEmpty();

    m_form->setRowVisible(m_profileRow, !local);
    m_form->setRowVisible(m_host, remote);
    m_form->setRowVisible(m_port, remote);
    m_form->setRowVisible(m_credentialRow, ssh);
    m_form->setRowVisible(m_user, ssh && !sharedUser);
    m_form->setRowVisible(m_password, ssh && !sharedUser);
    m_form->setRowVisible(m_savePasswordBox, ssh && !sharedUser);
    m_form->setRowVisible(m_identityRow, ssh && !sharedUser);
    m_form->setRowVisible(m_legacyBox, ssh);
    m_form->setRowVisible(m_programRow, local);
    m_form->setRowVisible(m_arguments, local);
    m_form->setRowVisible(m_workingDirectory, local);
    for (QWidget *w : {m_serialRow, static_cast<QWidget *>(m_baudRate), static_cast<QWidget *>(m_dataBits),
                       static_cast<QWidget *>(m_parity), static_cast<QWidget *>(m_stopBits),
                       static_cast<QWidget *>(m_flowControl), static_cast<QWidget *>(m_crlfBox),
                       static_cast<QWidget *>(m_backspaceBox)}) {
        m_form->setRowVisible(w, serial);
    }
    adjustSize();
}

void SessionDialog::setSession(const SessionInfo &s) {
    m_id = s.id;
    m_name->setText(s.name);
    m_folder->setCurrentText(s.folder);
    m_type->setCurrentIndex(s.type == SessionInfo::Type::Ssh      ? SshIndex
                            : s.type == SessionInfo::Type::Telnet ? TelnetIndex
                            : s.type == SessionInfo::Type::Local  ? LocalIndex
                                                                  : SerialIndex);
    fillProfiles(s.profileId);
    fillUsers(s.credentialId);
    m_host->setText(s.host);
    m_port->setValue(s.port);
    m_user->setText(s.user);
    m_keptPassword = s.protectedPassword;
    m_password->setPlaceholderText(s.protectedPassword.isEmpty() ? tr("optional, asked when needed")
                                                                 : tr("saved (leave empty to keep)"));
    m_savePasswordBox->setChecked(!s.protectedPassword.isEmpty());
    m_identity->setText(s.identityFile);
    m_legacyBox->setChecked(s.legacyAlgorithms);
    m_program->setText(QDir::toNativeSeparators(s.program));
    QStringList quoted;
    for (const QString &arg : s.arguments) {
        quoted << (arg.contains(QLatin1Char(' ')) ? QLatin1Char('"') + arg + QLatin1Char('"') : arg);
    }
    m_arguments->setText(quoted.join(QLatin1Char(' ')));
    m_workingDirectory->setText(s.workingDirectory);

    m_serialPort->setCurrentText(s.serialPort);
    for (int i = 0; i < m_serialPort->count(); ++i) {
        if (m_serialPort->itemData(i).toString() == s.serialPort) m_serialPort->setCurrentIndex(i);
    }
    m_baudRate->setCurrentText(QString::number(s.baudRate));
    m_dataBits->setCurrentText(QString::number(s.dataBits));
    serialui::selectData(m_parity, QString(s.parity));
    m_stopBits->setCurrentText(s.stopBits);
    serialui::selectData(m_flowControl, s.flowControl);
    m_crlfBox->setChecked(s.sendCrLf);
    m_backspaceBox->setChecked(s.backspaceCtrlH);

    m_color = s.color;
    paintSwatch(m_colorButton, m_color);
    m_saveBox->setChecked(true);
    m_saveBox->setVisible(false);  // editing an existing session always saves
    setWindowTitle(tr("Edit session"));
    updateVisibility();
}

SessionInfo SessionDialog::session() const {
    SessionInfo s;
    s.id = m_id;
    const int type = typeIndex();
    s.type = type == SshIndex      ? SessionInfo::Type::Ssh
           : type == TelnetIndex   ? SessionInfo::Type::Telnet
           : type == LocalIndex    ? SessionInfo::Type::Local
                                   : SessionInfo::Type::Serial;
    s.folder = m_folder->currentText();
    s.profileId = s.type == SessionInfo::Type::Local ? QString() : m_profile->currentData().toString();

    s.host = m_host->text().trimmed();
    s.port = m_port->value();
    if (s.type == SessionInfo::Type::Ssh) {
        s.credentialId = m_credential->currentData().toString();
    }
    if (s.credentialId.isEmpty()) {
        s.user = m_user->text().trimmed();
        s.identityFile = m_identity->text().trimmed();
        if (const qsizetype at = s.host.indexOf(QLatin1Char('@')); at > 0) {
            s.user = s.host.left(at);
            s.host = s.host.mid(at + 1);
        }
        if (s.type == SessionInfo::Type::Ssh && m_savePasswordBox->isChecked()) {
            if (!m_password->text().isEmpty()) s.setPassword(m_password->text());
            else s.protectedPassword = m_keptPassword;
        }
    } else if (const qsizetype at = s.host.indexOf(QLatin1Char('@')); at > 0) {
        s.host = s.host.mid(at + 1);  // the shared user wins
    }
    s.legacyAlgorithms = m_legacyBox->isChecked();

    s.program = m_program->text().trimmed();
    s.arguments = QProcess::splitCommand(m_arguments->text());
    s.workingDirectory = m_workingDirectory->text().trimmed();

    s.serialPort = m_serialPort->currentText().section(PortSeparator, 0, 0).trimmed();
    s.baudRate = m_baudRate->currentText().toInt() > 0 ? m_baudRate->currentText().toInt() : 9600;
    s.dataBits = m_dataBits->currentText().toInt();
    s.parity = m_parity->currentData().toString().at(0);
    s.stopBits = m_stopBits->currentText();
    s.flowControl = m_flowControl->currentData().toString();
    s.sendCrLf = m_crlfBox->isChecked();
    s.backspaceCtrlH = s.type == SessionInfo::Type::Serial && m_backspaceBox->isChecked();
    s.color = m_color;

    s.name = m_name->text().trimmed();
    if (s.name.isEmpty()) {
        if (s.type == SessionInfo::Type::Local) {
            s.name = s.program.isEmpty() ? tr("Local shell") : QFileInfo(s.program).completeBaseName();
        } else if (s.type == SessionInfo::Type::Serial) {
            s.name = s.serialPort + QLatin1Char(' ') + QString::number(s.baudRate);
        } else {
            QString user = s.user;
            if (const Credential *c = m_store->findCredential(s.credentialId)) user = c->user;
            s.name = user.isEmpty() ? s.host : user + QLatin1Char('@') + s.host;
        }
    }
    return s;
}

QString SessionDialog::typedPassword() const {
    return m_credential->currentData().toString().isEmpty() ? m_password->text() : QString();
}

bool SessionDialog::saveRequested() const {
    return m_saveBox->isChecked();
}

void SessionDialog::setSaveRequested(bool save) {
    m_saveBox->setChecked(save);
}

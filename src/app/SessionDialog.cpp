// Copyright 2026 FastShell contributors. Apache-2.0.
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
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QToolButton>

#include "app/Icons.h"
#include "app/ManageDialogs.h"

namespace {

enum TypeIndex { SshIndex, TelnetIndex, LocalIndex, SerialIndex, RdpIndex };
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
    m_type->addItem(tr("Remote desktop (RDP)"));
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("default: user@host"));
    m_folder = new QComboBox(this);
    m_folder->setEditable(true);
    m_folder->addItem(QString());
    m_folder->addItems(store->folders());
    m_folder->lineEdit()->setPlaceholderText(tr("e.g. Switches/Building A"));

    // Device profile (tag + serial preset).
    m_profile = new QComboBox(this);
    auto *manageProfiles = new QPushButton(tr("Manage..."), this);
    m_profileRow = rowWith(this, m_profile, manageProfiles);
    connect(manageProfiles, &QPushButton::clicked, this, [this] {
        const QString current = m_profile->currentData().toString();
        ProfilesDialog(m_store, this).exec();
        fillProfiles(current);
    });

    // System mark shown next to the session.
    // A small icon button next to the name; its menu lists the systems.
    m_osButton = new QToolButton(this);
    m_osButton->setObjectName(QStringLiteral("osButton"));
    m_osButton->setPopupMode(QToolButton::InstantPopup);
    m_osButton->setIconSize(QSize(20, 20));
    m_osButton->setFixedSize(QSize(52, 30));
    auto *osMenu = new QMenu(m_osButton);
    osMenu->addAction(tr("None"), this, [this] { setSystem(QString()); });
    for (const icons::System &system : icons::systems()) {
        const QString id = QLatin1String(system.id);
        osMenu->addAction(icons::system(id), tr(system.label), this, [this, id] { setSystem(id); });
    }
    m_osButton->setMenu(osMenu);
    m_nameRow = new QWidget(this);
    auto *nameLayout = new QHBoxLayout(m_nameRow);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->addWidget(m_name, 1);
    nameLayout->addWidget(m_osButton);
    setSystem(QString());

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
    m_user->setToolTip(tr("Empty: asked in the terminal (login as:), like MobaXterm"));
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("optional"));
    m_password->setToolTip(tr("Empty: asked in the terminal"));
    m_savePasswordBox = new QCheckBox(tr("Remember password (encrypted for your Windows account)"), this);
    m_identity = new QLineEdit(this);
    m_identity->setPlaceholderText(tr("optional; no key is used unless chosen here"));
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

    // Compact layout: fields that go together share a line.
    m_savePasswordBox->setText(tr("Remember password"));
    m_savePasswordBox->setToolTip(tr("Encrypted for your Windows account (DPAPI), never stored in clear text"));
    m_legacyBox->setText(tr("Old devices (SHA-1, CBC, group1)"));
    m_crlfBox->setText(tr("Enter sends CR LF"));
    m_crlfBox->setToolTip(tr("Default: CR only"));
    m_backspaceBox->setText(tr("Backspace sends ^H"));
    m_backspaceBox->setToolTip(tr("Ctrl+H instead of DEL, for devices that need it"));
    m_port->setFixedWidth(90);
    m_dataBits->setMinimumWidth(70);
    m_stopBits->setMinimumWidth(70);

    m_deviceLabel = new QLabel(tr("Device"), this);
    m_typeRow = line({m_type, m_deviceLabel, m_profileRow}, {1, 0, 2});
    m_nameLine = line({m_nameRow, new QLabel(tr("Folder"), this), m_folder}, {3, 0, 2});
    m_hostRow = line({m_host, new QLabel(tr("Port"), this), m_port}, {1, 0, 0});
    m_loginRow = line({m_user, new QLabel(tr("Password"), this), m_password}, {1, 0, 1});
    m_optionsRow = line({m_savePasswordBox, m_legacyBox}, {0, 1});
    m_localArgsRow = line({m_arguments, new QLabel(tr("Start in"), this), m_workingDirectory}, {1, 0, 1});
    m_speedRow = line({m_baudRate, new QLabel(tr("Data bits"), this), m_dataBits, new QLabel(tr("Stop bits"), this),
                       m_stopBits},
                      {2, 0, 1, 0, 1});
    m_parityRow = line({m_parity, new QLabel(tr("Flow control"), this), m_flowControl}, {1, 0, 1});
    m_serialOptionsRow = line({m_crlfBox, m_backspaceBox}, {0, 1});
    m_bottomRow = line({m_colorButton, m_saveBox}, {1, 0});

    m_form = new QFormLayout(this);
    m_form->setVerticalSpacing(8);
    m_form->addRow(tr("Type"), m_typeRow);
    m_form->addRow(tr("Name"), m_nameLine);
    m_form->addRow(tr("Host"), m_hostRow);
    m_form->addRow(tr("Saved user"), m_credentialRow);
    m_form->addRow(tr("User"), m_loginRow);
    m_form->addRow(tr("Private key"), m_identityRow);
    m_form->addRow(QString(), m_optionsRow);
    m_form->addRow(tr("Program"), m_programRow);
    m_form->addRow(tr("Arguments"), m_localArgsRow);
    m_form->addRow(tr("Serial port"), m_serialRow);
    m_form->addRow(tr("Speed (baud)"), m_speedRow);
    m_form->addRow(tr("Parity"), m_parityRow);
    m_form->addRow(QString(), m_serialOptionsRow);
    m_form->addRow(tr("Tab color"), m_bottomRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    fillUsers(QString());
    fillProfiles(QString());
    connect(m_type, &QComboBox::currentIndexChanged, this, [this](int index) {
        // Follow the type while the port is still one of the defaults.
        const int port = m_port->value();
        if (port == 22 || port == 23 || port == 3389) {
            m_port->setValue(index == TelnetIndex ? 23 : index == RdpIndex ? 3389 : 22);
        }
        updateVisibility();
    });
    connect(m_credential, &QComboBox::currentIndexChanged, this, &SessionDialog::updateVisibility);
    connect(m_profile, &QComboBox::activated, this, &SessionDialog::applyProfile);
    updateVisibility();
    setMinimumWidth(620);
    resize(660, sizeHint().height());
}

// One form line holding several fields (and their small labels) side by side.
QWidget *SessionDialog::line(std::initializer_list<QWidget *> widgets, std::initializer_list<int> stretches) {
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto stretch = stretches.begin();
    for (QWidget *w : widgets) {
        if (qobject_cast<QLabel *>(w)) w->setContentsMargins(8, 0, 0, 0);  // a gap before each inner label
        layout->addWidget(w, stretch != stretches.end() ? *stretch++ : 0);
    }
    return row;
}

void SessionDialog::setSystem(const QString &os) {
    const QIcon icon = icons::system(os);
    m_os = icon.isNull() ? QString() : os;
    QString label = tr("None");
    for (const icons::System &system : icons::systems()) {
        if (m_os == QLatin1String(system.id)) label = tr(system.label);
    }
    if (icon.isNull()) {
        m_osButton->setIcon(QIcon());
        m_osButton->setText(QStringLiteral("OS"));  // nothing chosen yet
    } else {
        m_osButton->setIcon(icon);
        m_osButton->setText(QString());
    }
    m_osButton->setToolTip(tr("System icon: %1").arg(label));
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
    const bool rdp = type == RdpIndex;
    const bool remote = type == SshIndex || type == TelnetIndex || rdp;
    const bool login = ssh || rdp;  // user + password fields
    const bool local = type == LocalIndex;
    const bool serial = type == SerialIndex;
    const bool sharedUser = login && !m_credential->currentData().toString().isEmpty();

    m_deviceLabel->setVisible(!local && !rdp);
    m_profileRow->setVisible(!local && !rdp);
    m_osButton->setVisible(!local);
    m_form->setRowVisible(m_hostRow, remote);
    m_form->setRowVisible(m_credentialRow, login);
    m_form->setRowVisible(m_loginRow, login && !sharedUser);
    m_user->setPlaceholderText(rdp ? tr("user or DOMAIN\\user") : tr("optional"));
    m_form->setRowVisible(m_identityRow, ssh && !sharedUser);
    m_savePasswordBox->setVisible(login && !sharedUser);
    m_legacyBox->setVisible(ssh);
    m_form->setRowVisible(m_optionsRow, (login && !sharedUser) || ssh);
    m_form->setRowVisible(m_programRow, local);
    m_form->setRowVisible(m_localArgsRow, local);
    for (QWidget *w : {m_serialRow, m_speedRow, m_parityRow, m_serialOptionsRow}) {
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
                            : s.type == SessionInfo::Type::Rdp    ? RdpIndex
                                                                  : SerialIndex);
    fillProfiles(s.profileId);
    fillUsers(s.credentialId);
    setSystem(s.os);
    m_host->setText(s.host);
    m_port->setValue(s.port);
    m_user->setText(s.user);
    m_keptPassword = s.protectedPassword;
    m_password->setPlaceholderText(s.protectedPassword.isEmpty() ? tr("optional")
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
           : type == RdpIndex      ? SessionInfo::Type::Rdp
                                   : SessionInfo::Type::Serial;
    const bool login = s.type == SessionInfo::Type::Ssh || s.type == SessionInfo::Type::Rdp;
    s.folder = m_folder->currentText();
    s.profileId = s.type == SessionInfo::Type::Local || s.type == SessionInfo::Type::Rdp
                    ? QString()
                    : m_profile->currentData().toString();

    s.host = m_host->text().trimmed();
    s.port = m_port->value();
    if (login) {
        s.credentialId = m_credential->currentData().toString();
    }
    if (s.credentialId.isEmpty()) {
        s.user = m_user->text().trimmed();
        s.identityFile = m_identity->text().trimmed();
        if (const qsizetype at = s.host.indexOf(QLatin1Char('@')); at > 0) {
            s.user = s.host.left(at);
            s.host = s.host.mid(at + 1);
        }
        if (login && m_savePasswordBox->isChecked()) {
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
    s.os = s.type == SessionInfo::Type::Local ? QString() : m_os;

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

void SessionDialog::setType(SessionInfo::Type type) {
    m_type->setCurrentIndex(type == SessionInfo::Type::Telnet  ? TelnetIndex
                            : type == SessionInfo::Type::Local ? LocalIndex
                            : type == SessionInfo::Type::Serial ? SerialIndex
                            : type == SessionInfo::Type::Rdp    ? RdpIndex
                                                                : SshIndex);
}

void SessionDialog::setSaveRequested(bool save) {
    m_saveBox->setChecked(save);
}

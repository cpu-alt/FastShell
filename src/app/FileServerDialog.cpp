// Copyright 2026 FastShell contributors. Apache-2.0.
#include "app/FileServerDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

FileServer::Config FileServerDialog::savedConfig(const QString &folder) {
    QSettings s;
    FileServer::Config c;
    c.root = folder.isEmpty() ? s.value(QStringLiteral("files/root"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString()
                              : folder;
    const QString address = s.value(QStringLiteral("files/address")).toString();
    c.address = address.isEmpty() ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(address);
    c.tftpPort = quint16(s.value(QStringLiteral("files/tftpPort"), 69).toInt());
    c.http = s.value(QStringLiteral("files/http"), false).toBool();
    c.httpPort = quint16(s.value(QStringLiteral("files/httpPort"), 8080).toInt());
    c.allowUpload = s.value(QStringLiteral("files/upload"), false).toBool();
    if (c.tftpPort == 0) c.tftpPort = 69;
    if (c.httpPort == 0) c.httpPort = 8080;
    return c;
}

void FileServerDialog::saveConfig(const FileServer::Config &c) {
    QSettings s;
    s.setValue(QStringLiteral("files/root"), c.root);
    s.setValue(QStringLiteral("files/address"),
               c.address == QHostAddress(QHostAddress::AnyIPv4) ? QString() : c.address.toString());
    s.setValue(QStringLiteral("files/tftpPort"), c.tftpPort);
    s.setValue(QStringLiteral("files/http"), c.http);
    s.setValue(QStringLiteral("files/httpPort"), c.httpPort);
    s.setValue(QStringLiteral("files/upload"), c.allowUpload);
}

bool FileServerDialog::start(FileServer *server, const FileServer::Config &config, QWidget *parent) {
    QString error;
    if (server->start(config, &error)) return true;
    QMessageBox::warning(parent, tr("File server"),
                         tr("%1\n\nAnother TFTP server (tftpd64, SolarWinds...) may already use this port: close it, "
                            "or choose another interface.").arg(error));
    return false;
}

FileServerDialog::FileServerDialog(FileServer *server, QWidget *parent)
    : QDialog(parent), m_server(server), m_timer(new QTimer(this)) {
    setWindowTitle(tr("File server (TFTP / HTTP)"));
    setAttribute(Qt::WA_DeleteOnClose);
    const FileServer::Config c = savedConfig();

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Sends files from a folder of this PC to network devices, for example a firmware: "
                                "<i>copy tftp://PC/file flash:</i>. Read only unless uploads are allowed."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    m_folder = new QLineEdit(QDir::toNativeSeparators(c.root), this);
    auto *browse = new QPushButton(tr("Browse..."), this);
    auto *open = new QPushButton(tr("Open"), this);
    auto *folderLine = new QHBoxLayout;
    folderLine->addWidget(m_folder, 1);
    folderLine->addWidget(browse);
    folderLine->addWidget(open);
    form->addRow(tr("Folder:"), folderLine);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder to share"), m_folder->text());
        if (!dir.isEmpty()) m_folder->setText(QDir::toNativeSeparators(dir));
    });
    connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder->text())); });

    m_address = new QComboBox(this);
    m_address->addItem(tr("All interfaces"), QHostAddress(QHostAddress::AnyIPv4).toString());
    for (const auto &[name, address] : FileServer::localAddresses()) {
        m_address->addItem(QStringLiteral("%1  (%2)").arg(address.toString(), name), address.toString());
    }
    const int index = m_address->findData(c.address.toString());
    if (index >= 0) m_address->setCurrentIndex(index);
    form->addRow(tr("Interface:"), m_address);

    m_tftpPort = new QSpinBox(this);
    m_tftpPort->setRange(1, 65535);
    m_tftpPort->setValue(c.tftpPort);
    m_http = new QCheckBox(tr("Also HTTP, port"), this);
    m_http->setChecked(c.http);
    m_httpPort = new QSpinBox(this);
    m_httpPort->setRange(1, 65535);
    m_httpPort->setValue(c.httpPort);
    auto *ports = new QHBoxLayout;
    ports->addWidget(m_tftpPort);
    ports->addSpacing(16);
    ports->addWidget(m_http);
    ports->addWidget(m_httpPort);
    ports->addStretch(1);
    form->addRow(tr("TFTP port:"), ports);
    m_upload = new QCheckBox(tr("Allow uploads to this folder (new files only, e.g. copy running-config tftp:)"), this);
    m_upload->setChecked(c.allowUpload);
    form->addRow(QString(), m_upload);
    layout->addLayout(form);

    m_startStop = new QPushButton(this);
    m_startStop->setDefault(true);
    connect(m_startStop, &QPushButton::clicked, this, &FileServerDialog::toggle);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *line = new QHBoxLayout;
    line->addWidget(m_startStop);
    line->addWidget(m_hint, 1);
    layout->addLayout(line);

    m_list = new QTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setHeaderLabels({tr("File"), tr("Device"), tr("Protocol"), tr("Progress"), tr("Size"), tr("State")});
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->setMinimumHeight(140);
    layout->addWidget(m_list, 1);
    resize(720, 440);

    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &FileServerDialog::refresh);
    connect(m_server, &FileServer::runningChanged, this, &FileServerDialog::refresh);
    connect(m_server, &FileServer::transferStarted, this, &FileServerDialog::refresh);
    connect(m_server, &FileServer::transferFinished, this, &FileServerDialog::refresh);
    m_timer->start();
    refresh();
}

FileServer::Config FileServerDialog::fromFields() const {
    FileServer::Config c;
    c.root = QDir::fromNativeSeparators(m_folder->text().trimmed());
    c.address = QHostAddress(m_address->currentData().toString());
    c.tftpPort = quint16(m_tftpPort->value());
    c.http = m_http->isChecked();
    c.httpPort = quint16(m_httpPort->value());
    c.allowUpload = m_upload->isChecked();
    return c;
}

void FileServerDialog::toggle() {
    if (m_server->isRunning()) {
        if (m_server->runningTransfers() > 0 &&
            QMessageBox::question(this, tr("File server"), tr("A transfer is running. Stop it?")) != QMessageBox::Yes) {
            return;
        }
        m_server->stop();
    } else {
        const FileServer::Config c = fromFields();
        saveConfig(c);
        start(m_server, c, this);
    }
    refresh();
}

void FileServerDialog::refresh() {
    const bool running = m_server->isRunning();
    m_startStop->setText(running ? tr("Stop") : tr("Start"));
    for (QWidget *w : std::initializer_list<QWidget *>{m_folder, m_address, m_tftpPort, m_http, m_httpPort, m_upload}) w->setEnabled(!running);
    if (running) {
        const FileServer::Config &c = m_server->config();
        QString ip = c.address.toString();
        if (c.address == QHostAddress(QHostAddress::AnyIPv4)) {
            const auto addresses = FileServer::localAddresses();
            ip = addresses.isEmpty() ? QStringLiteral("<PC>") : addresses.first().second.toString();
        }
        QString hint = tr("Running. On the switch: copy tftp://%1/<file> flash:").arg(ip);
        if (c.http) hint += tr("   Browser: http://%1:%2/").arg(ip).arg(m_server->httpPort());
        m_hint->setText(hint);
    } else {
        m_hint->setText(tr("Stopped. Windows may ask once to allow FastShell through the firewall: allow it for the "
                           "network of the devices."));
    }

    const QList<FileServer::Transfer> transfers = m_server->transfers();
    m_list->clear();
    for (auto it = transfers.crbegin(); it != transfers.crend(); ++it) {
        const FileServer::Transfer &t = *it;
        QString state;
        switch (t.state) {
        case FileServer::Transfer::State::Running: state = tr("Running"); break;
        case FileServer::Transfer::State::Done: state = tr("Done (%1 s)").arg(std::max<qint64>(1, t.started.secsTo(t.finished))); break;
        case FileServer::Transfer::State::Failed: state = tr("Failed: %1").arg(t.error); break;
        }
        const QString progress = t.total > 0 ? QStringLiteral("%1 %").arg(t.bytes * 100 / t.total) : FileServer::sizeText(t.bytes);
        auto *item = new QTreeWidgetItem(m_list, {t.file, (t.upload ? QStringLiteral("<- ") : QStringLiteral("-> ")) + t.peer,
                                                  t.protocol.toUpper(), progress, FileServer::sizeText(t.total), state});
        item->setToolTip(5, state);
    }
}

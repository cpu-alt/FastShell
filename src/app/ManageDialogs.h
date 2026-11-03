// Copyright 2026 EMF Hexa contributors. Apache-2.0.
//
// Editors for the shared users and the device profiles (Cisco, FS...).
#pragma once

#include <QDialog>

#include "app/SessionStore.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;

// Shared user accounts, picked from the session dialog instead of typing
// the same login on every session.
class UsersDialog : public QDialog {
    Q_OBJECT

public:
    UsersDialog(SessionStore *store, QWidget *parent = nullptr);
    QString selectedId() const { return m_currentId; }

private:
    void reload(const QString &select = QString());
    void showCredential(const QString &id);
    void saveCurrent();

    SessionStore *m_store;
    QString m_currentId;
    QListWidget *m_list;
    QLineEdit *m_name;
    QLineEdit *m_user;
    QLineEdit *m_password;
    QLineEdit *m_identity;
    QPushButton *m_delete;
    QByteArray m_keptPassword;
};

// Device families: a tag shown in the session tree and the serial settings
// they need. SSH is left untouched, as PuTTY does.
class ProfilesDialog : public QDialog {
    Q_OBJECT

public:
    ProfilesDialog(SessionStore *store, QWidget *parent = nullptr);

private:
    void reload(const QString &select = QString());
    void showProfile(const QString &id);
    void saveCurrent();

    SessionStore *m_store;
    QString m_currentId;
    QColor m_color;
    QListWidget *m_list;
    QLineEdit *m_name;
    QLineEdit *m_tag;
    QPushButton *m_colorButton;
    QComboBox *m_baudRate;
    QComboBox *m_dataBits;
    QComboBox *m_parity;
    QComboBox *m_stopBits;
    QComboBox *m_flowControl;
    QCheckBox *m_crlf;
    QCheckBox *m_backspace;
    QPushButton *m_delete;
};

// Shared helpers for the serial widgets (also used by the session dialog).
namespace serialui {
QComboBox *baudCombo(QWidget *parent);
QComboBox *dataBitsCombo(QWidget *parent);
QComboBox *parityCombo(QWidget *parent);
QComboBox *stopBitsCombo(QWidget *parent);
QComboBox *flowCombo(QWidget *parent);
void selectData(QComboBox *combo, const QString &value);
}  // namespace serialui

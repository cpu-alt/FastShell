// Copyright 2026 EMF Hexa contributors. Apache-2.0.
#pragma once

#include <QDialog>

#include "app/SessionStore.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class SessionDialog : public QDialog {
    Q_OBJECT

public:
    SessionDialog(SessionStore *store, QWidget *parent = nullptr);

    void setSession(const SessionInfo &session);
    SessionInfo session() const;
    bool saveRequested() const;
    QString typedPassword() const;  // also used when it is not saved
    void setSaveRequested(bool save);

private:
    void updateVisibility();
    void fillUsers(const QString &select);
    void fillProfiles(const QString &select);
    void applyProfile();
    int typeIndex() const;

    SessionStore *m_store;
    QString m_id;
    QFormLayout *m_form;
    QLineEdit *m_name;
    QComboBox *m_folder;
    QComboBox *m_type;
    QComboBox *m_profile;
    QWidget *m_profileRow;

    QLineEdit *m_host;
    QSpinBox *m_port;
    QComboBox *m_credential;
    QWidget *m_credentialRow;
    QLineEdit *m_user;
    QLineEdit *m_password;
    QCheckBox *m_savePasswordBox;
    QLineEdit *m_identity;
    QWidget *m_identityRow;
    QCheckBox *m_legacyBox;

    QLineEdit *m_program;
    QWidget *m_programRow;
    QLineEdit *m_arguments;
    QLineEdit *m_workingDirectory;

    QWidget *m_serialRow;
    QComboBox *m_serialPort;
    QComboBox *m_baudRate;
    QComboBox *m_dataBits;
    QComboBox *m_parity;
    QComboBox *m_stopBits;
    QComboBox *m_flowControl;
    QCheckBox *m_crlfBox;
    QCheckBox *m_backspaceBox;

    QPushButton *m_colorButton;
    QColor m_color;
    QCheckBox *m_saveBox;
    QByteArray m_keptPassword;
};

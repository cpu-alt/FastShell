// Copyright 2026 FastShell contributors. Apache-2.0.
#pragma once

#include <QDialog>

#include <initializer_list>

#include "app/SessionStore.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

class SessionDialog : public QDialog {
    Q_OBJECT

public:
    SessionDialog(SessionStore *store, QWidget *parent = nullptr);

    void setSession(const SessionInfo &session);
    SessionInfo session() const;
    bool saveRequested() const;
    QString typedPassword() const;  // also used when it is not saved
    void setSaveRequested(bool save);
    void setType(SessionInfo::Type type);  // for a new session

private:
    void updateVisibility();
    void fillUsers(const QString &select);
    void fillProfiles(const QString &select);
    void applyProfile();
    int typeIndex() const;
    QWidget *line(std::initializer_list<QWidget *> widgets, std::initializer_list<int> stretches);

    // Form lines holding several fields side by side.
    QLabel *m_deviceLabel;
    QWidget *m_typeRow;
    QWidget *m_nameLine;
    QWidget *m_hostRow;
    QWidget *m_loginRow;
    QWidget *m_optionsRow;
    QWidget *m_localArgsRow;
    QWidget *m_speedRow;
    QWidget *m_parityRow;
    QWidget *m_serialOptionsRow;
    QWidget *m_bottomRow;

    SessionStore *m_store;
    QString m_id;
    QFormLayout *m_form;
    QLineEdit *m_name;
    QComboBox *m_folder;
    QComboBox *m_type;
    QComboBox *m_profile;
    QToolButton *m_osButton;  // system icon, next to the name
    QString m_os;
    QWidget *m_nameRow;
    void setSystem(const QString &os);
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

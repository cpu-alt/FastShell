/****************************************************************************
** Meta object code from reading C++ file 'SessionPanel.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/app/SessionPanel.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'SessionPanel.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN12SessionPanelE_t {};
} // unnamed namespace

template <> constexpr inline auto SessionPanel::qt_create_metaobjectdata<qt_meta_tag_ZN12SessionPanelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "SessionPanel",
        "openRequested",
        "",
        "SessionInfo",
        "session",
        "password",
        "openInSplitRequested",
        "Qt::Orientation",
        "orientation",
        "openSideBySideRequested",
        "QList<SessionInfo>",
        "sessions"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'openRequested'
        QtMocHelpers::SignalData<void(const SessionInfo &, const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { QMetaType::QString, 5 },
        }}),
        // Signal 'openInSplitRequested'
        QtMocHelpers::SignalData<void(const SessionInfo &, Qt::Orientation)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { 0x80000000 | 7, 8 },
        }}),
        // Signal 'openSideBySideRequested'
        QtMocHelpers::SignalData<void(const QList<SessionInfo> &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 10, 11 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SessionPanel, qt_meta_tag_ZN12SessionPanelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject SessionPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12SessionPanelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12SessionPanelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12SessionPanelE_t>.metaTypes,
    nullptr
} };

void SessionPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SessionPanel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->openRequested((*reinterpret_cast<std::add_pointer_t<SessionInfo>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 1: _t->openInSplitRequested((*reinterpret_cast<std::add_pointer_t<SessionInfo>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<Qt::Orientation>>(_a[2]))); break;
        case 2: _t->openSideBySideRequested((*reinterpret_cast<std::add_pointer_t<QList<SessionInfo>>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (SessionPanel::*)(const SessionInfo & , const QString & )>(_a, &SessionPanel::openRequested, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (SessionPanel::*)(const SessionInfo & , Qt::Orientation )>(_a, &SessionPanel::openInSplitRequested, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (SessionPanel::*)(const QList<SessionInfo> & )>(_a, &SessionPanel::openSideBySideRequested, 2))
            return;
    }
}

const QMetaObject *SessionPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SessionPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12SessionPanelE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int SessionPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void SessionPanel::openRequested(const SessionInfo & _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void SessionPanel::openInSplitRequested(const SessionInfo & _t1, Qt::Orientation _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void SessionPanel::openSideBySideRequested(const QList<SessionInfo> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}
QT_WARNING_POP

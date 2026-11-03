/****************************************************************************
** Meta object code from reading C++ file 'SftpClient.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/net/SftpClient.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'SftpClient.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10SftpClientE_t {};
} // unnamed namespace

template <> constexpr inline auto SftpClient::qt_create_metaobjectdata<qt_meta_tag_ZN10SftpClientE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "SftpClient",
        "opened",
        "",
        "homePath",
        "listed",
        "path",
        "QList<RemoteEntry>",
        "entries",
        "failed",
        "message",
        "changed",
        "directory",
        "transferStarted",
        "id",
        "name",
        "upload",
        "size",
        "transferProgress",
        "done",
        "total",
        "transferFinished",
        "ok"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'opened'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'listed'
        QtMocHelpers::SignalData<void(const QString &, const QList<RemoteEntry> &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { 0x80000000 | 6, 7 },
        }}),
        // Signal 'failed'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Signal 'changed'
        QtMocHelpers::SignalData<void(const QString &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 },
        }}),
        // Signal 'transferStarted'
        QtMocHelpers::SignalData<void(quint64, const QString &, bool, qint64)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 13 }, { QMetaType::QString, 14 }, { QMetaType::Bool, 15 }, { QMetaType::LongLong, 16 },
        }}),
        // Signal 'transferProgress'
        QtMocHelpers::SignalData<void(quint64, qint64, qint64)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 13 }, { QMetaType::LongLong, 18 }, { QMetaType::LongLong, 19 },
        }}),
        // Signal 'transferFinished'
        QtMocHelpers::SignalData<void(quint64, bool, const QString &)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 13 }, { QMetaType::Bool, 21 }, { QMetaType::QString, 9 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SftpClient, qt_meta_tag_ZN10SftpClientE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject SftpClient::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10SftpClientE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10SftpClientE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10SftpClientE_t>.metaTypes,
    nullptr
} };

void SftpClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SftpClient *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->opened((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->listed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QList<RemoteEntry>>>(_a[2]))); break;
        case 2: _t->failed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->changed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->transferStarted((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[4]))); break;
        case 5: _t->transferProgress((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3]))); break;
        case 6: _t->transferFinished((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<RemoteEntry> >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (SftpClient::*)(const QString & )>(_a, &SftpClient::opened, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (SftpClient::*)(const QString & , const QList<RemoteEntry> & )>(_a, &SftpClient::listed, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (SftpClient::*)(const QString & )>(_a, &SftpClient::failed, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (SftpClient::*)(const QString & )>(_a, &SftpClient::changed, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (SftpClient::*)(quint64 , const QString & , bool , qint64 )>(_a, &SftpClient::transferStarted, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (SftpClient::*)(quint64 , qint64 , qint64 )>(_a, &SftpClient::transferProgress, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (SftpClient::*)(quint64 , bool , const QString & )>(_a, &SftpClient::transferFinished, 6))
            return;
    }
}

const QMetaObject *SftpClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SftpClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10SftpClientE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int SftpClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void SftpClient::opened(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void SftpClient::listed(const QString & _t1, const QList<RemoteEntry> & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void SftpClient::failed(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void SftpClient::changed(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void SftpClient::transferStarted(quint64 _t1, const QString & _t2, bool _t3, qint64 _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 5
void SftpClient::transferProgress(quint64 _t1, qint64 _t2, qint64 _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2, _t3);
}

// SIGNAL 6
void SftpClient::transferFinished(quint64 _t1, bool _t2, const QString & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2, _t3);
}
QT_WARNING_POP

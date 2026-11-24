// Copyright 2026 FastShell contributors. Apache-2.0.
//
// Small animations: a padlock that opens over a terminal when the vault logs
// it in, confetti when an account is added, and the vault door drawn in the
// empty Vault panel. All of them can be turned off (View > Animations).
#pragma once

#include <QRectF>
#include <QString>

class QPainter;
class QWidget;

namespace fun {

// View > Animations (setting appearance/animations, on by default).
bool enabled();
void setEnabled(bool on);

// A padlock that opens, with the account name, over widget (about 1.4 s).
void unlock(QWidget *widget, const QString &text);
// A burst of confetti over widget (about 2 s).
void confetti(QWidget *widget);

// A vault door in rect: round door, bolts, and a dial turned by dialTurn
// (0..1 = one turn), in the current theme colours.
void paintVaultDoor(QPainter &p, const QRectF &rect, qreal dialTurn);

}  // namespace fun

// Copyright 2026 EMF Hexa contributors. Apache-2.0.
//
// Generates assets/emfhexa.ico (Windows executable icon) and docs/logo.png
// from the same drawing code the application uses.
//   make_icon <ico path> <png path>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>

#include "app/Logo.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    if (argc < 3) return 2;

    // The ICO writer stores one image; use 256 px, Windows scales it down well.
    QImage big = logo::pixmap(256).toImage();
    QImageWriter ico(QString::fromLocal8Bit(argv[1]), "ico");
    if (!ico.write(big)) return 1;

    return logo::pixmap(512).toImage().save(QString::fromLocal8Bit(argv[2]), "png") ? 0 : 1;
}

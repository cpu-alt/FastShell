// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#pragma once

#include <QColor>
#include <array>
#include <cstdint>

#include "terminal/Screen.h"

struct Theme {
    QColor foreground{0xD4, 0xD7, 0xDD};
    QColor background{0x1B, 0x1D, 0x23};
    QColor cursor{0x5F, 0xB3, 0xFF};
    QColor selection{0x2F, 0x4F, 0x7A};
    std::array<QColor, 16> ansi{{
        {0x1B, 0x1D, 0x23}, {0xE0, 0x6C, 0x75}, {0x98, 0xC3, 0x79}, {0xE5, 0xC0, 0x7B},
        {0x61, 0xAF, 0xEF}, {0xC6, 0x78, 0xDD}, {0x56, 0xB6, 0xC2}, {0xAB, 0xB2, 0xBF},
        {0x5C, 0x63, 0x70}, {0xFF, 0x7B, 0x86}, {0xB5, 0xE8, 0x90}, {0xFF, 0xD6, 0x8A},
        {0x7D, 0xC4, 0xFF}, {0xE0, 0x9A, 0xF5}, {0x6F, 0xD6, 0xE0}, {0xFF, 0xFF, 0xFF},
    }};

    QColor indexed(int i) const {
        if (i < 16) {
            return ansi[std::size_t(i)];
        }
        if (i < 232) {  // 6x6x6 color cube
            static constexpr int levels[] = {0, 95, 135, 175, 215, 255};
            i -= 16;
            return QColor(levels[i / 36], levels[(i / 6) % 6], levels[i % 6]);
        }
        const int gray = 8 + (i - 232) * 10;
        return QColor(gray, gray, gray);
    }

    QColor resolve(std::uint32_t c, bool isForeground, bool bold) const {
        if (color::isRgb(c)) {
            return QColor::fromRgb(QRgb(c & 0xFFFFFF));
        }
        if (color::isIndexed(c)) {
            int i = int(c & 0xFF);
            if (bold && isForeground && i < 8) {
                i += 8;  // bold as bright, like most terminals
            }
            return indexed(i);
        }
        return isForeground ? foreground : background;
    }
};

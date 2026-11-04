// Copyright 2026 FastShell contributors. Apache-2.0.
#pragma once

// Number of terminal columns a code point occupies: 0 (combining marks and
// zero-width characters), 1, or 2 (East Asian wide characters and emoji).
int charWidth(char32_t codePoint);

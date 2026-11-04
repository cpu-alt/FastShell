// Copyright 2026 FastShell contributors. Apache-2.0.
#include "terminal/VtParser.h"

namespace {
constexpr std::uint8_t ESC = 0x1B;
constexpr std::uint8_t BEL = 0x07;
constexpr std::uint8_t CAN = 0x18;
constexpr std::uint8_t SUB = 0x1A;
constexpr std::size_t MaxOscLength = 64 * 1024;
constexpr char32_t Replacement = 0xFFFD;
}  // namespace

VtParser::VtParser(VtHandler &handler) : m_handler(handler) {
    m_print.reserve(4096);
}

void VtParser::reset() {
    m_state = State::Ground;
    m_print.clear();
    m_utf8Remaining = 0;
    clearSequence();
}

void VtParser::clearSequence() {
    m_intermediates.clear();
    m_params = {};
    m_paramStarted = false;
}

void VtParser::flushPrint() {
    if (!m_print.empty()) {
        m_handler.print(m_print.data(), m_print.size());
        m_print.clear();
    }
}

void VtParser::decodeUtf8(std::uint8_t byte) {
    if (m_utf8Remaining > 0) {
        if ((byte & 0xC0) == 0x80) {
            m_utf8CodePoint = (m_utf8CodePoint << 6) | (byte & 0x3F);
            if (--m_utf8Remaining == 0) {
                const char32_t cp = m_utf8CodePoint;
                const bool invalid = cp < m_utf8Min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF);
                m_print.push_back(invalid ? Replacement : cp);
            }
            return;
        }
        // Truncated sequence: emit a replacement and reprocess this byte.
        m_utf8Remaining = 0;
        m_print.push_back(Replacement);
    }

    if (byte < 0x80) {
        m_print.push_back(byte);
    } else if ((byte & 0xE0) == 0xC0) {
        m_utf8CodePoint = byte & 0x1F; m_utf8Remaining = 1; m_utf8Min = 0x80;
    } else if ((byte & 0xF0) == 0xE0) {
        m_utf8CodePoint = byte & 0x0F; m_utf8Remaining = 2; m_utf8Min = 0x800;
    } else if ((byte & 0xF8) == 0xF0) {
        m_utf8CodePoint = byte & 0x07; m_utf8Remaining = 3; m_utf8Min = 0x10000;
    } else {
        m_print.push_back(Replacement);
    }
}

void VtParser::csiByte(std::uint8_t byte) {
    if (byte >= '0' && byte <= '9') {
        if (m_state == State::CsiIntermediate) {
            m_state = State::CsiIgnore;
            return;
        }
        m_state = State::CsiParam;
        if (m_params.count == 0) {
            m_params.count = 1;
        }
        int &value = m_params.values[m_params.count - 1];
        value = value > 100000 ? value : value * 10 + (byte - '0');
        m_paramStarted = true;
    } else if (byte == ';' || byte == ':') {
        if (m_state == State::CsiIntermediate) {
            m_state = State::CsiIgnore;
            return;
        }
        m_state = State::CsiParam;
        if (m_params.count == 0) {
            m_params.count = 1;  // leading ';' means an empty first parameter
        }
        if (m_params.count < VtParams::MaxParams) {
            m_params.values[m_params.count++] = 0;
        }
    } else if (byte >= 0x3C && byte <= 0x3F) {
        // Private marker, only valid before any parameter.
        if (m_state == State::CsiEntry) {
            m_intermediates.push_back(char(byte));
        } else {
            m_state = State::CsiIgnore;
        }
    } else if (byte >= 0x20 && byte <= 0x2F) {
        m_intermediates.push_back(char(byte));
        m_state = State::CsiIntermediate;
    } else if (byte >= 0x40 && byte <= 0x7E) {
        m_handler.csiDispatch(m_params, m_intermediates, char(byte));
        m_state = State::Ground;
    }
}

void VtParser::feed(const char *data, std::size_t length) {
    const auto *p = reinterpret_cast<const std::uint8_t *>(data);
    const auto *end = p + length;

    while (p < end) {
        const std::uint8_t byte = *p;

        if (m_state == State::Ground) {
            // Fast path: copy a run of printable ASCII directly.
            if (byte >= 0x20 && byte < 0x7F && m_utf8Remaining == 0) {
                const auto *run = p;
                while (p < end && *p >= 0x20 && *p < 0x7F) {
                    ++p;
                }
                m_print.insert(m_print.end(), run, p);
                continue;
            }
            ++p;
            if (byte >= 0x80 || (byte >= 0x20 && byte < 0x7F)) {
                decodeUtf8(byte);
            } else if (byte == ESC) {
                flushPrint();
                clearSequence();
                m_state = State::Escape;
            } else if (byte < 0x20) {
                if (m_utf8Remaining > 0) {
                    m_utf8Remaining = 0;
                    m_print.push_back(Replacement);
                }
                flushPrint();
                m_handler.execute(byte);
            }
            // DEL (0x7F) is ignored.
            continue;
        }

        ++p;

        // These work from any state.
        if (byte == CAN || byte == SUB) {
            m_state = State::Ground;
            continue;
        }
        if (byte == ESC && m_state != State::Osc && m_state != State::StringIgnore) {
            clearSequence();
            m_state = State::Escape;
            continue;
        }

        switch (m_state) {
        case State::Ground:
            break;

        case State::Escape:
            if (byte < 0x20) {
                m_handler.execute(byte);
            } else if (byte == '[') {
                m_state = State::CsiEntry;
            } else if (byte == ']') {
                m_osc.clear();
                m_state = State::Osc;
            } else if (byte == 'P' || byte == 'X' || byte == '^' || byte == '_') {
                m_state = State::StringIgnore;  // DCS, SOS, PM, APC
            } else if (byte >= 0x20 && byte <= 0x2F) {
                m_intermediates.push_back(char(byte));
                m_state = State::EscapeIntermediate;
            } else if (byte >= 0x30 && byte <= 0x7E) {
                m_handler.escDispatch(m_intermediates, char(byte));
                m_state = State::Ground;
            }
            break;

        case State::EscapeIntermediate:
            if (byte < 0x20) {
                m_handler.execute(byte);
            } else if (byte <= 0x2F) {
                m_intermediates.push_back(char(byte));
            } else if (byte <= 0x7E) {
                m_handler.escDispatch(m_intermediates, char(byte));
                m_state = State::Ground;
            }
            break;

        case State::CsiEntry:
        case State::CsiParam:
        case State::CsiIntermediate:
            if (byte < 0x20) {
                m_handler.execute(byte);  // C0 controls run inside CSI
            } else {
                csiByte(byte);
            }
            break;

        case State::CsiIgnore:
            if (byte < 0x20) {
                m_handler.execute(byte);
            } else if (byte >= 0x40 && byte <= 0x7E) {
                m_state = State::Ground;
            }
            break;

        case State::Osc:
            if (byte == BEL) {
                m_handler.oscDispatch(m_osc);
                m_state = State::Ground;
            } else if (byte == ESC) {
                m_state = State::OscEscape;
            } else if (byte >= 0x20 && m_osc.size() < MaxOscLength) {
                m_osc.push_back(char(byte));
            }
            break;

        case State::OscEscape:
            // ESC \ (ST) ends the string; anything else aborts it.
            if (byte == '\\') {
                m_handler.oscDispatch(m_osc);
                m_state = State::Ground;
            } else {
                clearSequence();
                m_state = State::Escape;
                --p;  // reprocess as the byte after ESC
            }
            break;

        case State::StringIgnore:
            if (byte == ESC) {
                m_state = State::StringIgnoreEscape;
            } else if (byte == BEL) {
                m_state = State::Ground;
            }
            break;

        case State::StringIgnoreEscape:
            m_state = (byte == '\\') ? State::Ground : State::StringIgnore;
            break;
        }
    }
    flushPrint();
}

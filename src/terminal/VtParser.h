// Copyright 2026 WindTerm NG contributors. Apache-2.0.
//
// VT500/xterm escape-sequence parser (state machine after Paul Williams'
// DEC parser). Decodes UTF-8 and hands printable text to the handler in
// runs, so plain output needs one virtual call per run instead of per byte.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct VtParams {
    static constexpr int MaxParams = 32;
    std::array<int, MaxParams> values{};
    int count = 0;

    // Value of parameter i, or fallback when missing or zero.
    int get(int i, int fallback) const {
        return (i < count && values[i] > 0) ? values[i] : fallback;
    }
    // Raw value (0 when missing).
    int raw(int i) const { return i < count ? values[i] : 0; }
};

class VtHandler {
public:
    virtual ~VtHandler() = default;
    virtual void print(const char32_t *text, std::size_t length) = 0;
    virtual void execute(std::uint8_t control) = 0;
    virtual void escDispatch(std::string_view intermediates, char finalByte) = 0;
    // intermediates holds a private marker ("?", ">", "=") and/or " ", "!", ...
    virtual void csiDispatch(const VtParams &params, std::string_view intermediates, char finalByte) = 0;
    virtual void oscDispatch(std::string_view data) = 0;
};

class VtParser {
public:
    explicit VtParser(VtHandler &handler);

    void feed(const char *data, std::size_t length);
    void reset();

private:
    enum class State : std::uint8_t {
        Ground, Escape, EscapeIntermediate, CsiEntry, CsiParam, CsiIntermediate, CsiIgnore,
        Osc, OscEscape, StringIgnore, StringIgnoreEscape,
    };

    void flushPrint();
    void decodeUtf8(std::uint8_t byte);
    void clearSequence();
    void csiByte(std::uint8_t byte);

    VtHandler &m_handler;
    State m_state = State::Ground;
    std::vector<char32_t> m_print;
    std::string m_intermediates;
    std::string m_osc;
    VtParams m_params;
    bool m_paramStarted = false;
    char32_t m_utf8CodePoint = 0;
    int m_utf8Remaining = 0;
    char32_t m_utf8Min = 0;
};

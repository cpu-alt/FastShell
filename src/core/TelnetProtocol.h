// Copyright 2020 WindTerm, 2026 WindTerm NG contributors. Apache-2.0.
//
// Telnet protocol (RFC 854/855) with a working option-negotiation engine.
// The original component only had name tables; this adds the parser that
// strips commands from the data stream and answers the server.
#pragma once

#include <QByteArray>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace telnet {

enum Command : std::uint8_t {
    XEOF = 236, SUSP = 237, ABORT = 238, EOR = 239, SE = 240, NOP = 241, DM = 242,
    BREAK = 243, IP = 244, AO = 245, AYT = 246, EC = 247, EL = 248, GA = 249,
    SB = 250, WILL = 251, WONT = 252, DO = 253, DONT = 254, IAC = 255,
};

enum Option : std::uint8_t {
    BINARY = 0, ECHO = 1, SGA = 3, STATUS = 5, TIMING_MARK = 6, TTYPE = 24,
    EOR_OPT = 25, NAWS = 31, TSPEED = 32, LFLOW = 33, LINEMODE = 34,
    NEW_ENVIRON = 39, CHARSET = 42, EXOPL = 255,
};

enum SubOption : std::uint8_t { IS = 0, SEND = 1 };

std::string commandName(std::uint8_t command);
std::string optionName(std::uint8_t option);

// Client-side engine. Feed it bytes from the socket; it returns the plain
// terminal data and writes any protocol replies through the send callback.
class Engine {
public:
    using SendFunction = std::function<void(const QByteArray &)>;

    explicit Engine(SendFunction send, QByteArray terminalType = "XTERM-256COLOR");

    // Sends our initial offers (NAWS, TTYPE, SGA). Call once after connect.
    void begin();
    QByteArray receive(const QByteArray &input);
    // Escapes IAC and, outside binary mode, turns a lone CR into CR NUL.
    QByteArray encode(const QByteArray &output) const;
    void setWindowSize(int columns, int rows);

    bool remoteEcho() const { return m_remote[ECHO]; }
    bool binaryOut() const { return m_local[BINARY]; }
    bool localEnabled(std::uint8_t option) const { return m_local[option]; }
    bool remoteEnabled(std::uint8_t option) const { return m_remote[option]; }

private:
    enum class State { Data, Iac, Will, Wont, Do, Dont, Sb, SbIac, Cr };

    void handleWill(std::uint8_t option);
    void handleWont(std::uint8_t option);
    void handleDo(std::uint8_t option);
    void handleDont(std::uint8_t option);
    void handleSubnegotiation();
    void sendCommand(std::uint8_t command, std::uint8_t option);
    void sendWindowSize();

    static bool acceptRemote(std::uint8_t option);
    static bool acceptLocal(std::uint8_t option);

    SendFunction m_send;
    QByteArray m_terminalType;
    State m_state = State::Data;
    QByteArray m_subnegotiation;
    std::array<bool, 256> m_local{};   // options we perform (WILL)
    std::array<bool, 256> m_remote{};  // options the server performs (DO)
    std::array<bool, 256> m_localRequested{};
    std::array<bool, 256> m_remoteRequested{};
    int m_columns = 80;
    int m_rows = 24;
};

}  // namespace telnet

// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
#include "core/TelnetProtocol.h"

#include <cstdio>
#include <utility>

namespace telnet {

namespace {

constexpr const char *CommandNames[] = {
    "XEOF", "SUSP", "ABORT", "EOR", "SE", "NOP", "DM", "BREAK", "IP", "AO",
    "AYT", "EC", "EL", "GA", "SB", "WILL", "WONT", "DO", "DONT", "IAC",
};

constexpr const char *OptionNames[] = {
    "BINARY", "ECHO", "RCP", "SUPPRESS GO AHEAD", "NAME", "STATUS", "TIMING MARK", "RCTE",
    "NAOL", "NAOP", "NAOCRD", "NAOHTS", "NAOHTD", "NAOFFD", "NAOVTS", "NAOVTD",
    "NAOLFD", "EXTEND ASCII", "LOGOUT", "BYTE MACRO", "DATA ENTRY TERMINAL", "SUPDUP", "SUPDUP OUTPUT", "SEND LOCATION",
    "TERMINAL TYPE", "END OF RECORD", "TACACS UID", "OUTPUT MARKING", "TTYLOC", "3270 REGIME", "X.3 PAD", "NAWS",
    "TSPEED", "LFLOW", "LINEMODE", "XDISPLOC", "OLD ENVIRON", "AUTHENTICATION", "ENCRYPT", "NEW ENVIRON",
    "TN3270E", "XAUTH", "CHARSET", "RSP", "COM PORT CONTROL", "SUPPRESS LOCAL ECHO", "START TLS", "KERMIT",
    "SEND-URL", "FORWARD_X",
};

std::string unknown(std::uint8_t value, const char *what) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "0x%02x(Unknown Telnet %s)", value, what);
    return buffer;
}

}  // namespace

std::string commandName(std::uint8_t command) {
    if (command >= XEOF) {
        return CommandNames[command - XEOF];
    }
    return unknown(command, "Command");
}

std::string optionName(std::uint8_t option) {
    if (option < std::size(OptionNames)) {
        return OptionNames[option];
    }
    switch (option) {
    case 85: return "MUD COMPRESSION PROTOCOL (V1)";
    case 86: return "MUD COMPRESSION PROTOCOL (V2)";
    case 90: return "MUD SOUND PROTOCOL";
    case 91: return "MUD EXTENSION PROTOCOL";
    case 93: return "ZENITH MUD PROTOCOL";
    case 138: return "TELOPT PRAGMA LOGON";
    case 139: return "TELOPT SSPI LOGON";
    case 140: return "TELOPT PRAGMA HEARTBEAT";
    case 201: return "GENERIC MUD COMMUNICATION PROTOCOL";
    case 255: return "EXTENDED OPTIONS LIST";
    default: return unknown(option, "Option");
    }
}

Engine::Engine(SendFunction send, QByteArray terminalType)
    : m_send(std::move(send)), m_terminalType(std::move(terminalType)) {}

bool Engine::acceptRemote(std::uint8_t option) {
    return option == ECHO || option == SGA || option == BINARY;
}

bool Engine::acceptLocal(std::uint8_t option) {
    return option == NAWS || option == TTYPE || option == SGA || option == BINARY;
}

void Engine::begin() {
    m_localRequested[NAWS] = true;
    sendCommand(WILL, NAWS);
    m_localRequested[TTYPE] = true;
    sendCommand(WILL, TTYPE);
    m_remoteRequested[SGA] = true;
    sendCommand(DO, SGA);
}

void Engine::sendCommand(std::uint8_t command, std::uint8_t option) {
    const char packet[3] = {char(IAC), char(command), char(option)};
    m_send(QByteArray(packet, 3));
}

void Engine::sendWindowSize() {
    if (!m_local[NAWS]) {
        return;
    }
    QByteArray packet;
    packet.append(char(IAC)).append(char(SB)).append(char(NAWS));
    for (int value : {m_columns, m_rows}) {
        for (char byte : {char((value >> 8) & 0xFF), char(value & 0xFF)}) {
            packet.append(byte);
            if (std::uint8_t(byte) == IAC) {
                packet.append(byte);  // IAC inside SB must be doubled
            }
        }
    }
    packet.append(char(IAC)).append(char(SE));
    m_send(packet);
}

void Engine::setWindowSize(int columns, int rows) {
    m_columns = columns;
    m_rows = rows;
    sendWindowSize();
}

// RFC 1143 style: only answer when the state really changes, so two
// endpoints can never loop on each other.
void Engine::handleWill(std::uint8_t option) {
    if (m_remote[option]) {
        return;
    }
    if (acceptRemote(option)) {
        m_remote[option] = true;
        if (!std::exchange(m_remoteRequested[option], false)) {
            sendCommand(DO, option);
        }
    } else {
        sendCommand(DONT, option);
    }
}

void Engine::handleWont(std::uint8_t option) {
    const bool requested = std::exchange(m_remoteRequested[option], false);
    if (m_remote[option]) {
        m_remote[option] = false;
        sendCommand(DONT, option);
    } else if (requested) {
        // Our DO was refused: nothing to answer.
    }
}

void Engine::handleDo(std::uint8_t option) {
    if (m_local[option]) {
        return;
    }
    if (acceptLocal(option)) {
        m_local[option] = true;
        if (!std::exchange(m_localRequested[option], false)) {
            sendCommand(WILL, option);
        }
        if (option == NAWS) {
            sendWindowSize();
        }
    } else {
        sendCommand(WONT, option);
    }
}

void Engine::handleDont(std::uint8_t option) {
    std::exchange(m_localRequested[option], false);
    if (m_local[option]) {
        m_local[option] = false;
        sendCommand(WONT, option);
    }
}

void Engine::handleSubnegotiation() {
    if (m_subnegotiation.size() >= 2 && std::uint8_t(m_subnegotiation[0]) == TTYPE
        && std::uint8_t(m_subnegotiation[1]) == SEND && m_local[TTYPE]) {
        QByteArray packet;
        packet.append(char(IAC)).append(char(SB)).append(char(TTYPE)).append(char(IS));
        packet.append(m_terminalType);
        packet.append(char(IAC)).append(char(SE));
        m_send(packet);
    }
    m_subnegotiation.clear();
}

QByteArray Engine::receive(const QByteArray &input) {
    QByteArray data;
    data.reserve(input.size());

    for (char ch : input) {
        const auto byte = std::uint8_t(ch);
        switch (m_state) {
        case State::Data:
            if (byte == IAC) {
                m_state = State::Iac;
            } else {
                data.append(ch);
                if (byte == '\r' && !m_remote[BINARY]) {
                    m_state = State::Cr;
                }
            }
            break;
        case State::Cr:
            // NVT: CR NUL means a bare CR; drop the NUL.
            m_state = State::Data;
            if (byte == IAC) {
                m_state = State::Iac;
            } else if (byte != 0) {
                data.append(ch);
                if (byte == '\r') {
                    m_state = State::Cr;
                }
            }
            break;
        case State::Iac:
            switch (byte) {
            case IAC: data.append(ch); m_state = State::Data; break;
            case WILL: m_state = State::Will; break;
            case WONT: m_state = State::Wont; break;
            case DO: m_state = State::Do; break;
            case DONT: m_state = State::Dont; break;
            case SB: m_state = State::Sb; m_subnegotiation.clear(); break;
            default: m_state = State::Data; break;  // NOP, GA, DM, ...
            }
            break;
        case State::Will: handleWill(byte); m_state = State::Data; break;
        case State::Wont: handleWont(byte); m_state = State::Data; break;
        case State::Do: handleDo(byte); m_state = State::Data; break;
        case State::Dont: handleDont(byte); m_state = State::Data; break;
        case State::Sb:
            if (byte == IAC) {
                m_state = State::SbIac;
            } else if (m_subnegotiation.size() < 4096) {
                m_subnegotiation.append(ch);
            }
            break;
        case State::SbIac:
            if (byte == SE) {
                handleSubnegotiation();
                m_state = State::Data;
            } else {
                if (byte == IAC) {
                    m_subnegotiation.append(ch);
                }
                m_state = State::Sb;
            }
            break;
        }
    }
    return data;
}

QByteArray Engine::encode(const QByteArray &output) const {
    QByteArray encoded;
    encoded.reserve(output.size() + 8);
    const bool binary = m_local[BINARY];
    for (qsizetype i = 0; i < output.size(); ++i) {
        const char ch = output[i];
        encoded.append(ch);
        if (std::uint8_t(ch) == IAC) {
            encoded.append(ch);
        } else if (ch == '\r' && !binary && (i + 1 >= output.size() || output[i + 1] != '\n')) {
            encoded.append('\0');
        }
    }
    return encoded;
}

}  // namespace telnet

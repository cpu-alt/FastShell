// Copyright 2020 WindTerm, 2026 WindTerm NG contributors. Apache-2.0.
//
// Rewritten from the original. Fixes: resize never applied (`&&` instead of
// `||` and inverted assignment), delete instead of delete[], handles closed
// twice on restart, createProcess() reporting success on failure, a 512 byte
// read buffer (now 64 KiB) and one readyRead signal per chunk.
#ifdef _WIN32

#include "pty/ConPty.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QOperatingSystemVersion>
#include <QWinEventNotifier>

namespace {

constexpr DWORD ReadBufferSize = 64 * 1024;

void closeHandle(HANDLE &handle) {
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
        CloseHandle(handle);
    }
    handle = INVALID_HANDLE_VALUE;
}

// Quotes one argument following the MSVC CommandLineToArgvW rules.
QString quoteArgument(const QString &arg) {
    if (!arg.isEmpty() && !arg.contains(QLatin1Char(' ')) && !arg.contains(QLatin1Char('\t'))
        && !arg.contains(QLatin1Char('"'))) {
        return arg;
    }
    QString out = QStringLiteral("\"");
    int backslashes = 0;
    for (QChar ch : arg) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
        } else {
            out += QString(backslashes, QLatin1Char('\\'));
        }
        backslashes = 0;
        out += ch;
    }
    out += QString(backslashes * 2, QLatin1Char('\\'));
    out += QLatin1Char('"');
    return out;
}

std::wstring environmentBlock(const QStringList &extra) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("FastShell"));
    for (const QString &entry : extra) {
        const qsizetype eq = entry.indexOf(QLatin1Char('='));
        if (eq > 0) {
            env.insert(entry.left(eq), entry.mid(eq + 1));
        }
    }
    std::wstring block;
    for (const QString &entry : env.toStringList()) {
        block += entry.toStdWString();
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

}  // namespace

ConPty::ConPty(Options options, QObject *parent) : Pty(std::move(options), parent) {}

ConPty::~ConPty() {
    stop();
}

bool ConPty::isAvailable() {
    return QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 17763);
}

void ConPty::fail(const QString &what, DWORD error) {
    wchar_t *message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    const QString text = message ? QString::fromWCharArray(message).trimmed() : QString::number(error);
    LocalFree(message);
    Q_EMIT errorOccurred(QStringLiteral("%1: %2").arg(what, text));
}

void ConPty::start(int rows, int columns) {
    stop();
    if (!isAvailable()) {
        Q_EMIT errorOccurred(tr("ConPty needs Windows 10 version 1809 or later."));
        return;
    }

    HANDLE ptyInput = INVALID_HANDLE_VALUE;
    HANDLE ptyOutput = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&ptyInput, &m_input, nullptr, 0) || !CreatePipe(&m_output, &ptyOutput, nullptr, 0)) {
        fail(tr("Cannot create pipes"), GetLastError());
        closeHandle(ptyInput);
        stop();
        return;
    }

    const COORD size{SHORT(columns), SHORT(rows)};
    const HRESULT hr = CreatePseudoConsole(size, ptyInput, ptyOutput, 0, &m_console);
    // The console keeps its own references to the pty side of the pipes.
    closeHandle(ptyInput);
    closeHandle(ptyOutput);
    if (FAILED(hr)) {
        fail(tr("Cannot create pseudo console"), DWORD(hr));
        m_console = nullptr;
        stop();
        return;
    }
    m_rows = rows;
    m_columns = columns;

    SIZE_T listSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
    m_attributeList.resize(listSize);
    auto *list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributeList.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &listSize)
        || !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_console, sizeof(HPCON),
                                      nullptr, nullptr)) {
        fail(tr("Cannot attach pseudo console"), GetLastError());
        stop();
        return;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = list;

    QString commandLine = quoteArgument(QDir::toNativeSeparators(m_options.program));
    for (const QString &arg : m_options.arguments) {
        commandLine += QLatin1Char(' ') + quoteArgument(arg);
    }
    std::wstring command = commandLine.toStdWString();
    std::wstring environment = environmentBlock(m_options.extraEnvironment);
    const QString directory = m_options.workingDirectory.isEmpty() ? QDir::homePath() : m_options.workingDirectory;
    const std::wstring workingDirectory = QDir::toNativeSeparators(directory).toStdWString();

    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, environment.data(),
                        workingDirectory.c_str(), &startup.StartupInfo, &m_process)) {
        fail(tr("Cannot start %1").arg(m_options.program), GetLastError());
        m_process = {};
        stop();
        return;
    }

    m_reader = std::thread([this] { readLoop(); });

    m_exitNotifier = new QWinEventNotifier(m_process.hProcess, this);
    connect(m_exitNotifier, &QWinEventNotifier::activated, this, &ConPty::onProcessExited);
}

void ConPty::readLoop() {
    std::vector<char> buffer(ReadBufferSize);
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(m_output, buffer.data(), ReadBufferSize, &bytesRead, nullptr) || bytesRead == 0) {
            return;  // pipe closed: console gone
        }
        appendData(buffer.data(), bytesRead);
    }
}

void ConPty::onProcessExited() {
    DWORD exitCode = 0;
    GetExitCodeProcess(m_process.hProcess, &exitCode);
    stop();
    const QString code = exitCode >= 0xFF ? QStringLiteral("0x") + QString::number(exitCode, 16)
                                          : QString::number(exitCode);
    Q_EMIT finished(tr("Process exited with code %1").arg(code));
}

void ConPty::write(const QByteArray &data) {
    if (m_input == INVALID_HANDLE_VALUE) {
        return;
    }
    const char *p = data.constData();
    qsizetype remaining = data.size();
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(m_input, p, DWORD(remaining), &written, nullptr)) {
            return;
        }
        p += written;
        remaining -= written;
    }
}

void ConPty::resize(int rows, int columns) {
    if (m_console && (rows != m_rows || columns != m_columns)) {
        if (SUCCEEDED(ResizePseudoConsole(m_console, COORD{SHORT(columns), SHORT(rows)}))) {
            m_rows = rows;
            m_columns = columns;
        }
    }
}

void ConPty::stop() {
    if (m_exitNotifier) {
        m_exitNotifier->setEnabled(false);
        m_exitNotifier->deleteLater();
        m_exitNotifier = nullptr;
    }
    // Closing the console ends the client process and breaks the output
    // pipe, which lets the reader thread finish. The reader keeps draining
    // meanwhile, so ClosePseudoConsole cannot dead-lock on a full pipe.
    if (m_console) {
        ClosePseudoConsole(m_console);
        m_console = nullptr;
    }
    closeHandle(m_input);
    if (m_reader.joinable()) {
        m_reader.join();
    }
    closeHandle(m_output);
    closeHandle(m_process.hThread);
    closeHandle(m_process.hProcess);
    m_process = {};
    if (!m_attributeList.empty()) {
        DeleteProcThreadAttributeList(reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributeList.data()));
        m_attributeList.clear();
    }
}

#endif  // _WIN32

// Copyright 2026 FastShell contributors. Apache-2.0.
#include "net/SshCommon.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <chrono>
#include <future>

#include <libssh/libssh.h>

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("Ssh", text);
}

QString lastError(ssh_session s) {
    return QString::fromUtf8(ssh_get_error(s));
}

QString serverFingerprint(ssh_session s) {
    ssh_key key = nullptr;
    if (ssh_get_server_publickey(s, &key) != SSH_OK) {
        return {};
    }
    unsigned char *hash = nullptr;
    size_t length = 0;
    QString result;
    if (ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &length) == 0) {
        char *text = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, length);
        result = QStringLiteral("%1 %2").arg(QString::fromLatin1(ssh_key_type_to_char(ssh_key_type(key))),
                                             QString::fromLatin1(text));
        ssh_string_free_char(text);
        ssh_clean_pubkey_hash(&hash);
    }
    ssh_key_free(key);
    return result;
}

enum class LegacyLevel { None, Compatible, Full };

// Appends ("+" prefix) old algorithms after the modern defaults, so a modern
// server still negotiates modern ones. Full also allows group1-sha1, which
// is breakable (Logjam) and therefore never enabled automatically.
void applyLegacyAlgorithms(ssh_session s, LegacyLevel level) {
    if (level == LegacyLevel::None) return;
    const char *kex = level == LegacyLevel::Full
                          ? "+diffie-hellman-group14-sha1,diffie-hellman-group-exchange-sha1,diffie-hellman-group1-sha1"
                          : "+diffie-hellman-group14-sha1,diffie-hellman-group-exchange-sha1";
    ssh_options_set(s, SSH_OPTIONS_KEY_EXCHANGE, kex);
    ssh_options_set(s, SSH_OPTIONS_HOSTKEYS, "+ssh-rsa");
    ssh_options_set(s, SSH_OPTIONS_PUBLICKEY_ACCEPTED_TYPES, "+ssh-rsa");
    ssh_options_set(s, SSH_OPTIONS_CIPHERS_C_S, "+aes128-cbc,aes192-cbc,aes256-cbc,3des-cbc");
    ssh_options_set(s, SSH_OPTIONS_CIPHERS_S_C, "+aes128-cbc,aes192-cbc,aes256-cbc,3des-cbc");
    ssh_options_set(s, SSH_OPTIONS_HMAC_C_S, "+hmac-sha1");
    ssh_options_set(s, SSH_OPTIONS_HMAC_S_C, "+hmac-sha1");
}

bool isAlgorithmMismatch(const QString &error) {
    return error.contains(QLatin1String("no match for method"), Qt::CaseInsensitive)
        || error.contains(QLatin1String("kex error"), Qt::CaseInsensitive);
}

// Runs fn on the GUI thread and waits for the answer. If the caller is told
// to stop meanwhile (tab closed), it gives up instead of dead-locking.
template <typename R, typename F>
std::optional<R> callOnGui(QObject *context, const std::atomic<bool> *stop, F fn) {
    if (!context) return std::nullopt;
    auto promise = std::make_shared<std::promise<R>>();
    auto future = promise->get_future();
    QMetaObject::invokeMethod(context, [promise, fn = std::move(fn)]() mutable { promise->set_value(fn()); },
                              Qt::QueuedConnection);
    while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
        if (stop && *stop) {
            return std::nullopt;
        }
    }
    return future.get();
}

}  // namespace

void SshSessionDeleter::operator()(ssh_session_struct *s) const {
    if (ssh_is_connected(s)) {
        ssh_disconnect(s);
    }
    ssh_free(s);
}

bool SshPrompter::confirmOnGui(const QString &question) const {
    if (!confirm) return false;
    auto callback = confirm;
    return callOnGui<bool>(guiContext, stop, [callback, question] { return callback(question); }).value_or(false);
}

std::optional<QString> SshPrompter::askOnGui(const QString &prompt, bool secret) const {
    if (!ask) return std::nullopt;
    auto callback = ask;
    auto answer = callOnGui<std::optional<QString>>(guiContext, stop,
                                                    [callback, prompt, secret] { return callback(prompt, secret); });
    return answer ? *answer : std::nullopt;
}

std::optional<QString> SshPrompter::askLogin(const QString &prompt, bool secret) const {
    if (askInTerminal) return askInTerminal(prompt, secret);
    return askOnGui(prompt.trimmed(), secret);
}

void SshPrompter::notice(const QString &text) const {
    if (showInTerminal) showInTerminal(text);
}

SshConnectResult sshConnect(const SshOptions &options, const SshPrompter &prompter) {
    SshConnectResult result;
    SshSessionPtr session(ssh_new());
    if (!session) {
        result.error = tr("Cannot create SSH session");
        return result;
    }
    ssh_session s = session.get();

    // Like PuTTY: only what the session says. ~/.ssh/config is not read, so
    // it cannot bring a user name or keys of its own.
    const QByteArray host = options.host.toUtf8();
    auto configure = [&](ssh_session target, LegacyLevel legacy) {
        int port = options.port;
        ssh_options_set(target, SSH_OPTIONS_HOST, host.constData());
        ssh_options_set(target, SSH_OPTIONS_PORT, &port);
        if (!options.knownHostsFile.isEmpty()) {
            ssh_options_set(target, SSH_OPTIONS_KNOWNHOSTS, options.knownHostsFile.toLocal8Bit().constData());
        }
        applyLegacyAlgorithms(target, legacy);
    };

    configure(s, options.legacyAlgorithms ? LegacyLevel::Full : LegacyLevel::None);
    if (ssh_connect(s) != SSH_OK) {
        const QString error = lastError(s);
        // Old switches and routers only speak SHA-1 / CBC algorithms that
        // libssh no longer offers by default. Retry once with them appended
        // (modern ones stay preferred), as PuTTY and MobaXterm do.
        if (!options.legacyAlgorithms && isAlgorithmMismatch(error)) {
            session.reset(ssh_new());
            s = session.get();
            configure(s, LegacyLevel::Compatible);
            if (ssh_connect(s) == SSH_OK) {
                result.usedLegacyAlgorithms = true;
            } else {
                result.error = tr("Connection to %1 failed: %2\n\nThis device uses very old algorithms. Enable "
                                  "\"Compatibility with old devices\" in the session settings to also allow "
                                  "diffie-hellman-group1-sha1.")
                                   .arg(options.host, lastError(s));
                return result;
            }
        } else {
            result.error = tr("Connection to %1 failed: %2").arg(options.host, error);
            return result;
        }
    } else if (options.legacyAlgorithms) {
        result.usedLegacyAlgorithms = true;
    }

    // --- host key verification -------------------------------------------
    const QString fingerprint = serverFingerprint(s);
    switch (ssh_session_is_known_server(s)) {
    case SSH_KNOWN_HOSTS_OK:
        break;
    case SSH_KNOWN_HOSTS_CHANGED:
    case SSH_KNOWN_HOSTS_OTHER:
        result.error = tr("WARNING: the host key of %1 has changed (%2). "
                          "Someone may be intercepting the connection. Connection refused.")
                           .arg(options.host, fingerprint);
        return result;
    case SSH_KNOWN_HOSTS_NOT_FOUND:
    case SSH_KNOWN_HOSTS_UNKNOWN:
        if (!prompter.confirmOnGui(tr("The authenticity of host %1 can't be established.\n\n"
                                      "Key fingerprint: %2\n\nTrust this host and continue?")
                                       .arg(options.host, fingerprint))) {
            return result;  // cancelled
        }
        ssh_session_update_known_hosts(s);
        break;
    default:
        result.error = tr("Cannot verify host key: %1").arg(lastError(s));
        return result;
    }

    // --- user name -----------------------------------------------------------
    // No user in the session: ask for it in the terminal, like PuTTY.
    QString userName = options.user;
    while (userName.trimmed().isEmpty()) {
        const std::optional<QString> typed = prompter.askLogin(tr("login as: "), false);
        if (!typed) return result;  // cancelled
        userName = typed->trimmed();
    }
    result.user = userName;
    ssh_options_set(s, SSH_OPTIONS_USER, userName.toUtf8().constData());

    // --- authentication ----------------------------------------------------
    // Only the key set in the session (or its shared user) is ever used: no
    // agent, no ~/.ssh default keys. Otherwise: password, typed in the terminal.
    int rc = ssh_userauth_none(s, nullptr);
    const int methods = ssh_userauth_list(s, nullptr);
    auto stopping = [&prompter] { return prompter.stop && *prompter.stop; };

    if (rc != SSH_AUTH_SUCCESS && (methods & SSH_AUTH_METHOD_PUBLICKEY) && !options.identityFile.isEmpty()) {
        ssh_key key = nullptr;
        const QByteArray path = options.identityFile.toLocal8Bit();
        if (ssh_pki_import_privkey_file(path.constData(), nullptr, nullptr, nullptr, &key) != SSH_OK) {
            if (auto phrase = prompter.askOnGui(tr("Passphrase for %1:").arg(options.identityFile), true)) {
                ssh_pki_import_privkey_file(path.constData(), phrase->toUtf8().constData(), nullptr, nullptr, &key);
            }
        }
        if (key) {
            rc = ssh_userauth_publickey(s, nullptr, key);
            ssh_key_free(key);
        }
    }

    // The saved password is tried once, then up to 3 typed ones.
    constexpr int MaxTypedPasswords = 3;
    int typed = 0;
    bool savedTried = options.password.isEmpty();
    auto nextPassword = [&](const QString &prompt) -> std::optional<QString> {
        if (!savedTried) {
            savedTried = true;
            return options.password;
        }
        ++typed;
        return prompter.askLogin(prompt, true);
    };
    const QString accessDenied = tr("Access denied");
    // Same prompt whatever the method, like MobaXterm and PuTTY.
    const QString passwordPrompt = tr("%1@%2's password: ").arg(userName, options.host);

    if (rc != SSH_AUTH_SUCCESS && (methods & SSH_AUTH_METHOD_INTERACTIVE)) {
        while (rc != SSH_AUTH_SUCCESS && typed < MaxTypedPasswords && !stopping()) {
            bool answered = false;
            rc = ssh_userauth_kbdint(s, nullptr, nullptr);
            while (rc == SSH_AUTH_INFO && !stopping()) {
                const int prompts = ssh_userauth_kbdint_getnprompts(s);
                for (int i = 0; i < prompts; ++i) {
                    char echo = 0;
                    QString prompt = QString::fromUtf8(ssh_userauth_kbdint_getprompt(s, i, &echo));
                    if (!echo && prompts == 1 && prompt.contains(QLatin1String("password"), Qt::CaseInsensitive)) {
                        prompt = passwordPrompt;  // the server's plain "Password:"
                    } else if (!prompt.endsWith(QLatin1Char(' '))) {
                        prompt += QLatin1Char(' ');
                    }
                    const std::optional<QString> answer = echo ? prompter.askLogin(prompt, false) : nextPassword(prompt);
                    if (!answer) return result;  // cancelled
                    answered = true;
                    if (!echo) result.password = *answer;
                    ssh_userauth_kbdint_setanswer(s, i, answer->toUtf8().constData());
                }
                rc = ssh_userauth_kbdint(s, nullptr, nullptr);
            }
            if (rc == SSH_AUTH_SUCCESS || rc == SSH_AUTH_ERROR || !answered) break;
            prompter.notice(accessDenied);
        }
    }

    while (rc != SSH_AUTH_SUCCESS && rc != SSH_AUTH_ERROR && (methods & SSH_AUTH_METHOD_PASSWORD)
           && typed < MaxTypedPasswords && !stopping()) {
        const std::optional<QString> password = nextPassword(passwordPrompt);
        if (!password) return result;  // cancelled
        rc = ssh_userauth_password(s, nullptr, password->toUtf8().constData());
        result.password = *password;
        if (rc == SSH_AUTH_DENIED) prompter.notice(accessDenied);
    }

    if (rc != SSH_AUTH_SUCCESS) {
        if (!(methods & (SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_INTERACTIVE)) && options.identityFile.isEmpty()) {
            result.error = tr("Authentication failed: %1 only accepts keys. Choose a private key in the session.")
                               .arg(options.host);
        } else {
            result.error = tr("Authentication failed for %1@%2.").arg(userName, options.host);
        }
        return result;
    }
    result.session = std::move(session);
    return result;
}

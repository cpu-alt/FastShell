// Copyright 2026 FastShell contributors. Apache-2.0.
//
// SSH login against a small local libssh server: the user name and the
// passwords are asked in the terminal (like PuTTY) and no key is ever tried
// unless the session sets one.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>

#include "net/SshBackend.h"
#include "net/SshCommon.h"

namespace {

// Accepts "alice" / "secret" by password; counts key attempts.
class FakeServer {
public:
    FakeServer() {
        ssh_key key = nullptr;
        ssh_pki_generate_key(SSH_KEYTYPE_ED25519, nullptr, &key);
        for (int attempt = 0; attempt < 20 && !m_bind; ++attempt) {
            ssh_bind bind = ssh_bind_new();
            int port = 22000 + int((std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % 20000);
            ssh_bind_options_set(bind, SSH_BIND_OPTIONS_BINDADDR, "127.0.0.1");
            ssh_bind_options_set(bind, SSH_BIND_OPTIONS_BINDPORT, &port);
            ssh_bind_options_set(bind, SSH_BIND_OPTIONS_IMPORT_KEY, key);
            if (ssh_bind_listen(bind) == SSH_OK) {
                m_bind = bind;
                m_port = port;
            } else {
                ssh_bind_free(bind);
            }
        }
        if (m_bind) m_thread = std::thread([this] { serve(); });
    }
    ~FakeServer() {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
        if (m_bind) ssh_bind_free(m_bind);
    }

    int port() const { return m_port; }
    std::atomic<int> keyAttempts{0};
    std::atomic<int> passwordAttempts{0};
    std::atomic<bool> authenticated{false};
    std::string lastUser() {
        std::lock_guard lock(m_mutex);
        return m_lastUser;
    }

private:
    static int onPassword(ssh_session, const char *user, const char *password, void *data) {
        auto *self = static_cast<FakeServer *>(data);
        ++self->passwordAttempts;
        {
            std::lock_guard lock(self->m_mutex);
            self->m_lastUser = user;
        }
        if (std::string(user) == "alice" && std::string(password) == "secret") {
            self->authenticated = true;
            return SSH_AUTH_SUCCESS;
        }
        return SSH_AUTH_DENIED;
    }
    static int onKey(ssh_session, const char *, struct ssh_key_struct *, char, void *data) {
        ++static_cast<FakeServer *>(data)->keyAttempts;
        return SSH_AUTH_DENIED;
    }

    void serve() {
        ssh_session session = ssh_new();
        if (ssh_bind_accept(m_bind, session) == SSH_OK) {
            ssh_server_callbacks_struct callbacks{};
            callbacks.userdata = this;
            callbacks.auth_password_function = &FakeServer::onPassword;
            callbacks.auth_pubkey_function = &FakeServer::onKey;
            ssh_callbacks_init(&callbacks);
            ssh_set_server_callbacks(session, &callbacks);
            ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY);
            if (ssh_handle_key_exchange(session) == SSH_OK) {
                ssh_event event = ssh_event_new();
                ssh_event_add_session(event, session);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                while (!m_stop && std::chrono::steady_clock::now() < deadline) {
                    if (ssh_event_dopoll(event, 50) == SSH_ERROR && !ssh_is_connected(session)) break;
                }
                ssh_event_remove_session(event, session);
                ssh_event_free(event);
            }
        }
        ssh_disconnect(session);
        ssh_free(session);
    }

    ssh_bind m_bind = nullptr;
    int m_port = 0;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::mutex m_mutex;
    std::string m_lastUser;
};

QCoreApplication &app() {
    static int argc = 1;
    static char name[] = "fastshell_tests";
    static char *argv[] = {name, nullptr};
    static QCoreApplication *instance = QCoreApplication::instance() ? nullptr : new QCoreApplication(argc, argv);
    return instance ? *instance : *QCoreApplication::instance();
}

struct LoginRun {
    SshConnectResult result;
    QStringList prompts;
    QStringList notices;
};

// Connects with the answers typed "in the terminal", one per prompt.
LoginRun connect(const SshOptions &options, QStringList answers) {
    QObject gui;  // host key question: answered on this (main) thread
    LoginRun run;
    std::mutex mutex;
    SshPrompter prompter;
    prompter.guiContext = &gui;
    prompter.confirm = [](const QString &) { return true; };
    prompter.askInTerminal = [&](const QString &prompt, bool) -> std::optional<QString> {
        std::lock_guard lock(mutex);
        run.prompts << prompt;
        if (answers.isEmpty()) return std::nullopt;
        return answers.takeFirst();
    };
    prompter.showInTerminal = [&](const QString &text) {
        std::lock_guard lock(mutex);
        run.notices << text;
    };
    auto future = std::async(std::launch::async, [&] { return sshConnect(options, prompter); });
    while (future.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
        QCoreApplication::processEvents();
    }
    run.result = future.get();
    return run;
}

}  // namespace

class SshLogin : public ::testing::Test {
protected:
    void SetUp() override {
        app();
        ASSERT_TRUE(m_dir.isValid());
        ASSERT_GT(m_server.port(), 0) << "cannot start the test SSH server";
        m_options.host = QStringLiteral("127.0.0.1");
        m_options.port = m_server.port();
        m_options.knownHostsFile = m_dir.filePath(QStringLiteral("known_hosts"));  // never the user's own
    }

    QTemporaryDir m_dir;
    FakeServer m_server;
    SshOptions m_options;
};

TEST_F(SshLogin, AsksUserAndPasswordInTheTerminal) {
    const LoginRun run = connect(m_options, {QStringLiteral("alice"), QStringLiteral("wrong"), QStringLiteral("secret")});
    ASSERT_TRUE(run.result.session) << run.result.error.toStdString();
    ASSERT_EQ(run.prompts.size(), 3);
    EXPECT_EQ(run.prompts[0], QStringLiteral("login as: "));
    EXPECT_EQ(run.prompts[1], QStringLiteral("alice@127.0.0.1's password: "));
    EXPECT_EQ(run.prompts[2], run.prompts[1]);
    EXPECT_TRUE(run.notices.contains(QStringLiteral("Access denied")));
    EXPECT_EQ(run.result.user, QStringLiteral("alice"));
    EXPECT_EQ(run.result.password, QStringLiteral("secret"));
    EXPECT_EQ(m_server.lastUser(), "alice");
    EXPECT_EQ(m_server.keyAttempts, 0);  // no agent, no ~/.ssh keys
}

TEST_F(SshLogin, SavedLoginAsksNothing) {
    m_options.user = QStringLiteral("alice");
    m_options.password = QStringLiteral("secret");
    const LoginRun run = connect(m_options, {});
    ASSERT_TRUE(run.result.session) << run.result.error.toStdString();
    EXPECT_TRUE(run.prompts.isEmpty());
    EXPECT_EQ(m_server.keyAttempts, 0);
}

TEST_F(SshLogin, GivesUpAfterThreeWrongPasswords) {
    m_options.user = QStringLiteral("alice");
    const LoginRun run = connect(m_options, {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                                        QStringLiteral("secret")});
    EXPECT_FALSE(run.result.session);
    EXPECT_EQ(run.prompts.size(), 3);
    EXPECT_FALSE(run.result.error.isEmpty());
    EXPECT_EQ(m_server.keyAttempts, 0);
}

// --- through SshBackend, as the terminal sees it --------------------------------

namespace {

// Pumps events and collects what the backend shows until `text` appears.
bool waitShown(SshBackend &backend, QByteArray &shown, const QByteArray &text) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!shown.contains(text) && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        shown += backend.readAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return shown.contains(text);
}

bool waitLoggedIn(SshBackend &backend, QByteArray &shown, const std::atomic<bool> &authenticated) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!authenticated && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        shown += backend.readAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 20; ++i) {  // anything shown right after the login
        QCoreApplication::processEvents();
        shown += backend.readAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return authenticated;
}

}  // namespace

TEST_F(SshLogin, TerminalShowsEachPromptOnceLikeMobaXterm) {
    SshBackend backend(m_options);
    backend.confirm = [](const QString &) { return true; };
    backend.ask = [](const QString &, bool) -> std::optional<QString> {
        ADD_FAILURE() << "no dialog may ask for the login";
        return std::nullopt;
    };
    backend.start(24, 80);
    QByteArray shown;
    ASSERT_TRUE(waitShown(backend, shown, "login as: "));
    // Typing with a mistake, Backspace, and an arrow key split over two writes.
    backend.write("alx");
    backend.write("\x7f");
    backend.write("i\x1b[");
    backend.write("Cce\r");
    ASSERT_TRUE(waitShown(backend, shown, "password: "));
    backend.write("secret\r");
    ASSERT_TRUE(waitLoggedIn(backend, shown, m_server.authenticated));
    EXPECT_EQ(m_server.lastUser(), "alice");
    EXPECT_EQ(shown, QByteArray("login as: alx\b \bice\r\nalice@127.0.0.1's password: \r\n"));
    EXPECT_EQ(m_server.keyAttempts, 0);
}

TEST_F(SshLogin, UserOnlyGoesStraightToThePassword) {
    m_options.user = QStringLiteral("alice");
    SshBackend backend(m_options);
    backend.confirm = [](const QString &) { return true; };
    backend.start(24, 80);
    QByteArray shown;
    ASSERT_TRUE(waitShown(backend, shown, "password: "));
    backend.write("wrong\r");
    ASSERT_TRUE(waitShown(backend, shown, "Access denied\r\nalice@127.0.0.1's password: "));
    backend.write("secret\r");
    ASSERT_TRUE(waitLoggedIn(backend, shown, m_server.authenticated));
    EXPECT_EQ(shown, QByteArray("alice@127.0.0.1's password: \r\nAccess denied\r\n"
                                "alice@127.0.0.1's password: \r\n"));
}

TEST_F(SshLogin, CtrlCCancels) {
    const LoginRun run = connect(m_options, {});  // nothing typed: the login prompt is cancelled
    EXPECT_FALSE(run.result.session);
    EXPECT_TRUE(run.result.error.isEmpty());
    EXPECT_EQ(run.prompts, QStringList{QStringLiteral("login as: ")});
}

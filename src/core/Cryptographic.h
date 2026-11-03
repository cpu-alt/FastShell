// Copyright 2020 WindTerm, 2026 WindTerm NG contributors. Apache-2.0.
//
// Protects user secrets (passwords, private keys) with a master password.
//
// Current format (v2): PBKDF2-HMAC-SHA256 (600k iterations, OWASP 2023+)
// derives a 256-bit key; each message is sealed with AES-256-GCM and a fresh
// random 96-bit nonce, so tampering is detected and two encryptions of the
// same text never look alike.
//
// The original scheme (AES-256-CBC, IV derived from the password, no
// authentication) is kept as legacyDecrypt() only, to migrate old data.
#pragma once

#include <QByteArray>
#include <optional>

class Cryptographic {
public:
    static constexpr int KeyLength = 32;
    static constexpr int SaltLength = 16;
    static constexpr int DefaultIterations = 600000;

    // Random salt of SaltLength bytes.
    static QByteArray salt();
    static QByteArray deriveKey(const QByteArray &password, const QByteArray &salt,
                                int iterations = DefaultIterations);

    // Returns base64 text: "WT2" | nonce(12) | ciphertext | tag(16).
    static QByteArray encrypt(const QByteArray &plainText, const QByteArray &key);
    // Returns nullopt if the key is wrong or the data was modified.
    static std::optional<QByteArray> decrypt(const QByteArray &encoded, const QByteArray &key);

    // Original WindTerm format, for migration only.
    static QByteArray legacyPbkdf2(const QByteArray &password, const QByteArray &salt);
    static std::optional<QByteArray> legacyDecrypt(const QByteArray &encoded, const QByteArray &legacyPbkdf2);
};

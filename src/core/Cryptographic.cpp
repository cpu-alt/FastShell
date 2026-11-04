// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
#include "core/Cryptographic.h"

#include <cstring>
#include <memory>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace {

constexpr char Magic[] = "WT2";
constexpr int MagicLength = 3;
constexpr int NonceLength = 12;
constexpr int TagLength = 16;


using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

CipherCtx newContext() {
    CipherCtx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx) {
        throw std::bad_alloc();
    }
    return ctx;
}

const unsigned char *bytes(const QByteArray &data) {
    return reinterpret_cast<const unsigned char *>(data.constData());
}
unsigned char *bytes(QByteArray &data) {
    return reinterpret_cast<unsigned char *>(data.data());
}

}  // namespace

QByteArray Cryptographic::salt() {
    QByteArray salt(SaltLength, Qt::Uninitialized);
    if (RAND_bytes(bytes(salt), SaltLength) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return salt;
}

QByteArray Cryptographic::deriveKey(const QByteArray &password, const QByteArray &salt, int iterations) {
    QByteArray key(KeyLength, Qt::Uninitialized);
    if (PKCS5_PBKDF2_HMAC(password.constData(), int(password.size()), bytes(salt), int(salt.size()),
                          iterations, EVP_sha256(), KeyLength, bytes(key)) != 1) {
        throw std::runtime_error("PBKDF2 failed");
    }
    return key;
}

QByteArray Cryptographic::encrypt(const QByteArray &plainText, const QByteArray &key) {
    Q_ASSERT(key.size() == KeyLength);

    QByteArray out(MagicLength + NonceLength + plainText.size() + TagLength, Qt::Uninitialized);
    std::memcpy(out.data(), Magic, MagicLength);
    unsigned char *nonce = bytes(out) + MagicLength;
    unsigned char *cipher = nonce + NonceLength;
    unsigned char *tag = cipher + plainText.size();

    if (RAND_bytes(nonce, NonceLength) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }

    CipherCtx ctx = newContext();
    int length = 0;
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, bytes(key), nonce) != 1
        || EVP_EncryptUpdate(ctx.get(), cipher, &length, bytes(plainText), int(plainText.size())) != 1
        || EVP_EncryptFinal_ex(ctx.get(), cipher + length, &length) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TagLength, tag) != 1) {
        throw std::runtime_error("AES-GCM encryption failed");
    }
    return out.toBase64();
}

std::optional<QByteArray> Cryptographic::decrypt(const QByteArray &encoded, const QByteArray &key) {
    if (key.size() != KeyLength) {
        return std::nullopt;
    }
    const QByteArray raw = QByteArray::fromBase64(encoded);
    if (raw.size() < MagicLength + NonceLength + TagLength || !raw.startsWith(Magic)) {
        return std::nullopt;
    }

    const auto *nonce = bytes(raw) + MagicLength;
    const auto *cipher = nonce + NonceLength;
    const int cipherLength = int(raw.size()) - MagicLength - NonceLength - TagLength;
    QByteArray tag = raw.right(TagLength);

    QByteArray plain(cipherLength, Qt::Uninitialized);
    CipherCtx ctx = newContext();
    int length = 0;
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, bytes(key), nonce) != 1
        || EVP_DecryptUpdate(ctx.get(), bytes(plain), &length, cipher, cipherLength) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TagLength, tag.data()) != 1
        || EVP_DecryptFinal_ex(ctx.get(), bytes(plain) + length, &length) != 1) {
        return std::nullopt;  // wrong key or tampered data
    }
    return plain;
}


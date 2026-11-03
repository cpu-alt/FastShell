// Copyright 2026 WindTerm NG contributors. Apache-2.0.
#include <gtest/gtest.h>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/CircularBuffer.h"
#include "core/Cryptographic.h"
#include "core/ScopeGuard.h"
#include "core/SpinMutex.h"

TEST(CircularBuffer, RoundsCapacityToPowerOfTwo) {
    EXPECT_EQ(CircularBuffer<int>(1).capacity(), 1u);
    EXPECT_EQ(CircularBuffer<int>(8).capacity(), 8u);  // the original doubled this to 16
    EXPECT_EQ(CircularBuffer<int>(9).capacity(), 16u);
}

TEST(CircularBuffer, OverwritesOldest) {
    CircularBuffer<std::string> buffer(4);
    for (int i = 0; i < 6; ++i) {
        buffer.append(std::to_string(i));
    }
    EXPECT_EQ(buffer.size(), 4u);
    EXPECT_EQ(buffer.minIndex(), 2);
    EXPECT_EQ(buffer.currentIndex(), 5);
    EXPECT_EQ(buffer[2], "2");
    EXPECT_EQ(buffer[5], "5");
    EXPECT_FALSE(buffer.contains(1));
}

TEST(CircularBuffer, PopBackDoesNotResurrectOverwrittenSlots) {
    CircularBuffer<int> buffer(4);
    for (int i = 0; i < 6; ++i) {
        buffer.append(i);
    }
    EXPECT_EQ(buffer.popBack(), 5);
    EXPECT_EQ(buffer.minIndex(), 2);  // index 1 was overwritten, must stay gone
    EXPECT_EQ(buffer.size(), 3u);
    buffer.append(42);
    EXPECT_EQ(buffer.back(), 42);
    EXPECT_EQ(buffer[2], 2);
}

TEST(ScopeGuard, RunsAndCancels) {
    int calls = 0;
    {
        ScopeGuard guard([&] { ++calls; });
    }
    EXPECT_EQ(calls, 1);
    {
        ScopeGuard guard([&] { ++calls; });
        guard.cancel();
    }
    EXPECT_EQ(calls, 1);
    {
        ScopeGuard guard([&] { calls += 10; }, [&] { calls += 100; });
        EXPECT_EQ(calls, 11);
    }
    EXPECT_EQ(calls, 111);
}

TEST(SpinMutex, ProtectsCounter) {
    SpinMutex mutex;
    long counter = 0;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 100000; ++i) {
                std::lock_guard lock(mutex);
                ++counter;
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    EXPECT_EQ(counter, 400000);
}

TEST(Cryptographic, RoundTripAndTamperDetection) {
    const QByteArray salt = Cryptographic::salt();
    ASSERT_EQ(salt.size(), Cryptographic::SaltLength);
    const QByteArray key = Cryptographic::deriveKey("master password", salt, 1000);
    ASSERT_EQ(key.size(), Cryptographic::KeyLength);

    const QByteArray secret = "ssh-password-123";
    const QByteArray a = Cryptographic::encrypt(secret, key);
    const QByteArray b = Cryptographic::encrypt(secret, key);
    EXPECT_NE(a, b);  // fresh nonce every time
    EXPECT_EQ(Cryptographic::decrypt(a, key).value(), secret);

    QByteArray raw = QByteArray::fromBase64(a);
    raw[raw.size() / 2] = char(raw[raw.size() / 2] ^ 1);
    EXPECT_FALSE(Cryptographic::decrypt(raw.toBase64(), key).has_value());

    const QByteArray otherKey = Cryptographic::deriveKey("wrong", salt, 1000);
    EXPECT_FALSE(Cryptographic::decrypt(a, otherKey).has_value());
}

TEST(Cryptographic, LegacyKeyHasOriginalLayout) {
    const QByteArray legacy = Cryptographic::legacyPbkdf2("pw", "salt");
    EXPECT_EQ(legacy.size(), 48);  // 32 byte key + 16 byte IV, as in WindTerm 2.x
    EXPECT_FALSE(Cryptographic::legacyDecrypt("not base64 cipher", legacy).has_value());
}

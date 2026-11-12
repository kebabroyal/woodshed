// SPDX-FileCopyrightText: 2026 kebabroyal
// SPDX-License-Identifier: GPL-3.0-or-later

#include "woodshed/spsc_ring.hpp"

#include <catch2/catch.hpp>

#include <numeric>
#include <thread>
#include <vector>

using woodshed::spsc_ring;

TEST_CASE("spsc_ring rounds capacity up to a power of two")
{
    REQUIRE(spsc_ring<float>{5}.capacity() == 8);
    REQUIRE(spsc_ring<float>{8}.capacity() == 8);
}

TEST_CASE("spsc_ring keeps order across wrap-around and refuses overflow")
{
    spsc_ring<int> ring{8};
    std::vector<int> in(6);
    std::iota(in.begin(), in.end(), 0);
    std::vector<int> out(8);

    REQUIRE(ring.write(in.data(), 6) == 6);
    REQUIRE(ring.read(out.data(), 4) == 4);
    REQUIRE(out[3] == 3);

    // Wraps.
    std::iota(in.begin(), in.end(), 6);
    REQUIRE(ring.write(in.data(), 6) == 6);
    REQUIRE(ring.writable() == 0);
    REQUIRE(ring.write(in.data(), 1) == 0);

    REQUIRE(ring.read(out.data(), 8) == 8);
    for (int i = 0; i < 8; ++i)
        REQUIRE(out[i] == i + 4);
    REQUIRE(ring.readable() == 0);
}

TEST_CASE("spsc_ring clear drops pending items")
{
    spsc_ring<int> ring{4};
    int x[3] = {1, 2, 3};
    ring.write(x, 3);
    ring.clear();
    REQUIRE(ring.readable() == 0);
    REQUIRE(ring.writable() == 4);
}

TEST_CASE("spsc_ring passes a stream intact between two threads")
{
    constexpr int COUNT = 1'000'000;
    spsc_ring<int> ring{1024};

    std::thread writer{[&]
    {
        int buffer[97];
        int next = 0;
        while (next < COUNT)
        {
            const int n = std::min<int>(97, COUNT - next);
            for (int i = 0; i < n; ++i)
                buffer[i] = next + i;
            int sent = 0;
            while (sent < n)
                sent += static_cast<int>(ring.write(buffer + sent, n - sent));
            next += n;
        }
    }};

    int expected = 0;
    bool in_order = true;
    int buffer[61];
    while (expected < COUNT)
    {
        const std::size_t n = ring.read(buffer, 61);
        for (std::size_t i = 0; i < n; ++i)
            in_order = in_order && buffer[i] == expected++;
    }
    writer.join();
    REQUIRE(in_order);
}

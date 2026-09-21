/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <gtest/gtest.h>

#include "cpu/o3/klp_lock_view.hh"

namespace gem5
{
namespace o3
{

TEST(KlpLockViewTest, NewestProgramOrderUpdateWins)
{
    KlpLockView view;
    uint64_t key = 0;

    EXPECT_FALSE(view.lookup(0x1000, false, key));
    EXPECT_TRUE(view.update(0x1000, false, 20, 0xa));
    EXPECT_FALSE(view.update(0x1000, false, 10, 0xb));
    ASSERT_TRUE(view.lookup(0x1000, false, key));
    EXPECT_EQ(key, uint64_t{0xa});

    EXPECT_TRUE(view.update(0x1000, false, 30, 0xc));
    ASSERT_TRUE(view.lookup(0x1000, false, key));
    EXPECT_EQ(key, uint64_t{0xc});
}

TEST(KlpLockViewTest, DuplicateDoesNotReplacePermission)
{
    KlpLockView view;
    uint64_t key = 0;

    EXPECT_TRUE(view.update(0x1000, false, 20, 0xa));
    EXPECT_FALSE(view.update(0x1000, false, 20, 0xa));
    EXPECT_FALSE(view.update(0x1000, false, 20, 0xb));
    ASSERT_TRUE(view.lookup(0x1000, false, key));
    EXPECT_EQ(key, uint64_t{0xa});

    // A distinct, newer installation is an event even with the same key.
    EXPECT_TRUE(view.update(0x1000, false, 21, 0xa));
    EXPECT_FALSE(view.update(0x1000, false, 20, 0xb));
}

TEST(KlpLockViewTest, GranulesHaveIndependentPermissionAndOrder)
{
    KlpLockView view;
    uint64_t key = 0;

    EXPECT_TRUE(view.update(0x1000, false, 30, 0xa));
    EXPECT_TRUE(view.update(0x1010, false, 10, 0xb));
    EXPECT_TRUE(view.update(0x1010, false, 20, 0xc));
    EXPECT_FALSE(view.update(0x1000, false, 20, 0xd));

    ASSERT_TRUE(view.lookup(0x1000, false, key));
    EXPECT_EQ(key, uint64_t{0xa});
    ASSERT_TRUE(view.lookup(0x1010, false, key));
    EXPECT_EQ(key, uint64_t{0xc});
    EXPECT_FALSE(view.lookup(0x1020, false, key));
}

TEST(KlpLockViewTest, SecurityDomainsHaveIndependentPermissionAndOrder)
{
    KlpLockView view;
    uint64_t key = 0;

    EXPECT_TRUE(view.update(0x1000, false, 30, 0xa));
    EXPECT_TRUE(view.update(0x1000, true, 10, 0xb));
    EXPECT_TRUE(view.update(0x1000, true, 20, 0xc));
    EXPECT_FALSE(view.update(0x1000, false, 20, 0xd));

    ASSERT_TRUE(view.lookup(0x1000, false, key));
    EXPECT_EQ(key, uint64_t{0xa});
    ASSERT_TRUE(view.lookup(0x1000, true, key));
    EXPECT_EQ(key, uint64_t{0xc});
}

TEST(KlpLockViewTest, ZeroIsAValidKey)
{
    KlpLockView view;
    uint64_t key = 0xff;

    EXPECT_FALSE(view.lookup(0x1000, false, key));
    EXPECT_TRUE(view.update(0x1000, false, 1, 0));
    ASSERT_TRUE(view.lookup(0x1000, false, key));
    EXPECT_EQ(key, uint64_t{0});

    view.clear();
    EXPECT_FALSE(view.lookup(0x1000, false, key));
}

TEST(KlpLockViewTest, LaterMismatchCannotReuseAnEarlierMatchingLock)
{
    KlpLockView view;
    constexpr uint64_t loadKey = 0xa;
    uint64_t firstKey = 0;
    uint64_t secondKey = 0;

    // A split load must not reuse the first granule's earlier match when
    // another granule finally receives a matching installation.
    EXPECT_TRUE(view.update(0x1000, false, 10, loadKey));
    EXPECT_TRUE(view.update(0x1000, false, 20, 0xb));
    EXPECT_TRUE(view.update(0x1010, false, 30, loadKey));
    EXPECT_FALSE(view.update(0x1000, false, 10, loadKey));
    ASSERT_TRUE(view.lookup(0x1000, false, firstKey));
    ASSERT_TRUE(view.lookup(0x1010, false, secondKey));
    EXPECT_EQ(firstKey, uint64_t{0xb});
    EXPECT_EQ(secondKey, loadKey);
    EXPECT_FALSE(firstKey == loadKey && secondKey == loadKey);

    EXPECT_TRUE(view.update(0x1000, false, 40, loadKey));
    ASSERT_TRUE(view.lookup(0x1000, false, firstKey));
    EXPECT_TRUE(firstKey == loadKey && secondKey == loadKey);
}

TEST(KlpLockViewTest, InvalidateOnlyTheMatchingLineAndSecurityDomain)
{
    KlpLockView view;
    constexpr uint64_t lineMask = ~uint64_t(63);
    uint64_t key = 0;

    EXPECT_TRUE(view.update(0x0ff0, false, 1, 0x1));
    EXPECT_TRUE(view.update(0x1000, false, 2, 0x2));
    EXPECT_TRUE(view.update(0x1010, false, 3, 0x3));
    EXPECT_TRUE(view.update(0x1030, false, 4, 0x4));
    EXPECT_TRUE(view.update(0x1040, false, 5, 0x5));
    EXPECT_TRUE(view.update(0x1000, true, 6, 0x6));

    view.invalidate(0x1000, lineMask, false);
    EXPECT_FALSE(view.lookup(0x1000, false, key));
    EXPECT_FALSE(view.lookup(0x1010, false, key));
    EXPECT_FALSE(view.lookup(0x1030, false, key));
    ASSERT_TRUE(view.lookup(0x0ff0, false, key));
    EXPECT_EQ(key, uint64_t{0x1});
    ASSERT_TRUE(view.lookup(0x1040, false, key));
    EXPECT_EQ(key, uint64_t{0x5});
    ASSERT_TRUE(view.lookup(0x1000, true, key));
    EXPECT_EQ(key, uint64_t{0x6});

    view.invalidate(0x1000, lineMask, true);
    EXPECT_FALSE(view.lookup(0x1000, true, key));
    EXPECT_TRUE(view.lookup(0x1040, false, key));
}

TEST(KlpLockViewTest, ClearStartsANewExecutionAttempt)
{
    KlpLockView view;
    uint64_t key = 0;

    EXPECT_TRUE(view.update(0x1000, false, 100, 0xa));
    EXPECT_TRUE(view.update(0x1010, true, 200, 0xb));
    view.clear();
    EXPECT_FALSE(view.lookup(0x1000, false, key));
    EXPECT_FALSE(view.lookup(0x1010, true, key));

    // A replay's new view has no sequence watermark from its old attempt.
    EXPECT_TRUE(view.update(0x1000, false, 1, 0xc));
    EXPECT_TRUE(view.update(0x1010, true, 2, 0xd));
    ASSERT_TRUE(view.lookup(0x1000, false, key));
    EXPECT_EQ(key, uint64_t{0xc});
    ASSERT_TRUE(view.lookup(0x1010, true, key));
    EXPECT_EQ(key, uint64_t{0xd});
}

} // namespace o3
} // namespace gem5

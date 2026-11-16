//
// Native unit tests for the connection guard's rules (src/core/GuardLogic.h).
//
#include <unity.h>

#include "../../src/core/GuardLogic.h"

void setUp() {}
void tearDown() {}

static void test_one_connection_always() {
    TEST_ASSERT_EQUAL(1, guard::connectionLimit(0));
    TEST_ASSERT_EQUAL(1, guard::connectionLimit(guard::kReserve));
    TEST_ASSERT_EQUAL(1, guard::connectionLimit(guard::kReserve + 2 * guard::kPerConnection - 1));
}

static void test_limit_follows_free_heap() {
    TEST_ASSERT_EQUAL(2, guard::connectionLimit(guard::kReserve + 2 * guard::kPerConnection));
    TEST_ASSERT_EQUAL(4, guard::connectionLimit(38 * 1024));   // btn_a00001 when idle
    TEST_ASSERT_EQUAL(7, guard::connectionLimit(58 * 1024));   // btn_a00002 when idle
    TEST_ASSERT_EQUAL(guard::kMaxConnections, guard::connectionLimit(1024 * 1024));
    TEST_ASSERT_EQUAL(guard::kMaxConnections, guard::connectionLimit(0xFFFFFFFFu));
}

static void test_limit_never_decreases_with_more_heap() {
    uint8_t last = 0;
    for (uint32_t heap = 0; heap < 200 * 1024; heap += 512) {
        const uint8_t n = guard::connectionLimit(heap);
        TEST_ASSERT_TRUE(n >= last && n >= 1 && n <= guard::kMaxConnections);
        last = n;
    }
}

static void test_bodies() {
    TEST_ASSERT_TRUE(guard::bodyAllowed(true, "/app/scripts/main.js"));
    TEST_ASSERT_TRUE(guard::bodyAllowed(false, "/upload"));
    TEST_ASSERT_TRUE(guard::bodyAllowed(false, "/api/bytecode/upload"));
    TEST_ASSERT_TRUE(guard::bodyAllowed(false, "/connect"));
    TEST_ASSERT_FALSE(guard::bodyAllowed(false, "/api/reboot"));
    TEST_ASSERT_FALSE(guard::bodyAllowed(false, "/upload/"));
    TEST_ASSERT_FALSE(guard::bodyAllowed(false, "/nonexistent"));
    TEST_ASSERT_FALSE(guard::bodyAllowed(false, "/"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_one_connection_always);
    RUN_TEST(test_limit_follows_free_heap);
    RUN_TEST(test_limit_never_decreases_with_more_heap);
    RUN_TEST(test_bodies);
    return UNITY_END();
}

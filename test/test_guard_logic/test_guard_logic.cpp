//
// Native unit tests for the request guard's rules (src/core/GuardLogic.h).
//
#include <unity.h>

#include "../../src/core/GuardLogic.h"

void setUp() {}
void tearDown() {}

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
    RUN_TEST(test_bodies);
    return UNITY_END();
}

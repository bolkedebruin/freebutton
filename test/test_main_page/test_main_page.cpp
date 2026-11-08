//
// Native unit tests for the main display's page state (src/ui/MainPage.*):
// which items a page shows and what showPage() accepts. The .cpp files are
// included directly (same pattern as test_ha_slug).
//
#include <unity.h>

#include "../../src/ui/MainPage.cpp"
#include "../../src/ui/PendingRender.cpp"

using namespace freebutton::ui;

namespace {
// A widget that records whether a render was requested.
class Probe final : public PendingRender {
public:
    int applied = 0;
    bool applyPending() override { ++applied; return false; }
    void link() { linkPending(); }
    void unlink() { unlinkPending(); }
};
} // namespace

void setUp() {}
void tearDown() {}

void test_every_page_item_is_always_visible() {
    TEST_ASSERT_TRUE(visibleOnPage(kEveryPage, 1));
    TEST_ASSERT_TRUE(visibleOnPage(kEveryPage, 2));
}

void test_paged_item_only_on_its_page() {
    TEST_ASSERT_TRUE(visibleOnPage(1, 1));
    TEST_ASSERT_FALSE(visibleOnPage(1, 2));
    TEST_ASSERT_TRUE(visibleOnPage(2, 2));
    TEST_ASSERT_FALSE(visibleOnPage(2, 1));
}

void test_show_page_and_refuse_below_one() {
    TEST_ASSERT_EQUAL_INT(kFirstPage, shownPage());   // runs before any showPage()
    TEST_ASSERT_TRUE(showPage(2));
    TEST_ASSERT_EQUAL_INT(2, shownPage());
    TEST_ASSERT_FALSE(showPage(0));
    TEST_ASSERT_FALSE(showPage(-1));
    TEST_ASSERT_EQUAL_INT(2, shownPage());
    showPage(kFirstPage);
}

void test_page_change_requests_a_render_once() {
    Probe probe;
    probe.link();
    applyPendingRenders();       // drain anything pending
    probe.applied = 0;

    showPage(2);
    applyPendingRenders();
    TEST_ASSERT_EQUAL_INT(1, probe.applied);

    showPage(2);                 // same page: nothing to render
    applyPendingRenders();
    TEST_ASSERT_EQUAL_INT(1, probe.applied);

    probe.unlink();
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_page_item_is_always_visible);
    RUN_TEST(test_paged_item_only_on_its_page);
    RUN_TEST(test_show_page_and_refuse_below_one);
    RUN_TEST(test_page_change_requests_a_render_once);
    return UNITY_END();
}

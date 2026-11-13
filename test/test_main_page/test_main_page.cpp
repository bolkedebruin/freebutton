//
// Native unit tests for the main display's page state (src/ui/MainPage.*):
// which items a page shows and what showPage() accepts. The .cpp files are
// included directly (same pattern as test_ha_slug).
//
#include <string>
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

// A widget of one page: records the order of the applies.
static std::string g_order;
class PagedProbe final : public PendingRender {
public:
    PagedProbe(const char tag, const int page) : tag_(tag), page_(page) {}
    bool applyPending() override { g_order += tag_; return false; }
    bool shownNow() const override { return visibleOnPage(page_, shownPage()); }
    void link() { linkPending(); }
    void unlink() { unlinkPending(); }
private:
    char tag_;
    int page_;
};

// On a page change the widgets of the page that goes release their LVGL
// memory before the widgets of the page that comes build, whatever the list
// order (page 1 is listed before page 3 in a config).
void test_hidden_widgets_apply_before_shown_ones() {
    PagedProbe a('a', 1), b('b', 1), c('c', 3), d('d', 3), every('e', kEveryPage);
    a.link(); b.link(); c.link(); d.link(); every.link();
    showPage(3);
    g_order.clear();
    requestPendingRender();
    applyPendingRenders();
    TEST_ASSERT_EQUAL_STRING("abcde", g_order.c_str());   // page 1 releases, then 3 and "every" build
    showPage(1);
    g_order.clear();
    applyPendingRenders();
    TEST_ASSERT_EQUAL_STRING("cdabe", g_order.c_str());   // page 3 releases first
    a.unlink(); b.unlink(); c.unlink(); d.unlink(); every.unlink();
}

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
    RUN_TEST(test_hidden_widgets_apply_before_shown_ones);
    return UNITY_END();
}

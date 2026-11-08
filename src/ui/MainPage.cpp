//
// MainPage: see MainPage.h.
//

#include "MainPage.h"
#include "PendingRender.h"

#include <atomic>

namespace freebutton::ui {

namespace {
std::atomic<int> g_shownPage{kFirstPage};
} // namespace

int shownPage() {
    return g_shownPage.load(std::memory_order_acquire);
}

bool showPage(const int page) {
    if (page < kFirstPage) {
        return false;
    }
    if (g_shownPage.exchange(page, std::memory_order_acq_rel) != page) {
        requestPendingRender();
    }
    return true;
}

} // namespace freebutton::ui

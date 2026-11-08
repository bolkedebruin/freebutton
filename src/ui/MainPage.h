//
// MainPage: which page of the main display is shown.
//
// Display items and the clock carry a "page" in config.json (Button+ format):
// pages are numbered from 1, and page 0 means "every page". An item is shown
// when its page is 0 or the shown page. The shown page starts at 1 and only
// changes when a script calls fb.main.display.showPage().
//
// showPage() runs in any task: it stores the page and asks the LVGL task to
// re-render (PendingRender). Each widget hides or shows itself in its
// applyPending() under the LVGL lock, so no lock is taken here and nothing
// is allocated.
//

#ifndef FREEBUTTON_MAINPAGE_H
#define FREEBUTTON_MAINPAGE_H

namespace freebutton::ui {

// Item page meaning "shown on every page".
constexpr int kEveryPage = 0;
// Page shown at boot.
constexpr int kFirstPage = 1;

constexpr bool visibleOnPage(const int itemPage, const int shownPage) {
    return itemPage == kEveryPage || itemPage == shownPage;
}

// Any task.
int shownPage();
// Any task. Pages below kFirstPage are refused (returns false).
bool showPage(int page);

} // namespace freebutton::ui

#endif // FREEBUTTON_MAINPAGE_H

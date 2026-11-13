//
// PendingRender: widgets whose text other tasks change, brought up to date by
// the LVGL task once per tick.
//
// Other tasks (MQTT, JS) never render. They store the new text in a TextSlot
// (src/utils/TextSlot.h) and call requestPendingRender(). The LVGL task calls
// applyPendingRenders() under the LVGL lock it already holds, before
// lv_timer_handler(), so the new text is drawn in the same tick.
//
// The list is intrusive (one pointer per widget, no heap) and only touched
// with the LVGL lock held: linkPending() when the widget's LVGL object is
// created, unlinkPending() before it is deleted. Deleting a widget therefore
// cannot race the walk.
//

#ifndef FREEBUTTON_PENDINGRENDER_H
#define FREEBUTTON_PENDINGRENDER_H

namespace freebutton::ui {

class PendingRender {
public:
    // LVGL task, LVGL lock held. Render whatever changed since the last call.
    // Returns true if something is still pending (a writer was mid-copy) and
    // the next tick has to look again.
    virtual bool applyPending() = 0;

    // LVGL lock held. False for a widget of a page that is not shown: its
    // applyPending() then only releases its LVGL objects. Those run first
    // (applyPendingRenders), so on a page change the page that goes gives
    // its memory back before the page that comes builds; in list order
    // alone, a page listed earlier in the config was built while the later
    // one still held the pool, and values were left undrawn for lack of it.
    virtual bool shownNow() const { return true; }

protected:
    PendingRender() = default;
    ~PendingRender() = default;
    PendingRender(const PendingRender&) = delete;
    PendingRender& operator=(const PendingRender&) = delete;

    // LVGL lock held. Idempotent.
    void linkPending();
    void unlinkPending();

private:
    friend void applyPendingRenders();
    // With the vtable pointer, 8 bytes per widget.
    PendingRender* nextPending_ = nullptr;
};

// Any task, lock-free, no heap: there is new text to render.
void requestPendingRender();

// LVGL task (from an applyPending()): a text could not be drawn now (the LVGL
// pool was too low); walk the widgets again a little later. Not at once: that
// would spin while the pool stays low.
void requestPendingRenderLater();

// LVGL task, LVGL lock held.
void applyPendingRenders();

} // namespace freebutton::ui

#endif // FREEBUTTON_PENDINGRENDER_H

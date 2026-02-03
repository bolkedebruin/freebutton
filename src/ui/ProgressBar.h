#ifndef PROGRESSBAR_H
#define PROGRESSBAR_H

#include <lvgl.h>
#include <cstdint>
#include <string_view>
#include <memory>

// Forward declaration
namespace FreeButton {
    class LvglDisplay;
}

namespace FreeButton::UI {

class ProgressBar {
public:
    struct Config {
        std::int32_t width = 200;
        std::int32_t height = 3;
        std::int32_t label_offset_y = -30;
        std::uint32_t bg_color = 0x3a3a3c;
        std::uint32_t indicator_color = 0xffffff;
        std::uint32_t text_color = 0xffffff;
        std::int32_t radius = 2;
        const lv_font_t* font = &lv_font_montserrat_14;
        bool blur_background = false;
        std::uint32_t blur_color = 0x000000;
        std::uint8_t blur_opacity = 180;  // 0-255
    };

    explicit ProgressBar(lv_obj_t* parent, const Config& config);
    explicit ProgressBar(lv_obj_t* parent);
    ~ProgressBar();

    ProgressBar(const ProgressBar&) = delete;
    ProgressBar& operator=(const ProgressBar&) = delete;
    ProgressBar(ProgressBar&&) = delete;
    ProgressBar& operator=(ProgressBar&&) = delete;

    void show(std::string_view message = "Initializing...");
    void update(std::int32_t percentage, std::string_view message = {}) const;
    void hide();

    [[nodiscard]] bool is_visible() const noexcept;
    [[nodiscard]] std::int32_t get_progress() const noexcept;

private:
    lv_obj_t* progress_bar_{nullptr};
    lv_obj_t* progress_label_{nullptr};
    lv_obj_t* blur_overlay_{nullptr};
    lv_obj_t* parent_{nullptr};
    Config config_;
    bool visible_{false};

    void create_ui();
    void create_blur_overlay();
    void cleanup();
};

} // namespace FreeButton::UI

#endif // PROGRESSBAR_H
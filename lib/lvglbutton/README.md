# TITLE

DESCRIPTION

## Get started
1. Clone LVGL as submodule into your project:  
`git submodule add https://github.com/lvgl/lvgl.git`
2. Clone this project as submodule into your project:  
`git submodule add https://github.com/lvgl-private/ntsystems-buttonplus.git`
3. Copy the `lv_conf_template.h` as `lv_conf.h` next to the lvgl folder and set the first `#if 0` to `#if 1`.
4. In your `main.c` include:
    - `ui/ui/h`
5. Call the following inits:
```c
  lv_init();

  <your_hardware_init> //It should also register display and input device
	
  ui_init();
```

5. Call `lv_timer_handler()` periodically in a loop or a task
6. Call `lv_tick_inc(ms)` periodically
 
 
## Example
```c
ui_init();

lv_obj_t * box1 = ui_box_create(lv_scr_act());
ui_box_set_caption(box1, "Power");
ui_box_set_text(box1, "23.8");
ui_box_set_hint(box1, "W");
ui_box_set_font_size(box1, 4);
lv_obj_set_pos(box1, 20, 60);
lv_obj_set_width(box1, 180);
```

//
// Created by Bolke de Bruin on 06/08/2025.
//

#ifndef PINS_H

#define BUTTON_SDA 47
#define BUTTON_SCL 48
#define CH1_SDA 1
#define CH1_SCL 2
#define CONFIG_BUTTON 9

// TFT Config
#define TFT_MISO -1
#define TFT_MOSI 35
#define TFT_SCLK 36
#define TFT_DC 37 // Data Command control pin

#define BCKL160 46
#define BCKL320 3
#define NEOPIN 4

#define MCP_SWITCH_LEFT 6
#define MCP_SWITCH_RIGHT 2

#define MCP160_CS_LEFT 5
#define MCP160_CS_RIGHT 1

#define MCP320_CS MCP160_CS_LEFT

#define BLINK_GPIO (gpio_num_t)2

#define PINS_H

#endif //PINS_H

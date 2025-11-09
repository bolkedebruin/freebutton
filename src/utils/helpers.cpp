//
// Created by Bolke de Bruin on 06/08/2025.
//
#include <cstdint>
#include <esp32-hal.h>
#include <iomanip>
#include <string>
#include <stdio.h>

#include "esp_mac.h"
#include "helpers.h"


static uint16_t idCounter = 0;

uint16_t generateId() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    const uint16_t deviceId = (mac[4] << 8) | mac[5];  // Use last 2 bytes of MAC

    const uint32_t timeSeed = millis() & 0xFF;  // Use 8 bits of time seed

    const uint16_t newId = (timeSeed << 8) |
                     ((deviceId & 0xF0) >> 4) |  // Use upper 4 bits of deviceId
                     ((idCounter & 0x0F) << 4);  // Use 4 bits of counter

    idCounter = (idCounter + 1) & 0x0F;  // Increment and wrap counter

    return newId;
}

int gammaCorrection(const int value, const float gamma, const float base) {
	return pow(value / base, gamma) * base;
}

const char *platform_create_id_string()
{
    uint8_t mac[6];
    const auto id_string = static_cast<char *>(calloc(1, 32));
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(id_string, "btn_%02x%02x%02x", mac[3], mac[4], mac[5]);
    return id_string;
}

std::string roundme(const std::string_view input, const int precision) {
	// Early return for empty input
	if (input.empty()) {
		return {};
	}

	// First pass: validate input is a valid number
	bool has_digit = false;
	bool has_decimal = false;
	size_t start_idx = 0;

	// Check for leading sign
	if (input[0] == '-' || input[0] == '+') {
		start_idx = 1;
	}

	// Validate remaining characters
	for (size_t i = start_idx; i < input.length(); ++i) {
		if (std::isdigit(input[i])) {
			has_digit = true;
		} else if (input[i] == '.') {
			if (has_decimal) {  // Multiple decimal points invalid
				return std::string(input);
			}
			has_decimal = true;
		} else {  // Invalid character found
			return std::string(input);
		}
	}

	if (!has_digit) {  // No digits found
		return std::string(input);
	}

	// Convert to double without exceptions
	char* end_ptr;
	double value = std::strtod(input.data(), &end_ptr);

	// Check if entire string was converted
	if (end_ptr != input.data() + input.length()) {
		return std::string(input);
	}

	// Format the result
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(precision) << value;
	return oss.str();
}

void generateMacBasedPassword(char* buffer, size_t bufferSize, const uint8_t* mac) {
    if (bufferSize < 9 || !buffer || !mac) return; // Ensure buffer is large enough and pointers are valid

    // Create password: "fb" + last 6 hex digits of MAC address
    snprintf(buffer, bufferSize, "fb%02x%02x%02x", mac[3], mac[4], mac[5]);
}

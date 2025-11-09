//
// Created by Bolke de Bruin on 06/08/2025.
//

#ifndef HELPERS_H
#define HELPERS_H

#include <string>
#include <stddef.h>
#include <stdint.h>

uint16_t generateId();
int gammaCorrection(int value, float gamma, float base);
const char *platform_create_id_string();

std::string roundme(std::string_view input, int precision);

/**
 * @brief Generate unique password based on MAC address
 * @param buffer Buffer to store the generated password (must be at least 9 bytes)
 * @param bufferSize Size of the buffer
 * @param mac MAC address bytes (6 bytes)
 */
void generateMacBasedPassword(char* buffer, size_t bufferSize, const uint8_t* mac);

#endif //HELPERS_H

//
// Created by Bolke de Bruin on 06/08/2025.
//

#ifndef HELPERS_H
#define HELPERS_H

#include <string>

uint16_t generateId();
int gammaCorrection(int value, float gamma, float base);
const char *platform_create_id_string();

std::string roundme(std::string_view input, int precision);

#endif //HELPERS_H

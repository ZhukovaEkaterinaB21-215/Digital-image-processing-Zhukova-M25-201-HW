#pragma once

#include "image.h"

bool saveBMP(const char* filename, const Image& img);
bool saveBinaryBMP(const char* filename, const Image& img, uint8_t thresh);

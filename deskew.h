#pragma once

#include "image.h"

uint8_t otsuThreshold(const Image& img);
Image   removeNonTextObjects(const Image& bin, uint8_t thresh);
double  detectSkewAngle(const Image& bin, uint8_t thresh);
Image   rotateImage(const Image& src, double angleDeg);

#ifndef MATRIX_INDICATOR_H
#define MATRIX_INDICATOR_H

#include <Arduino.h>

void initMatrixIndicator();
void updateMatrixIndicator(float rpm, int8_t ebar, bool carDim, bool evMode, bool displayOff);

#endif

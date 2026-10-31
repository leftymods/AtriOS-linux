#pragma once

#include "tl_common.h"

void mfgInit();

void mfgStart();

void mfgStop();

void mfgSetChannel(uint8_t channel);

void mfgSetPower(int8_t power);

void mfgSendPacket(const uint8_t* data, uint8_t length);

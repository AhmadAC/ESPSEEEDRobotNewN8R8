// main\cam_controller.h
#pragma once
#include <stdbool.h>

extern volatile bool isCapturing;

void cam_controller_init();
void cam_espnow_init();
void cam_espnow_pair_claw();
void cam_toggle_flip();
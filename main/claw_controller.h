#pragma once

extern char claw_last_command_str[32];
extern int claw_current_angle;

void claw_controller_init();
void claw_set_angle(int angle);
void claw_execute_command(const char* cmd);
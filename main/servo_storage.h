#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct ServoPose {
    int32_t ll, hr, hl, lr;
};

void servo_storage_init(void);
void servo_storage_get_targets(int *ll, int *hr, int *hl, int *lr);
void servo_storage_set_targets(int ll, int hr, int hl, int lr);
void servo_storage_set_target_by_id(const char *id, int angle);
int32_t servo_storage_get_target_by_id(const char *id);

void servo_storage_get_offsets(int *ll, int *hr, int *hl, int *lr);
void servo_storage_set_offsets(int ll, int hr, int hl, int lr, bool save_to_nvs);

ServoPose servo_storage_get_pose(int index);
const char* servo_storage_get_pose_name(int index);
int servo_storage_find_pose_index(const char *name);
void servo_storage_set_pose(int index, int ll, int hr, int hl, int lr, bool save_to_nvs);

void servo_storage_set_leg_names(const char *hl, const char *hr, const char *ll, const char *lr);
void servo_storage_get_leg_names(char *hl, char *hr, char *ll, char *lr, size_t max_len);

char* servo_storage_get_json(void);
char* servo_storage_get_cpp(void);
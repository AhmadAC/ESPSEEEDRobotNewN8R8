// main\servo_storage.cpp
#include "servo_storage.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int32_t s_tgt_ll = 90, s_tgt_hr = 90, s_tgt_hl = 90, s_tgt_lr = 90;
static int32_t s_off_ll = 0,  s_off_hr = 0,  s_off_hl = 0,  s_off_lr = 11;

static char s_nm_hl[32] = "Front Left (HL)";
static char s_nm_hr[32] = "Front Right (HR)";
static char s_nm_ll[32] = "Back Left (LL)";
static char s_nm_lr[32] = "Back Right (LR)";

static ServoPose s_poses[5] = {
    {0, 0, 0, 0},        // 0: sit
    {90, 90, 90, 90},    // 1: stand
    {90, 180, 180, 90},  // 2: stretch_down
    {0, 90, 90, 0},      // 3: stretch_back
    {90, 90, 90, 90}     // 4: stop
};

static const char* s_pose_names[5] = {"sit", "stand", "stretch_down", "stretch_back", "stop"};
static const char* s_pose_keys[5]  = {"sit", "std", "sdn", "sbk", "stp"};

void servo_storage_init(void) {
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_nm_hl); nvs_get_str(h, "nm_hl", s_nm_hl, &len);
        len = sizeof(s_nm_hr); nvs_get_str(h, "nm_hr", s_nm_hr, &len);
        len = sizeof(s_nm_ll); nvs_get_str(h, "nm_ll", s_nm_ll, &len);
        len = sizeof(s_nm_lr); nvs_get_str(h, "nm_lr", s_nm_lr, &len);

        nvs_get_i32(h, "ll_off", &s_off_ll);
        nvs_get_i32(h, "hr_off", &s_off_hr);
        nvs_get_i32(h, "hl_off", &s_off_hl);
        nvs_get_i32(h, "lr_off", &s_off_lr);

        for (int i = 0; i < 5; i++) {
            char key[16];
            snprintf(key, sizeof(key), "p_%s_ll", s_pose_keys[i]); nvs_get_i32(h, key, &s_poses[i].ll);
            snprintf(key, sizeof(key), "p_%s_hr", s_pose_keys[i]); nvs_get_i32(h, key, &s_poses[i].hr);
            snprintf(key, sizeof(key), "p_%s_hl", s_pose_keys[i]); nvs_get_i32(h, key, &s_poses[i].hl);
            snprintf(key, sizeof(key), "p_%s_lr", s_pose_keys[i]); nvs_get_i32(h, key, &s_poses[i].lr);
        }
        nvs_close(h);
    }
}

void servo_storage_get_targets(int *ll, int *hr, int *hl, int *lr) {
    if (ll) *ll = (int)s_tgt_ll;
    if (hr) *hr = (int)s_tgt_hr;
    if (hl) *hl = (int)s_tgt_hl;
    if (lr) *lr = (int)s_tgt_lr;
}

void servo_storage_set_targets(int ll, int hr, int hl, int lr) {
    s_tgt_ll = ll; s_tgt_hr = hr; s_tgt_hl = hl; s_tgt_lr = lr;
}

void servo_storage_set_target_by_id(const char *id, int angle) {
    if (strcmp(id, "low_left") == 0)   s_tgt_ll = angle;
    if (strcmp(id, "high_right") == 0) s_tgt_hr = angle;
    if (strcmp(id, "high_left") == 0)  s_tgt_hl = angle;
    if (strcmp(id, "low_right") == 0)  s_tgt_lr = angle;
    if (strcmp(id, "all") == 0) {
        s_tgt_ll = s_tgt_hr = s_tgt_hl = s_tgt_lr = angle;
    }
}

int32_t servo_storage_get_target_by_id(const char *id) {
    if (strcmp(id, "low_left") == 0)   return s_tgt_ll;
    if (strcmp(id, "high_right") == 0) return s_tgt_hr;
    if (strcmp(id, "high_left") == 0)  return s_tgt_hl;
    if (strcmp(id, "low_right") == 0)  return s_tgt_lr;
    return 90;
}

void servo_storage_get_offsets(int *ll, int *hr, int *hl, int *lr) {
    if (ll) *ll = (int)s_off_ll;
    if (hr) *hr = (int)s_off_hr;
    if (hl) *hl = (int)s_off_hl;
    if (lr) *lr = (int)s_off_lr;
}

void servo_storage_set_offsets(int ll, int hr, int hl, int lr, bool save_to_nvs) {
    s_off_ll = ll; s_off_hr = hr; s_off_hl = hl; s_off_lr = lr;
    if (save_to_nvs) {
        nvs_handle_t h;
        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_i32(h, "ll_off", s_off_ll);
            nvs_set_i32(h, "hr_off", s_off_hr);
            nvs_set_i32(h, "hl_off", s_off_hl);
            nvs_set_i32(h, "lr_off", s_off_lr);
            nvs_commit(h);
            nvs_close(h);
        }
    }
}

ServoPose servo_storage_get_pose(int index) {
    if (index >= 0 && index < 5) return s_poses[index];
    return s_poses[1];
}

const char* servo_storage_get_pose_name(int index) {
    if (index >= 0 && index < 5) return s_pose_names[index];
    return "stand";
}

int servo_storage_find_pose_index(const char *name) {
    for (int i = 0; i < 5; i++) {
        if (strcmp(name, s_pose_names[i]) == 0) return i;
    }
    return -1;
}

void servo_storage_set_pose(int index, int ll, int hr, int hl, int lr, bool save_to_nvs) {
    if (index < 0 || index >= 5) return;
    s_poses[index].ll = ll; s_poses[index].hr = hr;
    s_poses[index].hl = hl; s_poses[index].lr = lr;

    if (save_to_nvs) {
        nvs_handle_t h;
        if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
            char key[16];
            snprintf(key, sizeof(key), "p_%s_ll", s_pose_keys[index]); nvs_set_i32(h, key, s_poses[index].ll);
            snprintf(key, sizeof(key), "p_%s_hr", s_pose_keys[index]); nvs_set_i32(h, key, s_poses[index].hr);
            snprintf(key, sizeof(key), "p_%s_hl", s_pose_keys[index]); nvs_set_i32(h, key, s_poses[index].hl);
            snprintf(key, sizeof(key), "p_%s_lr", s_pose_keys[index]); nvs_set_i32(h, key, s_poses[index].lr);
            nvs_commit(h);
            nvs_close(h);
        }
    }
}

void servo_storage_set_leg_names(const char *hl, const char *hr, const char *ll, const char *lr) {
    if (hl && strlen(hl) > 0) strncpy(s_nm_hl, hl, sizeof(s_nm_hl) - 1);
    if (hr && strlen(hr) > 0) strncpy(s_nm_hr, hr, sizeof(s_nm_hr) - 1);
    if (ll && strlen(ll) > 0) strncpy(s_nm_ll, ll, sizeof(s_nm_ll) - 1);
    if (lr && strlen(lr) > 0) strncpy(s_nm_lr, lr, sizeof(s_nm_lr) - 1);

    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "nm_hl", s_nm_hl);
        nvs_set_str(h, "nm_hr", s_nm_hr);
        nvs_set_str(h, "nm_ll", s_nm_ll);
        nvs_set_str(h, "nm_lr", s_nm_lr);
        nvs_commit(h);
        nvs_close(h);
    }
}

void servo_storage_get_leg_names(char *hl, char *hr, char *ll, char *lr, size_t max_len) {
    if (hl) strncpy(hl, s_nm_hl, max_len);
    if (hr) strncpy(hr, s_nm_hr, max_len);
    if (ll) strncpy(ll, s_nm_ll, max_len);
    if (lr) strncpy(lr, s_nm_lr, max_len);
}

char* servo_storage_get_json(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *off = cJSON_CreateObject();
    cJSON_AddNumberToObject(off, "ll", s_off_ll);
    cJSON_AddNumberToObject(off, "hr", s_off_hr);
    cJSON_AddNumberToObject(off, "hl", s_off_hl);
    cJSON_AddNumberToObject(off, "lr", s_off_lr);
    cJSON_AddItemToObject(root, "offsets", off);

    for (int i = 0; i < 5; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "ll", s_poses[i].ll);
        cJSON_AddNumberToObject(p, "hr", s_poses[i].hr);
        cJSON_AddNumberToObject(p, "hl", s_poses[i].hl);
        cJSON_AddNumberToObject(p, "lr", s_poses[i].lr);
        cJSON_AddItemToObject(root, s_pose_names[i], p);
    }
    char *res = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return res;
}

char* servo_storage_get_cpp(void) {
    char *buf = (char*)malloc(1024);
    if (!buf) return NULL;
    snprintf(buf, 1024,
        "// ESPRobot Calibration Defaults (ESP32-S3 Zero)\n"
        "static int32_t offset_low_left   = %ld;\n"
        "static int32_t offset_high_right = %ld;\n"
        "static int32_t offset_high_left  = %ld;\n"
        "static int32_t offset_low_right  = %ld;\n\n"
        "struct Pose { int32_t ll, hr, hl, lr; };\n"
        "static Pose poses[5] = {\n"
        "    {%ld, %ld, %ld, %ld},  // sit\n"
        "    {%ld, %ld, %ld, %ld},  // stand\n"
        "    {%ld, %ld, %ld, %ld},  // stretch_down\n"
        "    {%ld, %ld, %ld, %ld},  // stretch_back\n"
        "    {%ld, %ld, %ld, %ld}   // stop\n"
        "};\n",
        (long)s_off_ll, (long)s_off_hr, (long)s_off_hl, (long)s_off_lr,
        (long)s_poses[0].ll, (long)s_poses[0].hr, (long)s_poses[0].hl, (long)s_poses[0].lr,
        (long)s_poses[1].ll, (long)s_poses[1].hr, (long)s_poses[1].hl, (long)s_poses[1].lr,
        (long)s_poses[2].ll, (long)s_poses[2].hr, (long)s_poses[2].hl, (long)s_poses[2].lr,
        (long)s_poses[3].ll, (long)s_poses[3].hr, (long)s_poses[3].hl, (long)s_poses[3].lr,
        (long)s_poses[4].ll, (long)s_poses[4].hr, (long)s_poses[4].hl, (long)s_poses[4].lr
    );
    return buf;
}
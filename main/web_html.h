// main\web_html.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------------------------------------
// DYNAMIC CAM HTML PAYLOADS
// -------------------------------------------------------------
extern const char HTML_CAM_SETUP[];
extern const char HTML_CAM_APP[];
extern const char HTML_CAM_BASIC[];

// -------------------------------------------------------------
// DYNAMIC CLAW HTML PAYLOADS
// -------------------------------------------------------------
extern const char HTML_CLAW_UI[];
extern const char HTML_CLAW_BASIC[];

// -------------------------------------------------------------
// DYNAMIC ROBOT HTML PARTITIONS & FALLBACK
// -------------------------------------------------------------
extern const char html_part1[];
extern const char html_cam_card[];
extern const char html_audio_card[];
extern const char html_part2[];
extern const char html_audio_tripped[];
extern const char html_part3[];
extern const char html_audio_cleared[];
extern const char html_part4[];
extern const char HTML_ROBOT_BASIC[];

#ifdef __cplusplus
}
#endif
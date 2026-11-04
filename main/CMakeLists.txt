# Locate dog-barking.wav across case differences on Linux CI runners
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../Sound/dog-barking.wav")
    set(AUDIO_FILE "${CMAKE_CURRENT_LIST_DIR}/../Sound/dog-barking.wav")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../sound/dog-barking.wav")
    set(AUDIO_FILE "${CMAKE_CURRENT_LIST_DIR}/../sound/dog-barking.wav")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/dog-barking.wav")
    set(AUDIO_FILE "${CMAKE_CURRENT_LIST_DIR}/dog-barking.wav")
else()
    file(GLOB_RECURSE SEARCH_WAV "${CMAKE_CURRENT_LIST_DIR}/../*dog-barking.wav")
    if(SEARCH_WAV)
        list(GET SEARCH_WAV 0 AUDIO_FILE)
    else()
        set(AUDIO_FILE "../Sound/dog-barking.wav")
    endif()
endif()

idf_component_register(SRCS "main.cpp"
                            "logger.cpp"
                            "wifi_manager.cpp"
                            "sensor_monitor.cpp"
                            "servo_controller.cpp"
                            "claw_controller.cpp"
                            "cam_controller.cpp"
                            "web_server.cpp"
                            "web_html_cam.cpp"
                            "web_html_claw.cpp"
                            "web_html_robot.cpp"
                            "ble_manager.cpp"
                            "audio_player.cpp"
                    INCLUDE_DIRS "."
                    EMBED_FILES "${AUDIO_FILE}")
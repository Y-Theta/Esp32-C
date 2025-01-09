file(GLOB_RECURSE HAL_SRCS
    ../components/ESP32_FTPClient/*.c
    ../components/ESP32_FTPClient/*.cpp
    ./camera/*.c
    ./camera/*.cpp
    ./mic/*.c
    ./mic/*.cpp
)

set(APP_LAYER_INCS
    ../components/ESP32_FTPClient/src
)

idf_component_register(SRCS "main.cpp" ${HAL_SRCS}
                    INCLUDE_DIRS "." ${APP_LAYER_INCS})
# get IDF version for comparison
set(idf_version "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")

# set conversion sources
set(COMPONENT_SRCS)

if(CONFIG_CAMERA_CONVERTER_ENABLED)
  list(APPEND COMPONENT_SRCS
    conversions/yuv.c
    conversions/to_jpg.cpp
    conversions/to_bmp.c
    conversions/jpge.cpp
    conversions/esp_jpg_decode.c
    )
endif()

set(COMPONENT_PRIV_INCLUDEDIRS
  conversions/private_include
  )

set(COMPONENT_ADD_INCLUDEDIRS
  driver/include
  conversions/include
  )

set(COMPONENT_REQUIRES driver)

# set driver sources only for supported platforms
if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
  list(APPEND COMPONENT_SRCS
    driver/esp_camera.c
    driver/cam_hal.c
    driver/sccb.c
    driver/sensor.c
    )
  
  if(CONFIG_OV2640_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/ov2640.c)
  endif()
  
  if(CONFIG_OV3660_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/ov3660.c)
  endif()
  
  if(CONFIG_OV5640_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/ov5640.c)
  endif()
  
  if(CONFIG_OV7725_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/ov7725.c)
  endif()
  
  if(CONFIG_OV7670_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/ov7670.c)
  endif()
  
  if(CONFIG_NT99141_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/nt99141.c)
  endif()
  
  if(CONFIG_GC0308_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/gc0308.c)
  endif()
  
  if(CONFIG_GC2145_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/gc2145.c)
  endif()
  
  if(CONFIG_GC032A_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/gc032a.c)
  endif()
  
  if(CONFIG_BF3005_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/bf3005.c)
  endif()
  
  if(CONFIG_BF20A6_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/bf20a6.c)
  endif()
  
  if(CONFIG_SC101IOT_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/sc101iot.c)
  endif()
  
  if(CONFIG_SC030IOT_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/sc030iot.c)
  endif()
  
  if(CONFIG_SC031GS_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/sc031gs.c)
  endif()
  
  if(CONFIG_MEGA_CCM_SUPPORT)
    list(APPEND COMPONENT_SRCS sensors/mega_ccm.c)
  endif()

  list(APPEND COMPONENT_PRIV_INCLUDEDIRS
    driver/private_include
    sensors/private_include
    target/private_include
    )

  if(IDF_TARGET STREQUAL "esp32")
    list(APPEND COMPONENT_SRCS
      target/xclk.c
      target/esp32/ll_cam.c
      )
  endif()

  if(IDF_TARGET STREQUAL "esp32s2")
    list(APPEND COMPONENT_SRCS
      target/xclk.c
      target/esp32s2/ll_cam.c
      target/tjpgd.c
      )

    list(APPEND COMPONENT_PRIV_INCLUDEDIRS
      target/esp32s2/private_include
      )
  endif()

  if(IDF_TARGET STREQUAL "esp32s3")
    list(APPEND COMPONENT_SRCS
      target/esp32s3/ll_cam.c
      )
  endif()

  set(COMPONENT_PRIV_REQUIRES freertos nvs_flash)

  set(min_version_for_esp_timer "4.2")
  if (idf_version VERSION_GREATER_EQUAL min_version_for_esp_timer)
    list(APPEND COMPONENT_PRIV_REQUIRES esp_timer)
  endif()

endif()

# CONFIG_ESP_ROM_HAS_JPEG_DECODE is available from IDF v4.4 but
# previous IDF supported chips already support JPEG decoder, hence okay to use this
if(idf_version VERSION_GREATER_EQUAL "4.4" AND NOT CONFIG_ESP_ROM_HAS_JPEG_DECODE)
  list(APPEND COMPONENT_SRCS
    target/tjpgd.c
  )
  list(APPEND COMPONENT_PRIV_INCLUDEDIRS
    target/jpeg_include/
  )
endif()

register_component()
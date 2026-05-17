if(CONFIG_ZMK_RGB_PLUS)
  target_sources(app PRIVATE src/zmk_rgb_plus.c)
  target_include_directories(app PRIVATE include)
endif()

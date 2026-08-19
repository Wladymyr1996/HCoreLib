#######################################################################
# Devices - I2C peripheral drivers, split out of HCoreLib proper.
#
# Everything here talks to real hardware through the IDF I2C master driver, so
# the whole component is target-only and is left out of host builds. Keeping it
# separate also means an application that drives no peripherals never compiles
# a font table it will not use.
#######################################################################

if(NOT ESP_PLATFORM)
  # Host builds have no <driver/i2c_master.h>; there is nothing to offer them.
  return()
endif()

set(HDEVICES_SRCS
    "HAHT20/HAHT20.cpp"
    "HBMP280/HBMP280.cpp"
    "HSSD1306/HIFont/HIFont.cpp"
    "HSSD1306/HSSD1306Panel/HSSD1306Panel.cpp"
    "HSSD1306/HUtf8/HUtf8.cpp"
    "HSSD1306/fonts/HFont6x8/HFont6x8.cpp"
    "HSSD1306/fonts/HFont8x16/HFont8x16.cpp"
    "HSSD1306/fonts/HFont24x32/HFont24x32.cpp")

# Both sensors BLOCK while a conversion runs (HSystemUtils::sleep), which is why
# the application gives each of them a task of its own rather than a slot on the
# shared update() tick - see HITickable. Their non-blocking startMeasurement() /
# isBusy() / collect() split is there for anything that would rather poll.
#
# HSSD1306 itself is a header-only template (the panel size is a template
# parameter), which is why only its helpers appear above.
idf_component_register(SRCS ${HDEVICES_SRCS}
                       INCLUDE_DIRS "."
                       # HCoreLib for HLog, esp_driver_i2c for the bus handles
                       # that appear in the drivers' public headers.
                       REQUIRES HCoreLib esp_driver_i2c)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

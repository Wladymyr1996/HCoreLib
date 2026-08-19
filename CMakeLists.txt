#######################################################################
# HCoreLib - the project's reusable library, consumed as a git submodule.
#
# It builds either as an ESP-IDF component (the target build) or as a plain
# static library (host tooling and unit tests), which is why every module
# splits its platform-specific parts behind IS_MCU / IS_DESKTOP from HCoreLib.h.
#
# Devices/ is a component of its own (see Devices/CMakeLists.txt): it is the
# only part of HCoreLib that talks to peripherals, so an application that drives
# no hardware should not have to build or link it.
#######################################################################

set(HCORELIB_SRCS
    "HBootMode/HBootMode.cpp"
    "HButton/HButton.cpp"
    "HConfig/HConfig.cpp"
    "HConfig/HConfigParser.cpp"
    "HConfig/HConfigPath.cpp"
    "HConfig/HConfigWriter.cpp"
    "HFs/HFs.cpp"
    "HFs/HIFile.cpp"
    "HFs/HIFs.cpp"
    "HGpioManager/HGpioBackend/HGpioBackend.cpp"
    "HGpioManager/HGpioManager.cpp"
    "HGpioManager/HGpioPin/HGpioPin.cpp"
    "HGpioManager/HIGpio/HIGpio.cpp"
    "HITickable/HITickable.cpp"
    # HIWatchable is what HTaskManager watches. Its only implementer is
    # target-only, but the interface itself is platform-independent, so it is
    # built here as well - a file nothing on this platform includes is still a
    # file the host compiler will tell us about.
    "HTaskManager/HIWatchable/HIWatchable.cpp"
    "HAuth/HAuth.cpp"
    "HRtcStore/HRtcStore.cpp"
    "HSleep/HSleep.cpp"
    "HLog/HLog.cpp"
    "HLog/HILogBackend/HILogBackend.cpp"
    "HLog/HConsoleLogBackend/HConsoleLogBackend.cpp"
    "HJson/HJsonDocument.cpp"
    "HJson/HJsonPool.cpp"
    "HJson/HJsonValue.cpp"
    "HValue/HValue.cpp"
    "Utils/HSha256/HSha256.cpp"
    "Utils/HSystemUtils/HSystemUtils.cpp"
    "Utils/HTimer/HTimer.cpp")

# Exactly one backend of each platform-split module is compiled in, chosen the
# same way the facades choose which one to declare - there is no runtime switch
# and no dead platform code in the image.
if(ESP_PLATFORM)
  list(APPEND HCORELIB_SRCS
       "HFs/LittleFs/HLittleFs.cpp"
       "HFs/LittleFs/HLittleFsFile.cpp"
       "HGpioManager/HGpioEsp32/HGpioEsp32.cpp"
       # HTask wraps a FreeRTOS task, which the host has no equivalent of.
       "HTask/HTask.cpp"
       "HTaskManager/HTaskManager.cpp")
else()
  list(APPEND HCORELIB_SRCS
       "HFs/Desktop/HFsDesktop.cpp"
       "HFs/Desktop/HFsDesktopFile.cpp"
       "HGpioManager/HGpioDesktop/HGpioDesktop.cpp")
endif()

# "." serves the root <HCoreLib.h> as well as <HLog/HLog.hpp>,
# <HValue/HValue.hpp> and the rest; "Utils" serves the flat
# <HSystemUtils/HSystemUtils.hpp> and <HTimer/HTimer.hpp> spellings those
# modules use; "EtlProfile" is where ETL finds the configuration header it looks
# for by name.
set(HCORELIB_INCLUDE_DIRS "." "Utils" "EtlProfile")

# ---------------------------------------------------------------------------
# ETL
#
# Header-only, so it needs INCLUDE DIRECTORIES and nothing else - no component,
# no target, no library to link. It used to be wrapped in one, which bought
# nothing but a directory: upstream ETL has no ESP-IDF support of its own, and
# handing over two include paths does not need a component to do it.
#
# Searched for inside HCoreLib first, which is where a self-contained submodule
# would keep its own dependency, then beside the project.
# ---------------------------------------------------------------------------

foreach(candidate "${CMAKE_CURRENT_LIST_DIR}/3rdParty/etl"
                  "${CMAKE_CURRENT_LIST_DIR}/../3rdParty/etl")
  if(EXISTS "${candidate}/include/etl/vector.h")
    set(ETL_DIR "${candidate}")
    break()
  endif()
endforeach()

if(NOT DEFINED ETL_DIR)
  message(FATAL_ERROR
    "ETL not found in HCoreLib/3rdParty/etl or 3rdParty/etl. It is a "
    "submodule - run: git submodule update --init --recursive")
endif()

# PUBLIC, not private: HValue.hpp puts etl::string in its interface, so anything
# including an HCoreLib header needs these too.
list(APPEND HCORELIB_INCLUDE_DIRS "${ETL_DIR}/include")

# HCoreLib.h includes <HCoreLibConfig.h>, which belongs to the application
# rather than to the library - see App/Config/HCoreLibConfig.h. Its directory
# arrives in HCORELIB_CONFIG_DIR and is exported as a public include dir,
# because that header is reachable from any HCoreLib header a consumer
# includes.
#
# IDF runs this file twice: once in cmake script mode, purely to harvest
# REQUIRES, and once for real. The project's cache does not exist in the script
# pass, so the variable is neither visible nor needed there.
if(NOT CMAKE_SCRIPT_MODE_FILE)
  if(NOT DEFINED HCORELIB_CONFIG_DIR)
    message(FATAL_ERROR
      "HCORELIB_CONFIG_DIR is not set. Point it at the directory holding the "
      "application's HCoreLibConfig.h before adding HCoreLib to the build.")
  endif()

  if(NOT EXISTS "${HCORELIB_CONFIG_DIR}/HCoreLibConfig.h")
    message(FATAL_ERROR
      "No HCoreLibConfig.h in HCORELIB_CONFIG_DIR (${HCORELIB_CONFIG_DIR}).")
  endif()

  # The board's pin table lives beside it. HGpioManager is a generic driver and
  # cannot invent one, so its absence is a missing board definition rather than
  # a missing option.
  if(NOT EXISTS "${HCORELIB_CONFIG_DIR}/HGpioConfig.h")
    message(FATAL_ERROR
      "No HGpioConfig.h in HCORELIB_CONFIG_DIR (${HCORELIB_CONFIG_DIR}). "
      "HGpioManager needs the board's pin table - declare it there, empty lists included.")
  endif()

  list(APPEND HCORELIB_INCLUDE_DIRS "${HCORELIB_CONFIG_DIR}")
endif()

if(ESP_PLATFORM)
  idf_component_register(SRCS ${HCORELIB_SRCS}
                         INCLUDE_DIRS ${HCORELIB_INCLUDE_DIRS}
                         # FreeRTOS is public because HTask.hpp exposes a task
                         # handle and etl::mutex reaches the kernel headers.
                         REQUIRES freertos
                         # Private: esp_timer for the millisecond clock,
                         # esp_system only for esp_restart(), esp_driver_gpio
                         # for the pads.
                         PRIV_REQUIRES esp_timer esp_system esp_driver_gpio)

  target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

  # etl/mutex/mutex_freertos.h includes "FreeRTOS.h" and <semphr.h> without the
  # "freertos/" prefix that IDF exposes publicly. The kernel does ship a
  # directory serving those bare names - add it so etl::mutex compiles, here and
  # in anything that includes an HCoreLib header.
  if(CONFIG_FREERTOS_SMP)
    set(freertos_kernel_impl "FreeRTOS-Kernel-SMP")
  else()
    set(freertos_kernel_impl "FreeRTOS-Kernel")
  endif()

  idf_component_get_property(freertos_dir freertos COMPONENT_DIR)
  target_include_directories(${COMPONENT_LIB} PUBLIC
                             "${freertos_dir}/${freertos_kernel_impl}/include/freertos")
else()
  cmake_minimum_required(VERSION 3.16)

  add_library(HCoreLib STATIC ${HCORELIB_SRCS})
  add_library(HCoreLib::HCoreLib ALIAS HCoreLib)

  target_include_directories(HCoreLib PUBLIC ${HCORELIB_INCLUDE_DIRS})
  target_compile_features(HCoreLib PUBLIC cxx_std_17)

  # Nothing to link: ETL is headers, and its directories are already in
  # HCORELIB_INCLUDE_DIRS above. On the host, etl::mutex falls back to std::mutex,
  # so there is no kernel include path to add either.
endif()

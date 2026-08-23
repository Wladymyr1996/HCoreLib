#######################################################################
# Portal - the settings mode every Hatynka device shares.
#
# A component of its own, like Devices/, and for the same reason: it is the only
# part of HCoreLib that needs a radio and an HTTP server. A device with no
# configuration portal - a sensor node that is only ever talked to over HBus -
# neither builds nor links any of this, and does not inherit the mDNS dependency
# either.
#
# What is HERE is what every device does identically: bring up a network, serve
# a page, answer the common REST routes, hold a session key. What is NOT here is
# anything about a particular device - its own page, its own settings, its own
# routes. Those are registered by the application.
#######################################################################

if(NOT ESP_PLATFORM)
  # Wi-Fi, lwIP and esp_http_server have no host equivalents. The logic that can
  # be tested off-target lives in HCoreLib proper.
  return()
endif()

idf_component_register(SRCS "HNetwork/HNetwork.cpp"
                            "HWebServer/HWebServer.cpp"
                            "HCaptiveDns/HCaptiveDns.cpp"
                            "HStaticUi/HStaticUi.cpp"
                            "HFactoryReset/HFactoryReset.cpp"
                            "HOtaWriter/HOtaWriter.cpp"
                            "HOtaPull/HOtaPull.cpp"
                            "HRestApi/HRestApi.cpp"
                            "HRestApi/HRestRequest/HRestRequest.cpp"
                            "HRestApi/HRestResponse/HRestResponse.cpp"
                       INCLUDE_DIRS "."
                       # HCoreLib for HLog, HTask and HCoreLib.h; the rest is
                       # the radio and the server themselves.
                       REQUIRES HCoreLib
                                esp_wifi esp_netif esp_event nvs_flash esp_http_server lwip
                                esp_app_format app_update esp_partition
                                esp_http_client)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# The favicon
#
# The one asset the library ships rather than the application. Every device in
# the ecosystem is the same product to a browser - the tab it opens says
# "Hatynka", not "this particular thermometer" - so the bytes live here and
# HStaticUi serves them from a route no application registers or can replace.
#
# A PNG, embedded as it is: it is under a kilobyte and already compressed, so
# gzipping it would cost a step and save nothing.
#
# RENAME_TO fixes the linker symbol, which otherwise comes from the file name
# alone - an application that embeds a favicon.png of its own would collide with
# this one at link time.
# ---------------------------------------------------------------------------
target_add_binary_data(${COMPONENT_LIB} "HStaticUi/favicon.png" BINARY
                       RENAME_TO hcore_favicon_png)

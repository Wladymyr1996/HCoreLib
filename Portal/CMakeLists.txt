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
                            "HRestApi/HRestApi.cpp"
                            "HRestApi/HRestRequest/HRestRequest.cpp"
                            "HRestApi/HRestResponse/HRestResponse.cpp"
                       INCLUDE_DIRS "."
                       # HCoreLib for HLog, HTask and HCoreLib.h; the rest is
                       # the radio and the server themselves.
                       REQUIRES HCoreLib
                                esp_wifi esp_netif esp_event nvs_flash esp_http_server lwip
                                esp_app_format)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

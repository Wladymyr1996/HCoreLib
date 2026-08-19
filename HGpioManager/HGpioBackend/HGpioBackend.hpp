#pragma once

class HIGpio;

/**
 * @brief The one GPIO backend this build compiled in.
 *
 * A free function rather than a member of HGpioManager, and that is the whole
 * point of the file. HGpioPin has to reach the pads to do anything at all, and
 * asking the MANAGER for them made the pin depend on the class that hands pins
 * out - a cycle for no reason, since a pin needs the pads and not the table.
 * Here the dependency runs one way: both the manager and the pin ask this, and
 * this asks nobody.
 *
 * Which backend that is - HGpioEsp32 or HGpioDesktop - is decided in the .cpp
 * and nowhere else, so a new platform is one branch in one file.
 *
 * Not application API: an application says HGpioManager::find("btn") and gets a
 * handle that already knows how to reach the pad.
 */
HIGpio& hGpioBackend() noexcept;

#pragma once

class HIAdc;

/**
 * @brief The one ADC backend this build compiled in.
 *
 * A free function rather than a member of HAdcManager, for exactly the reason
 * hGpioBackend() is one: HAdcChannel has to reach the converter to do anything
 * at all, and asking the MANAGER for it would make the channel depend on the
 * class that hands channels out - a cycle for no reason, since a channel needs
 * the converter and not the table.
 *
 * Which backend that is - HAdcEsp32 or HAdcDesktop - is decided in the .cpp and
 * nowhere else.
 *
 * Not application API: an application says HAdcManager::find("vbat") and gets a
 * handle that already knows how to reach the pad.
 */
HIAdc& hAdcBackend() noexcept;

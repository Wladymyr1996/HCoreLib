# HSleep

Deep sleep, and the reason the last one ended.

```cpp
switch (HSleep::wakeCause()) {
  case HWakeCause::Timer:  /* nobody is watching: measure and go back */ break;
  case HWakeCause::Button: /* somebody is: light the screen up */       break;
  default:                 /* switched on */                            break;
}

HSleep::enableTimerWakeup(60000);
HSleep::enableButtonWakeup(buttonPin);
HSleep::deepSleep();                  // never returns
```

## Deep sleep is a reset

`app_main()` runs again from the top on every wake. Statics are re-initialised,
tasks are gone, peripherals are reset — **nothing in RAM survives**. Only RTC
memory does, which is why anything the next run needs (measurements, the screen
the user was on) lives in an `RTC_DATA_ATTR` block behind a magic word. See
`App/HDeviceState/` for the pattern and [HBootMode](../HBootMode/) for the
variant that must survive a *reset* but not a *power cut*.

## Waking on a pin

`enableButtonWakeup()` takes an `HGpioPin` and works out the electrical level
itself: the pin's logical `true` is what wakes the device, so a button wired to
ground behind a pull-up wakes on the **press** and this module needs to know
nothing about the wiring to get that right.

Two hardware details it handles, both of which cost a flat battery if missed:

- Only pads in the chip's low-power domain can wake it — GPIO0–7 on the
  ESP32-C6. Anything else is rejected and logged rather than silently ignored.
- The pull resistor is re-established through the **RTC** IO mux before
  sleeping, because the digital pad control is powered down during the nap. A
  wake pin left floating wakes the device continuously.

## On the host

`wakeCause()` is always `ColdBoot` and `deepSleep()` exits the process — the
closest honest equivalent to "start over as a fresh boot".

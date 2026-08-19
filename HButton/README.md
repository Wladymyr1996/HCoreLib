# HButton

Debounces one pin and turns it into three events.

```cpp
static HButton button(HGpioManager::find("btn"));
button.onPressed(HButtonCallback::create<&onPressed>());
button.onLongPressed(HButtonCallback::create<&onHold>());
button.onReleased(HButtonReleasedCallback::create<&onReleased>());

// from the tick task, every HCORELIB_TICK_MS:
button.update();
```

| Callback | When |
| --- | --- |
| `onPressed()` | the moment a press is believed |
| `onLongPressed()` | **once**, `HBUTTON_LONG_PRESS_MS` into a press that is still held |
| `onReleased(heldMs)` | the moment a release is believed, with the press duration |

`onReleased` fires for every press, long ones included, and carries the duration
so the application decides what a "click" is instead of a second policy living
here. `onLongPressed` fires while the button is still down — which is what makes
"hold three seconds to reset" feel right: the device acts, and the user lets go
afterwards.

Callbacks are `etl::delegate` (no heap) and run inside `update()`, in the
caller's task. Keep them short; do not block in one.

## The debounce rule

A raw level that disagrees with the settled one starts a timer; agreement cancels
it. Only a disagreement surviving `HBUTTON_DEBOUNCE_MS` becomes the new settled
level and fires an event. Contact bounce is precisely a disagreement that keeps
collapsing, so it produces **no** events rather than a burst of them.

## Why `update()` takes no `dt`

Durations are measured by [HTimer](../Utils/HTimer/) against an absolute clock,
never accumulated from the caller's tick period. A task that runs late, or misses
a tick entirely, still reads the correct elapsed time — so the tick rate sets how
quickly an edge is **noticed** and never how accurately a hold is **measured**. A
press held 3000 ms is reported as 3000 ms even if the driving task stalled in the
middle of it.

## The state at construction is adopted, not announced

An event means a **change**. A button already down when the object is built did
not change — it was found that way — so no press fires for it, and the hold is
timed from construction. That is what a device woken from deep sleep *by* the
button needs: the press that did the waking has already been spent, and firing
for it would act on the user's input twice. The long press and the release that
follow still arrive normally, and `isPressed()` answers if the boot-time state
matters.

## Wiring is not this module's business

The `HGpioPin` it takes is already configured and already speaks logical levels:
`read() == true` means pressed, whatever the pull-up and the switch are doing.
See [HGpioManager](../HGpioManager/).

## Tested without hardware

`HGpioDesktop` lets a host build move the pad, so the state machine is exercised
by a synthetic bounce train: bounce alone fires nothing, a settled press fires
once, the long press does not repeat, a short press produces no long press, and
`heldMs` matches the gesture.

# xcb-test
xcb example collection

## `frame_sync`

`frame_sync` creates an X11 window that advertises the EWMH extended frame
synchronization protocol. It creates the basic and extended XSync counters,
sets both in `_NET_WM_SYNC_REQUEST_COUNTER`, marks each animated frame with
odd/even extended-counter values, and validates the `_NET_WM_FRAME_DRAWN` and
`_NET_WM_FRAME_TIMINGS` client messages sent by the compositor.

Build and run it under the Xwayland display managed by the wlroots compositor:

```sh
meson setup build
meson compile -C build frame_sync
./build/frame_sync/frame_sync --frames 120 --wait-drawn
```

Use `--resize` to additionally generate periodic resize requests. A successful
run ends with `RESULT: PASS`; the serial, high-precision timestamp, presentation
offset, refresh interval, and frame delay are printed for every received frame.

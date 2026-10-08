# WLED packet buffer regression checks

Tests compile the actual v22 sender method with the real ColorRgb definition and a synchronous UDP recording model. The complete DriverNetWled class is separately compiled by the native webOS build. ProviderUdp::writeBytes copies the packet through Qt writeDatagram before returning.

Run `python3 tests/wled-packets/run.py --baseline`, then without `--baseline`, and with `--sanitize`. The baseline comes from the official v22.0.0.0 tag. Cases cover colored/black frames, protocol boundaries, indexed offsets through 65535 pixels, count-change behavior, write errors and 166-pixel/500-byte output. Warmed steady 166-pixel writes allocate 1000 packet buffers per 1000 frames originally and zero after this change. This allocator model is not a TV CPU benchmark or network-delivery/ESP-output measurement.

The wire behavior is preserved: DRGB header 02 ff through 490 pixels, then indexed DNRGB chunks of 489 pixels. The original larger-strip error handling and count-change first-frame behavior are retained.

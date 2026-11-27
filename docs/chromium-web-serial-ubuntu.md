# Fix Chromium Web Serial USB Device Detection on Ubuntu

When using a web-based ESP32 flashing tool, such as [Luxio Studio](https://studio.luxio.lighting/), Chromium may report that no serial devices are available even though Ubuntu recognises the connected ESP32. This can be caused by Snap confinement restricting Chromium's access to USB devices.

## Check whether Chromium is installed as a Snap

Run:

```sh
snap list chromium
```

If Chromium is listed, continue with the following steps.

## Grant Chromium USB access

Run:

```sh
sudo snap connect chromium:raw-usb
```

This grants Chromium access to USB devices through the Snap `raw-usb` interface.

## Restart Chromium

Completely close Chromium and reopen it.

## Test the serial connection

1. Connect your ESP32 via USB.
2. Open [Luxio Studio](https://studio.luxio.lighting/).
3. Navigate to the ESP32 flashing interface.
4. Select the serial device when prompted.

Chromium should now detect the ESP32's USB-to-serial interface.

## Additional troubleshooting

If the problem persists, verify that Ubuntu detects the serial device:

```sh
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

Check Chromium's Snap interface connections:

```sh
snap connections chromium
```

Check that the `raw-usb` interface is connected.

**Note:** This solution applies specifically to Chromium installed through Snap on Ubuntu. Other Chromium distributions may require different permissions or configuration.

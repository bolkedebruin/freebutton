# FreeButton

Open firmware for the Button+ smart button (ESP32-S3, 16 MB flash), with a
web IDE that runs on the device.

FreeButton is an independent project. It is not made or supported by the
maker of the hardware, and it does not read the configuration format of the
original firmware.

Version: see [`VERSION`](VERSION). The firmware is in alpha: formats and the
script API can still change.

## What it does

- **Displays and keys**: a main display with pages, lists and images, and
  button bars with a display and an LED per key. The layout is in one file,
  `config.json` ([names](docs/NAMES.md), [styles and fonts](docs/STYLE.md),
  [lists](docs/LISTS.md)).
- **Scripts**: a JavaScript engine ([mquickjs](lib/mquickjs)) runs
  `scripts/main.js` on the device. A script reads keys and sensors, draws on
  the displays, sets LEDs and talks MQTT. Examples are in
  [`data/scripts/examples`](data/scripts/examples).
- **MQTT and Home Assistant**: several brokers, and Home Assistant discovery
  for keys, LEDs and sensors.
- **Web IDE**: served by the device. It has an editor with completion and
  live checks, a file manager for the device, and a virtual device that runs
  the firmware in the browser ([emulator](docs/EMULATOR.md)).
- **HTTP API**: an S3-style file API and device endpoints, signed with AWS
  Signature Version 4 because the device has no room for TLS
  ([authentication](docs/AUTH.md)).

```js
fb.main.left.button.onClick(function () {
    fb.bars[0].left.led.setColor(0, 255, 0);
});
```

## Build

You need [PlatformIO](https://platformio.org/) and Node.js 20.

```bash
git clone --recurse-submodules https://github.com/bolkedebruin/freebutton
cd freebutton

# Web IDE (the filesystem image contains it)
cd web && npm ci && npm run build && cd ..

# Firmware and filesystem image
pio run -e esp32
pio run -e esp32 -t buildfs
```

Tests:

```bash
cd web && npm test      # web IDE and the virtual device
pio test -e native      # firmware logic on the host
```

## Install on a device

The first installation is over USB, because the partition table differs from
the one of the original firmware. **This erases the device.** Read
[Moving a device to partition table v2](docs/MIGRATION-PARTITIONS-V2.md)
first; it has the steps, the offsets and how to keep a backup.

```bash
pio run -e esp32 -t upload      # bootloader, partition table, firmware
pio run -e esp32 -t uploadfs    # web IDE, default config.json, examples
python3 tools/scripts/flash_media.py --port <serial port>   # fonts
```

After that the device starts a WiFi setup portal. When it is on your network,
open `http://<device address>/` for the IDE. Later updates of the
filesystem image can go over the network (`tools/scripts/ota_fs.py`).

`data/config.json` is the configuration a new device starts with. Change the
broker address in it, or in the IDE after the first start.

## Your own device files

The `config.json`, scripts and images of a particular device describe one
installation, so they are not in this repository. Keep them in a repository
of your own, checked out as `devices/` (ignored by git here). See
[Device files](docs/DEVICES.md).

## Documentation

| Topic | File |
| --- | --- |
| Names in `config.json` | [docs/NAMES.md](docs/NAMES.md) |
| Styles and fonts | [docs/STYLE.md](docs/STYLE.md) |
| Lists on the main display | [docs/LISTS.md](docs/LISTS.md) |
| Virtual device and emulator | [docs/EMULATOR.md](docs/EMULATOR.md) |
| HTTP API authentication | [docs/AUTH.md](docs/AUTH.md) |
| Script store and restart | [docs/SCRIPT-STORE.md](docs/SCRIPT-STORE.md), [docs/SCRIPT-RESTART.md](docs/SCRIPT-RESTART.md) |
| Media store (fonts, images) | [docs/MEDIA-STORE.md](docs/MEDIA-STORE.md) |
| Partition table and migration | [docs/MIGRATION-PARTITIONS-V2.md](docs/MIGRATION-PARTITIONS-V2.md) |
| Build tools | [tools/README.md](tools/README.md) |

## License

GPL-3.0, see [`LICENSE`](LICENSE). The bundled fonts have their own licenses
in [`media/fonts`](media/fonts): Montserrat (SIL Open Font License) and
Material Design Icons (Apache 2.0).

# Device files

The files of a particular device are not part of this repository: its
`config.json`, the source of its `scripts/main.js`, the images and fonts
uploaded to it, notes on its Home Assistant side, and the backups the tools
make. They describe one installation (its network, its entities), so they
live in a repository of their own.

The tools and docs here refer to them as `devices/<device id>/...`:

| Path | Written or read by |
| --- | --- |
| `devices/<id>/config.json` | `tools/scripts/ota_fs.py --config`, `FREEBUTTON_CONFIG=... pio run -t uploadfs` |
| `devices/<id>/backup/fs-<timestamp>/` | `tools/scripts/ota_fs.py` (backup before a filesystem update), `media_sync.py pull` |

`/devices/` is in `.gitignore`. Check your own device repository out there,
or let the tools create the directory:

    git clone <your device repository> devices

The factory configuration that ships in the filesystem image is
`data/config.json`.

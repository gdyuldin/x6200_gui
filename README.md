# X6200 LVGL GUI

This is R1CBU alternative firmware, ported to X6200

## Installing

Open [Releases](https://github.com/gdyuldin/x6200_gui/releases/latest) page and download `sdcard.img` file (in Assets section). With balenaEtcher or Rufus
burn img file to microSD card. Insert card to the transceiver and boot it.

## Importing ADI log

Application could mark already worked callsign in the UI.
To load information about previous QSOs - copy your ADI log to the `DATA` partition and rename it to `incoming_log.adi`.
Application will import records to own log on the next boot and will rename `incoming_log.adi` to `incoming_log.adi.bak`.

*Note*: `DATA` partition will be created after first launch transceiver with inserted SD card.


## Exporting ADI log

Application stores FT8/FT4 QSOs to the `ft_log.adi` file on the `DATA` partition of SD card. This file might be used to load QSOs to online log.


## Building


* Clone repositories

```
mkdir x6200
cd x6200
git clone https://github.com/gdyuldin/AetherX6200Buildroot
git clone https://github.com/gdyuldin/x6200_gui
```

* Build buildroot

```
cd AetherX6200Buildroot
./br_build.sh
cd ..
```

* Build app

```
cd x6200_gui
git submodule init
git submodule update
cd buildroot
./build.sh
```

# The X6200's base firmware, fetched and written from the radio's Linux

The X6200 runs two firmwares: this GUI's Linux on the card, and Xiegu's **base** firmware in the radio board's own
processor (DSP, RF, power, keyer). Xiegu's way to update the base needs Xiegu's own system on the radio's internal
memory: a spare card with Xiegu's 370 MB update image, a boot to write it, a menu, the card swapped back. These tools
do it while the radio runs this GUI's image, without taking its card out: from a computer over SSH. Any release Xiegu
has published, up or down.

| Piece | Where | Does |
|---|---|---|
| **x6200-base-flash** | [base_flash.c](base_flash.c), on the radio (in the image, and copied there by flash.sh) | writes the base the way Xiegu's own app 1.0.8 does, step for step |
| **base_fetch.py** | here, on a computer (the radio's image has no Python) | fetches a release from xiegu.eu as a stream, keeping only the base file |
| **base_releases.txt** | [here](base_releases.txt) | the releases known: version, file, size, SHA-256, Xiegu's zip and its address |
| fetch.sh, build.sh, flash.sh, install.sh | here | fetch, build and write from a computer |

## The releases

All of Xiegu's X6200 base files, as [base_releases.txt](base_releases.txt) has them.
Files are told apart by their SHA-256, never by name or date (the date is the one in Xiegu's own file name):

| Base | File | Bytes | Where | |
|---|---|---|---|---|
| **1.0.8** | x6200_mcu_20260820141208.xgf | 524288 | xiegu.eu, `x6200udimg-260820001.zip` | fixes the S-meter stuck at -255 dBm (S1, -121.0 on screen), the CW keyer's speed; CW macros back |
| 1.0.7 | x6200_mcu_20260116092100.xgf | 401024 | not on xiegu.eu: the factory build, on the test X6200's internal memory | a later 1.0.7 build than xiegu.eu's: what the test X6200 came with (below) |
| 1.0.7 | x6200_mcu_20250916150221.xgf | 524288 | xiegu.eu, `x6200 FW 1.0.7 (251012).zip` | |
| 1.0.6 | x6200_mcu_20250111134652.xgf | 524288 | xiegu.eu, `Xiegu X6200 update V1.0.6 (240115).zip` | |
| 1.0.3 | x6200_mcu_20240924203603.xgf | 524288 | xiegu.eu, `X6200_UD_Image_1.0.3.zip` | |

**Two 1.0.7 builds.** Xiegu published 1.0.7 once, in October 2025, with a base file built on 16 September 2025. Our
X6200 left the factory in January 2026 with a later build of the same version, which Xiegu never put on its site:

- **It came from the radio, not from a backup.** A base's firmware cannot be read back (its bootloader only takes a
  file in, and the processor is read-protected), so nothing was backed up before the first write. The file is the one
  Xiegu's own system keeps for its UPGRADE screen, `/usr/firmware/x6200_mcu_20260116092100.xgf` on the radio's
  internal memory (`/dev/mmcblk2p2`).
- **Xiegu built that system on 16 January 2026.** The base file is dated 09:18 China time that morning (its name
  says 09:21), the system's last files 10:02; the only newer file there is the 1.0.8 we copied in on 13 November 2026
  ([install.sh](install.sh)). No update on xiegu.eu carries that system, so it came with the radio.
- **The base ran that build.** Before any write, on 7 November 2026, the base reported `fw version: 1.0.7`, `fw date:
  Jan 15 2026,16:35:01`: not xiegu.eu's September 2025 build. Written back in the downgrade below, the base reports the
  same date again. Without a readout, that is as close a match as can be had: same version, same build date.
- **What differs between the two is not known.** The files are encrypted and Xiegu wrote no release note for the later
  one. It is 401,024 bytes; every base file on xiegu.eu is padded to 524,288.

Another X6200 may carry another factory build in `/usr/firmware` on its internal memory: copied off, `flash.sh --file`
writes it, named by its SHA-256, or as unknown (`--unknown-ok`) when the list does not have it.

Xiegu's files are Xiegu's: they are fetched from Xiegu's own site and nothing of them is in this repository.

## From a computer

```bash
tools/base_update/fetch.sh
```

```bash
tools/base_update/build.sh
```

```bash
tools/base_update/flash.sh root@<radio> --check
```

```bash
tools/base_update/flash.sh root@<radio> --flash
```

1. **fetch.sh** fetches into `build/base_update`: `fetch.sh` the latest, `fetch.sh 1.0.6` one release,
   `fetch.sh all` every one on xiegu.eu, `fetch.sh --discover` whatever xiegu.eu's downloads page has that the list
   does not, `fetch.sh --url URL` or `--zip FILE` an update zip by its address or one here. No zip is kept. It needs
   Python 3 and nothing else (no 7-Zip).
2. **build.sh** builds the writer for the radio, statically linked, with any ARM hard-float Linux cross compiler
   (`arm-linux-gnueabihf-gcc`, or another prefix in `CROSS_COMPILE`). The image builds it as `x6200-base-flash`.
3. **flash.sh --check** reads only, and never asks the base for its bootloader: the file, what holds the base's
   serial port and I2C bus (the GUI, while it runs), and the base's version over I2C.
4. **flash.sh --flash** says what it is about to do and asks for `FLASH` to be typed (`--yes` does not ask). On the
   radio, apart from the SSH session ([run_on_radio.sh](run_on_radio.sh), under `setsid`): the GUI stopped
   (`/etc/init.d/S95gui stop`), the write, the GUI started again; flash.sh follows the log. `--to VERSION` writes
   another release, its file found in `build/base_update` by its SHA-256 (the newest build when a version has two);
   `--file FILE.xgf` a file of your own; `--recover` again after a write that failed after the erase.
5. The base then switches the radio off about three minutes later: switch it on again; `--check` shows the new
   version.

**install.sh** is the other way, with Xiegu's own system: it puts the 1.0.8 file in `/usr/firmware` on the internal
memory, where Xiegu's MENU > SYSTEM > UPGRADE lists it (switch off, card out, switch on, UPGRADE, card back in).

**Power.** As Xiegu asks for its own update: a 13.8 V supply of 3 A or more and the battery out, for the whole write.
Do not switch the radio off while it writes. Every write here so far was on the radio's 11.8 V external supply with its
battery in and charging: both sources there, and each went through, but that is not Xiegu's condition.

**If it fails.** The writer says where, in Xiegu's own words, and its exit status says what to do:

| Exit | Meaning | Then |
|---|---|---|
| 0 | written | switch the radio on again after it switches itself off |
| 1 | refused: nothing was sent to the base (not a file it writes, something else holds the port, no base on I2C) | what it says |
| 2 | the bootloader did not answer its handshake: nothing erased | switch the radio off and on |
| 3 | failed at or after the erase: the base has no firmware now | `flash.sh --recover` (the GUI is left off); do not switch off |

The bootloader is in the processor's first sectors, which the erase does not touch, so it should take the file
again. That is how Xiegu's design reads; no write here has needed it yet.

## Your own files, and a new release

- **A file of your own**: `flash.sh --file NAME.xgf`. One the list does not know by its SHA-256 is written only with
  `--unknown-ok` and one more typed confirmation. The writer takes it only if it passes what can be checked of an
  encrypted file: a multiple of 16 bytes, 16 to 512 KiB, and not a plain ARM image (one starting with a stack pointer
  and a reset vector would be decrypted into garbage by the bootloader).
- **A new release from Xiegu**: `fetch.sh --discover` finds it on xiegu.eu's downloads page, fetches it, keeps it as unverified (`NAME.xgf.json`, with the version from Xiegu's
  release note) and prints its line for `base_releases.txt`. Added to the list and to `known[]` in
  [base_flash.c](base_flash.c) (the test suite checks the two agree), it is a known release like the others.

## Only the X6200's base, only the X6200's files

Xiegu encrypts the X6100's, the G90's and the X6200's base files with the same key, so nothing in an encrypted file
says which radio it is for, and the X6100's base is written another way altogether (its Morse key line held low while
the base is reset). Every step keeps the radios apart:

- **The writer runs on an X6200 only.** Before it opens the base's port or bus, x6200-base-flash reads the device tree
  (`/proc/device-tree/compatible`) and refuses unless it says `xiegu,x6200`: on an X6100 not one byte reaches the base
  (tested: the `x6100` scenario below), should it ever be copied to one.
- **Only the X6200's files are written.** A file is known by its SHA-256 against the X6200 files in
  [base_releases.txt](base_releases.txt), never by its name. Any other file needs
  `--unknown-ok` and a second confirmation, and a file named for another radio (X6100, G90, G106, X5105) is refused
  even then.
- **Only the X6200's releases are fetched.** The fetcher reads only xiegu.eu's links titled "X6200 Firmware Upgrade",
  and takes only `./usr/firmware/*.xgf` from those zips.

## How it works

Xiegu's app 1.0.8 (`/usr/app_qt/x6200_ui_v100` in its update) was disassembled to see what its UPGRADE screen does:
`UiFwUpgrade::upgradeFirmware` reads the file whole and hands it to a worker thread, `XMWorker::slot_start`, with the
base's serial port, which its main loop stops reading meanwhile (`BaseComm::pause`). The writer does the same, with
the same timeouts, the same messages and the same error handling:

1. **The port** (`uart_create`, `BaseComm::start`): `/dev/ttyS1`, raw 8N1 at 1152000 baud, `VMIN` and `VTIME` 0 so
   every read returns at once, opened blocking without a controlling terminal, both queues flushed.
2. **The bootloader asked for**: a `c` on the port and host command `0x800a` over I2C, both again every 100 turns of
   a loop that sleeps 1 ms and reads up to 10240 bytes each turn. The host command is one write to the base at
   address `0x72` on `/dev/i2c-0`: register `0xfffe` high byte first, the command low byte first, `FF FE 0A 80`
   (`BaseCommIIC::iicWrite`; the same bytes as `x6200_control_host_cmd` in Aether's library). A one-byte read of `Y`
   or `y` counts; more than 10 end the loop. So do 10000 empty reads (`[ERR] Wait IAP respond timeout`) or 10000
   others (`[ERR] Clear rx buffer timeout`), and Xiegu's app goes on all the same: only the next step decides. The
   X6200 uses no GPIO for this (the X6100's updater pulls the Morse key line low and resets the base).
3. **The handshakes** (`send_cmd`): `c` for `y` (`[ERR] IAP failed, connect timeout`: nothing erased yet), then `r`,
   the erase, for `g` (`[ERR] IAP failed, can't erase flash`), then `p`, program, for `g` (`[ERR] IAP failed, can't
   start xmodem`). Each byte written, then up to 5000 one-byte reads 1 ms apart for the answer, written again up to 10
   more times (`[ERR] no respond to cmd %c, retry %d`).
4. **The file** (`Xmodem::sendFile`), as it is: XMODEM-1K with a CRC. The bootloader decrypts the `.xgf` itself, so
   no key is needed, here or anywhere. A 2-byte read whose second byte is `C` within 10 s, then 1024-byte packets
   (STX, the packet number from 1, its complement, the data with the last block padded with `0xFF`, CRC-16/XMODEM high
   byte first), each answered within 10 s by one byte: ACK the next, CAN, NAK or anything else the end; no retries.
   Then EOT, answered by ACK (`1k xmodem send finished.`).
5. **The end**: Xiegu's app ends every way the same and neither resets the base nor sends it a command.

Here the GUI is stopped for the write instead (run_on_radio.sh), as Xiegu's app stops its own reading of the port;
the writer refuses while any other process holds `/dev/ttyS1` or `/dev/i2c-0`.

## How it was checked

- **[test/run.sh](test/run.sh)**, on a Mac or a Linux PC, in about two minutes, and on GitHub for every change to
  the updater ([base_update.yml](../../.github/workflows/base_update.yml), with a stand-in for Xiegu's files):
  - unit tests: SHA-256 against FIPS 180-2's vectors; the packet's CRC against CRC-16/XMODEM's check value and against
    Xiegu's own CRC routine (rewritten from its disassembly) on 2000 random packets; the packet's frame; the writer's
    `known[]` against `base_releases.txt`; Xiegu's files taken, any other refused;
  - the writer itself, its port on a pseudo-terminal, against a simulated base in each of these:

    | Scenario | The simulated base | The writer |
    |---|---|---|
    | ok | streams, then answers as the bootloader | the file whole, byte for byte; exit 0 (also with 1.0.7's 392 packets) |
    | silent | says nothing until the writer has given up waiting | `[ERR] Wait IAP respond timeout`, goes on as Xiegu's app does; exit 0 |
    | stream | keeps streaming until the writer has given up | `[ERR] Clear rx buffer timeout`, goes on; exit 0 |
    | dead | never answers | `[ERR] IAP failed, connect timeout`; exit 2 |
    | noerase | never answers the erase | `[ERR] IAP failed, can't erase flash`; exit 3 |
    | nostart | never sends the `C` | `1k xmodem timeout.`; exit 3 |
    | nak, can, garbage | NAK at packet 100, CAN at 50, an `X` at 10 | `1k xmodem bad packet`, `… transfer canceled.`, `… error, = 88`; exit 3 |
    | eot | no ACK for the EOT | `1k xmodem error, = 0 (after EOT)`; exit 3 |
    | x6100 | an X6100's device tree | `refused: this radio is not an X6200`, not one byte sent; exit 1 |

  - the fetcher on small update zips made as Xiegu's are ([test/synth.py](test/synth.py): a zip written as a stream,
    an MBR, a FAT16 boot partition, a Linux one, a FAT16 one with rootfs.tar), through Python's zlib and through libz
    (for a Python without its zlib module): Xiegu's 1.0.8 file out and checked, an unknown one out whole,
    unverified, with its line for the list.
- **Fetching**: 1.0.8 from xiegu.eu, the file out and checked after 255 of the zip's 371 MB; from a zip already
  here, 0.9 s.

### Three real writes, 13 November 2026

The same writer, on an X6200 running OpenX6X00 (where these tools started), on its 11.8 V external supply with its
battery in, charging (89 to 90 %). This GUI's run_on_radio.sh differs only in the GUI it stops and starts.

| | 1.0.7 -> 1.0.8 (flash.sh) | 1.0.8 -> 1.0.7 (flash.sh, the downgrade) | 1.0.7 -> 1.0.8 (OpenX6X00's screen) |
|---|---|---|---|
| file | xiegu.eu's 1.0.8 | **the factory 1.0.7** (x6200_mcu_20260116092100.xgf, Jan 2026, 392 packets) | xiegu.eu's 1.0.8 |
| bootloader answers | 4.6 s | 4.6 s | 4.6 s |
| erase (`r` sent twice: it takes more than 5 s, as in Xiegu's app) | 6.7 s | 6.8 s | 6.8 s |
| transfer done, EOT acknowledged | 32.4 s | 27.9 s | 32.4 s |
| new firmware streaming on ttyS1 | (not watched) | yes | 10 s after the transfer |
| radio switches itself off | a few minutes after | about 4 minutes after | 3 min 10 s after its stream began |
| after the power-on | base 1.0.8, Aug 19 2026 | base 1.0.7, Jan 15 2026 | base 1.0.8, Aug 19 2026 |

- **A downgrade works**: 1.0.8's bootloader takes an older file as readily as a newer one, with the same steps and
  the same times.
- **The power-off is the base's own**: nothing on Linux sends it (no command to the base, no kernel message;
  OpenX6X00's GUI writes its log to the card when it switches the radio off itself, and there was none). The new
  firmware starts (its stream on the serial port), but its I2C to Linux stays locked (`mv64xxx: I2C bus locked`) and
  it never gets the start-up Linux sends it over I2C; about three minutes on it stops its stream and cuts the power.
  Switched on again, it starts normally.
- **The calibration stays**: the base's calibration block came through every write, its PA bias the radio's own
  (3226/3958) each time.

## Files

| File | |
|---|---|
| [base_flash.c](base_flash.c) | the writer: `--check FILE`, `--flash FILE --yes [--recover] [--held-by PID] [--unknown-ok]`, `--identify FILE` |
| [build.sh](build.sh) | the writer for the radio, static |
| [fetch.sh](fetch.sh), [base_fetch.py](base_fetch.py), [base_releases.txt](base_releases.txt) | releases from xiegu.eu into build/base_update, checked against the list |
| [flash.sh](flash.sh) | from a computer: `--check`, `--flash`, `--recover`, `--to VERSION`, `--file FILE` |
| [run_on_radio.sh](run_on_radio.sh) | on the radio, apart from the SSH session: GUI off, write, GUI on |
| [install.sh](install.sh) | the 1.0.8 file to `/usr/firmware` on the internal memory, for Xiegu's own UPGRADE screen |
| [test/](test) | run.sh, the unit tests (units.c), the simulated base (sim.py), the synthetic update zips (synth.py) |

The X6200 only: the X6100's base is written another way (its Morse key line held low while the base is reset).

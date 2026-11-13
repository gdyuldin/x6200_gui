/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  The X6200's base firmware (one of Xiegu's .xgf files) written from the radio's Linux side, as Xiegu's
 *  own app 1.0.8 writes it from MENU > SYSTEM > UPGRADE, step for step, with its timeouts, its messages and its error
 *  handling (x6200_ui_v100: UiFwUpgrade::upgradeFirmware, XMWorker::slot_start, send_cmd, Xmodem::sendFile and
 *  waitRespond; the port as uart_create and BaseComm::start set it up; the I2C write as BaseCommIIC::iicWrite):
 *
 *   0. The port: /dev/ttyS1 opened read-write without a controlling terminal (blocking, as Xiegu's), raw 8N1 at
 *      1152000 baud with VMIN and VTIME 0 (every read returns at once), both queues flushed. Xiegu's app runs
 *      "civ stop" and pauses its own reading of the port; here the GUI is stopped (tools/base_update/flash.sh).
 *   1. The base's application asked for its bootloader: a 'c' on the port and host command 0x800a over I2C
 *      (/dev/i2c-0, address 0x72, register 0xfffe high byte first, the command low byte first: FF FE 0A 80), both
 *      again every 100 turns of a loop that sleeps 1 ms and reads up to 10240 bytes each turn. A read of one byte that
 *      is 'Y' or 'y' counts; more than 10 of them end the loop. 10000 empty reads ("[ERR] Wait IAP respond timeout")
 *      or 10000 others ("[ERR] Clear rx buffer timeout") end it too, and Xiegu's app goes on all the same: only the
 *      step after decides. The results of the write and of the I2C command are not looked at.
 *   2. send_cmd 'c' for 'y' ("[ERR] IAP failed, connect timeout": nothing erased yet), then 'r' for 'g' (erase the
 *      application: "[ERR] IAP failed, can't erase flash"), then 'p' for 'g' ("[ERR] IAP failed, can't start
 *      xmodem"). send_cmd: the byte written (result not looked at), up to 5000 one-byte reads 1 ms apart for the
 *      answer, other bytes skipped; written again up to 10 more times ("[ERR] no respond to cmd %c, retry %d").
 *   3. The file as it is, by XMODEM-1K with a CRC (the bootloader decrypts it itself, so no key is needed here or
 *      anywhere): a 2-byte read whose second byte is 'C' or 'c' within 10 s ("1k xmodem timeout.", "1k xmodem error"),
 *      then 1024-byte packets (STX, number from 1, its complement, the data with the last block padded with 0xFF,
 *      CRC-16/XMODEM high byte first), each answered by a 1-byte read within 10 s: ACK the next, CAN "1k xmodem
 *      transfer canceled.", NAK "1k xmodem bad packet", anything else or nothing "1k xmodem error". No retries. Then
 *      EOT, answered by ACK ("1k xmodem send finished."). A failure here is "[ERR] xmodem send file failed".
 *   4. Every way out ends the same in Xiegu's app ("finished.", "civ start", its reading of the port resumed); it
 *      neither resets the base nor sends it a command after. Here the exit status says which step failed.
 *
 *  Only on an X6200 (its device tree says xiegu,x6200; on any other radio nothing is sent to the base), only the
 *  files listed in known[] (Xiegu's X6200 releases 1.0.3 to 1.0.8, by SHA-256) or, with --unknown-ok, one that passes
 *  the checks below and is not named for another radio, and only with nothing else holding /dev/ttyS1 or /dev/i2c-0
 *  (the GUI stopped first, tools/base_update/flash.sh, or lending the base, --held-by). From step 1 on, the hang-up,
 *  interrupt and terminate signals are ignored: a lost SSH session must not end the transfer half way.
 *
 *  The bootloader (the MCU's first sectors) is not erased by 'r': after a failed transfer the same run again should
 *  find it and write the application again, as Xiegu's app would. That is how Xiegu's design reads; it has not been
 *  tried on an X6200.
 *
 *      base_flash --check FILE     reads only: the file, who holds the base's port and bus, the base's version over
 *                                  I2C. It never asks the base for its bootloader.
 *      base_flash --flash FILE --yes
 *      base_flash --flash FILE --yes --recover
 *                                  after a transfer that failed past the erase: the base has no application to answer
 *                                  on I2C, so that check is left out; the bootloader is asked as before.
 *      base_flash --flash FILE --yes --held-by PID
 *                                  started by a GUI that keeps /dev/ttyS1 and /dev/i2c-0 open
 *                                  but has lent us the base: its descriptors are let be.
 *      base_flash --identify FILE  the file only: "xiegu-base 1.0.8 SHA256" when it is one of Xiegu's known here.
 *      ... --unknown-ok            a file not known here taken (identified as "unknown") when it passes the checks
 *                                  an .xgf can be given without its key: a multiple of 16 bytes, 16 KiB to 512 KiB,
 *                                  and not a plain ARM image. With a warning; the screen and flash.sh ask twice.
 *
 *  Exit status: 0 done, 1 refused before anything was sent to the base, 2 failed before the erase (the base's
 *  application is still there; switching the radio off and on brings it back), 3 failed after the erase (the base has
 *  no application: run the same again, and do not switch the radio off).
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef UART                        /* other paths only for a test against a simulated bootloader */
#define UART        "/dev/ttyS1"
#endif
#ifndef I2C_BUS
#define I2C_BUS     "/dev/i2c-0"
#endif
#define BASE_ADDR   0x72
#define REG_VERSION 0x0000          /* 128 bytes, the base's info block (x6200_control_get_fw_version) */
#define REG_READY   0x2000          /* not 0 once the base is up (x6200_control_init waits for it) */
#define REG_HOST    0xfffe          /* host commands (x6200_control_host_cmd) */
#define HOST_IAP    0x800a          /* Xiegu's app: the base's application to its bootloader */
#ifndef BACK_WAIT_S
#define BACK_WAIT_S 60              /* after the write, seconds the base's new application is looked for on I2C */
#endif
#ifndef DT_COMPAT                   /* the radio's model, from its device tree (a test points it at a file of its own) */
#define DT_COMPAT   "/proc/device-tree/compatible"
#endif

#define SOH_STX 0x02
#define EOT     0x04
#define ACK     0x06
#define NAK     0x15
#define CAN     0x18

enum { EXIT_DONE = 0, EXIT_REFUSED = 1, EXIT_BEFORE_ERASE = 2, EXIT_AFTER_ERASE = 3 };

/* Xiegu's base files, as their update images bring them to /usr/firmware */
static const struct {
    const char *name, *sha256;
    long        size;
    const char *ver;
} known[] = {     /* as rootfs/usr/share/x6100/base_releases.txt has them (tools/base_update/test checks) */
    {"x6200_mcu_20260820141208.xgf (base 1.0.8, Aug 2026)",
     "9a19f97a5ad9e141a3663d222f984073bf0b18b38d4662604372a0036f9daa23", 524288, "1.0.8"},
    {"x6200_mcu_20260116092100.xgf (base 1.0.7, Jan 2026: Xiegu's factory build, not on xiegu.eu)",
     "9baa46b824d9b20ddb41c6c3b64dde4a3dfbc3bdcd0a0cbc600506cb7a1e2690", 401024, "1.0.7"},
    {"x6200_mcu_20250916150221.xgf (base 1.0.7, Sep 2025: xiegu.eu's update)",
     "d58382c210738fee5261e58819aef9f3829030bf0997d53511dab3355a8fb4e2", 524288, "1.0.7"},
    {"x6200_mcu_20250111134652.xgf (base 1.0.6, Jan 2025)",
     "dca90387b70d9550448006310a69ea65625bec87f402c10df789deeb13ff12f3", 524288, "1.0.6"},
    {"x6200_mcu_20240924203603.xgf (base 1.0.3, Sep 2024)",
     "61e59a52e7c007c427d31f9de3b296e5568225efb93a1fd8df46028d48ffc3f1", 524288, "1.0.3"},
};
static const char *image_ver;   /* the loaded file's base version, known[].ver, or "unknown" (--unknown-ok) */
static char        image_sha[65];   /* and its SHA-256 */
static bool        unknown_ok;  /* --unknown-ok: a file not in known[] taken when it passes the checks below */

#define UNKNOWN_MIN 16384       /* an .xgf not known here: AES blocks (a multiple of 16), no more than the 512 KiB */
#define UNKNOWN_MAX 524288      /* Xiegu's own files fill, and not a plain ARM image (the bootloader decrypts it) */

static struct timespec t0;

static void say(const char *fmt, ...) {
    struct timespec t;
    va_list         ap;

    clock_gettime(CLOCK_MONOTONIC, &t);
    printf("[%7.3f] ", (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

static void msleep(int ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};

    while (nanosleep(&t, &t) < 0 && errno == EINTR) {
    }
}

/* ---- SHA-256 (FIPS 180-4), for telling Xiegu's files ---------------------------------------------------------- */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t *p) {
    uint32_t w[64], a, b, c, d, e, f, g, k;

    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i]        = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

static void sha256_hex(const uint8_t *data, size_t n, char out[65]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t  tail[128] = {0};
    size_t   full = n / 64 * 64, rest = n - full, tn = rest < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;

    for (size_t i = 0; i < full; i += 64)
        sha256_block(h, data + i);
    memcpy(tail, data + full, rest);
    tail[rest] = 0x80;
    for (int i = 0; i < 8; i++)
        tail[tn - 1 - i] = (uint8_t)(bits >> (8 * i));
    for (size_t i = 0; i < tn; i += 64)
        sha256_block(h, tail + i);
    for (int i = 0; i < 8; i++)
        sprintf(out + 8 * i, "%08x", h[i]);
}

/* ---- The file ------------------------------------------------------------------------------------------------- */

static uint8_t *image;
static long     image_len;

static int load(const char *path) {
    FILE       *f = fopen(path, "rb");
    char        hex[65];
    const char *name = NULL;

    if (!f) {
        say("%s: %s", path, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    image_len = ftell(f);
    rewind(f);
    if (image_len <= 0 || image_len > 1024 * 1024) {
        say("%s: %ld bytes, not a base image", path, image_len);
        fclose(f);
        return -1;
    }
    image = malloc(image_len);
    if (!image || fread(image, 1, image_len, f) != (size_t)image_len) {
        say("%s: cannot read it", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    sha256_hex(image, image_len, hex);
    memcpy(image_sha, hex, sizeof(image_sha));
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (image_len == known[i].size && !strcmp(hex, known[i].sha256))
            name = known[i].name, image_ver = known[i].ver;
    say("file %s: %ld bytes, %ld packets of 1024, sha256 %s", path, image_len, (image_len + 1023) / 1024, hex);
    if (name) {
        say("it is Xiegu's %s", name);
        return 0;
    }
    /* Another radio's base, by its name: never written to the X6200's, --unknown-ok or not (its content can't tell:
     * Xiegu encrypts the X6100's and the G90's with the same key) */
    {
        static const char *other[] = {"x6100", "6100_", "g90", "g106", "x5105", "x6100_mcu"};
        const char        *b       = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
        char               low[256];
        size_t             i;

        for (i = 0; b[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((unsigned char)b[i]);
        low[i] = '\0';
        for (i = 0; i < sizeof(other) / sizeof(other[0]); i++)
            if (strstr(low, other[i])) {
                say("refused: %s is named for another radio (%s): only the X6200's base firmware is written here", b,
                    other[i]);
                return -1;
            }
    }
    if (!unknown_ok) {
        say("not one of Xiegu's X6200 base files known here: it is not written (--unknown-ok takes one that passes "
            "the checks)");
        return -1;
    }
    /* Unknown: what can be told of an encrypted file. A plain ARM image starts with its stack pointer (in SRAM,
     * 0x2000xxxx) and its reset vector (in flash, 0x08xxxxxx): sent as it is, the bootloader would decrypt it into
     * garbage */
    uint32_t sp = image[0] | image[1] << 8 | image[2] << 16 | (uint32_t)image[3] << 24;
    uint32_t pc = image[4] | image[5] << 8 | image[6] << 16 | (uint32_t)image[7] << 24;

    if (image_len % 16 || image_len < UNKNOWN_MIN || image_len > UNKNOWN_MAX) {
        say("refused: %ld bytes is not an .xgf as Xiegu makes them (a multiple of 16, %d to %d bytes)", image_len,
            UNKNOWN_MIN, UNKNOWN_MAX);
        return -1;
    }
    if ((sp & 0xfff00000u) == 0x20000000u && (pc & 0xff000000u) == 0x08000000u) {
        say("refused: it starts as a plain ARM image (stack 0x%08x, reset 0x%08x), not an encrypted .xgf", sp, pc);
        return -1;
    }
    image_ver = "unknown";
    say("WARNING: not one of Xiegu's files known here; it passes the checks (size, AES blocks, not a plain image), "
        "but what it holds can't be told. Written only because --unknown-ok was given");
    return 0;
}

/* ---- Who else has the base's port or bus ---------------------------------------------------------------------- */

static int held_by = -1;    /* --held-by: the GUI that lent us the base (radio_base_hold) keeps its descriptors */

static int holders(const char *dev) {
    DIR           *proc = opendir("/proc");
    struct dirent *p;
    int            n = 0;

    if (!proc)
        return 0;
    while ((p = readdir(proc))) {
        char  path[600], link[300], comm[64] = "?";
        DIR  *fds;
        FILE *cf;

        if (p->d_name[0] < '0' || p->d_name[0] > '9' || atoi(p->d_name) == getpid())
            continue;
        snprintf(path, sizeof(path), "/proc/%s/fd", p->d_name);
        if (!(fds = opendir(path)))
            continue;
        snprintf(path, sizeof(path), "/proc/%s/comm", p->d_name);
        if ((cf = fopen(path, "r"))) {
            if (fgets(comm, sizeof(comm), cf))
                comm[strcspn(comm, "\n")] = 0;
            fclose(cf);
        }
        for (struct dirent *fd; (fd = readdir(fds));) {
            ssize_t l;

            snprintf(path, sizeof(path), "/proc/%s/fd/%s", p->d_name, fd->d_name);
            l = readlink(path, link, sizeof(link) - 1);
            if (l > 0 && (link[l] = 0, !strcmp(link, dev))) {
                if (atoi(p->d_name) == held_by) {
                    say("%s is open in %s (pid %s), which has lent us the base", dev, comm, p->d_name);
                } else {
                    say("%s is open in %s (pid %s)", dev, comm, p->d_name);
                    n++;
                }
                break;
            }
        }
        closedir(fds);
    }
    closedir(proc);
    return n;
}

/* ---- I2C, as Aether's library does it: the register's address high byte first --------------------------------- */

static int i2c = -1;

static bool base_read(uint16_t reg, uint8_t *buf, uint16_t len) {
    uint8_t                    a[2] = {reg >> 8, reg & 0xff};
    struct i2c_msg             m[2] = {{BASE_ADDR, 0, 2, a}, {BASE_ADDR, I2C_M_RD, len, buf}};
    struct i2c_rdwr_ioctl_data d    = {m, 2};

    return ioctl(i2c, I2C_RDWR, &d) >= 0;
}

static bool host_cmd(uint16_t cmd) {
    uint8_t                    b[4] = {REG_HOST >> 8, REG_HOST & 0xff, cmd & 0xff, cmd >> 8};
    struct i2c_msg             m    = {BASE_ADDR, 0, sizeof(b), b};
    struct i2c_rdwr_ioctl_data d    = {&m, 1};

    return ioctl(i2c, I2C_RDWR, &d) >= 0;
}

/* The base's version text, printable characters only: the string at 0x41 of its 128-byte info block, as
 * Aether's x6200_control_get_fw_version reads it; false when the base does not answer */
static bool base_version(char *out, size_t n) {
    uint8_t v[128];
    size_t  k = 0;

    memset(v, 0, sizeof(v));
    if (!base_read(REG_VERSION, v, sizeof(v)))
        return false;
    for (size_t i = 0x41; i < sizeof(v) && v[i] && k + 1 < n; i++)
        out[k++] = (v[i] >= 0x20 && v[i] < 0x7f) ? (char)v[i] : '.';
    out[k] = 0;
    return true;
}

/* ---- The serial port, as Xiegu's app has it ---------------------------------------------------------------- */

static int uart = -1;

/* uart_create's settings (cfsetispeed and cfsetospeed B1152000; iflag less IGNBRK BRKINT PARMRK ISTRIP INLCR IGNCR
 * ICRNL IXON; oflag less OPOST ONLCR; cflag CS8, less CSTOPB PARENB; lflag less ECHO ECHONL ICANON ISIG IEXTEN;
 * VTIME and VMIN 0), then BaseComm::start's open: read-write, no controlling terminal, blocking, both queues flushed.
 * CREAD is added should the port not have it: without it nothing at all is received */
static int uart_open(void) {
    struct termios t;

    uart = open(UART, O_RDWR | O_NOCTTY);
    if (uart < 0) {
        say("%s: %s", UART, strerror(errno));
        return -1;
    }
    if (tcgetattr(uart, &t) < 0) {
        say("%s: %s", UART, strerror(errno));
        return -1;
    }
    cfsetispeed(&t, B1152000);
    cfsetospeed(&t, B1152000);
    t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t.c_oflag &= ~(OPOST | ONLCR);
    t.c_cflag &= ~(CSIZE | CSTOPB | PARENB);
    t.c_cflag |= CS8;
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cc[VTIME] = 0;
    t.c_cc[VMIN]  = 0;
    if (!(t.c_cflag & CREAD)) {
        say("%s had CREAD off: on", UART);
        t.c_cflag |= CREAD;
    }
    if (tcsetattr(uart, TCSANOW, &t) < 0) {
        say("%s: error in uart parameters: %s", UART, strerror(errno));
        return -1;
    }
    tcflush(uart, TCIOFLUSH);
    return 0;
}

/* A write whose result Xiegu's app does not look at: said here when it fails, and nothing else */
static void uart_write(const uint8_t *p, size_t n) {
    while (n) {
        ssize_t w = write(uart, p, n);

        if (w > 0) {
            p += w, n -= w;
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else {
            say("%s: write: %s", UART, w < 0 ? strerror(errno) : "nothing written");
            return;
        }
    }
}

/* Xmodem::waitRespond: reads of up to 1024 bytes, 1 ms apart; one of exactly n bytes whose last is not 0 gives that
 * byte; after about s seconds, 0 */
static uint8_t wait_respond(int s, int n) {
    uint8_t buf[1024];

    if (n > 1023)
        n = 1023;
    if (n < 1)
        n = 1;
    for (int turn = 0;;) {
        ssize_t r = read(uart, buf, sizeof(buf));

        if (r == n && buf[n - 1])
            return buf[n - 1];
        turn = (turn + 1) % 1000;
        if (turn == 0) {
            if (s == 0)
                return 0;
            s--;
        }
        usleep(1000);
    }
}

/* send_cmd: cmd written, then up to 5000 one-byte reads 1 ms apart for want (other bytes passed over); written again
 * up to 10 more times */
static bool send_cmd(uint8_t cmd, uint8_t want) {
    for (int retry = 10;; retry--) {
        uart_write(&cmd, 1);
        for (int left = 5000;;) {
            uint8_t b;

            if (read(uart, &b, 1) > 0 && b == want)
                return true;
            if (--left == 0)
                break;
            msleep(1);
        }
        say("[ERR] no respond to cmd %c, retry %d", cmd, retry);
        if (retry == 0)
            return false;
    }
}

/* ---- The steps of XMWorker::slot_start -------------------------------------------------------------------------- */

/* Step 1. Ends by itself whatever comes: the step after decides */
static void to_bootloader(void) {
    static uint8_t buf[10240];
    int            empty_left = 10000, other_left = 10000, yes = 0, asks = 0, cmd_fails = 0;
    const uint8_t  c = 'c';

    for (int turn = 0;;) {
        if (turn == 0) {
            uart_write(&c, 1);
            if (!host_cmd(HOST_IAP) && cmd_fails++ == 0)
                say("host command 0x%04x on I2C: %s (as in Xiegu's app, not looked at)", HOST_IAP, strerror(errno));
            asks++;
        }
        turn = (turn + 1) % 100;
        msleep(1);

        ssize_t r = read(uart, buf, sizeof(buf));

        if (r <= 0) {
            if (--empty_left <= 0) {
                say("[ERR] Wait IAP respond timeout ('Y' %d times, %d asks)", yes, asks);
                return;
            }
        } else {
            if (r == 1 && (buf[0] & 0xdf) == 'Y')
                yes++;
            if (--other_left <= 0) {
                say("[ERR] Clear rx buffer timeout ('Y' %d times, %d asks)", yes, asks);
                return;
            }
        }
        if (yes > 10) {
            say("[OK] Get IAP respond ('Y' %d times, %d asks, %d of them unanswered on I2C)", yes, asks, cmd_fails);
            return;
        }
    }
}

static void put_packet(uint8_t seq, const uint8_t *data, uint8_t *pkt) {
    uint16_t crc = 0;

    pkt[0] = SOH_STX, pkt[1] = seq, pkt[2] = (uint8_t)~seq;
    memcpy(pkt + 3, data, 1024);
    for (int i = 0; i < 1024; i++) {                /* CRC-16/XMODEM: 0x1021, from 0 */
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = crc & 0x8000 ? (uint16_t)(crc << 1) ^ 0x1021 : (uint16_t)(crc << 1);
    }
    pkt[1027] = crc >> 8, pkt[1028] = crc & 0xff;
}

/* Step 3: Xmodem::sendFile */
static bool send_file(void) {
    uint8_t pkt[1029], block[1024];
    long    total = (image_len + 1023) / 1024, rem = image_len;
    uint8_t r;

    if (image_len <= 0 || !image)
        return false;
    r = wait_respond(10, 2);
    if (r == 0) {
        say("1k xmodem timeout.");
        return false;
    }
    if ((r & 0xdf) != 'C') {
        say("1k xmodem error, = %d", r);
        return false;
    }
    say("1k xmodem start.");
    for (long seq = 1, at = 0;; seq++, at += 1024) {
        if (rem >= 1024) {
            memcpy(block, image + at, 1024);
            rem -= 1024;
        } else {
            memset(block, 0xff, sizeof(block));
            memcpy(block, image + at, rem);
            rem = 0;
        }
        put_packet((uint8_t)seq, block, pkt);
        uart_write(pkt, sizeof(pkt));
        r = wait_respond(10, 1);
        if (r == CAN) {
            say("1k xmodem transfer canceled. (packet %ld of %ld)", seq, total);
            return false;
        }
        if (r == NAK) {
            say("1k xmodem bad packet, pn= %ld (of %ld)", seq, total);
            return false;
        }
        if (r != ACK) {
            say("1k xmodem error, = %d (packet %ld of %ld)", r, seq, total);
            return false;
        }
        if (seq % 64 == 0 || rem == 0)
            say("1k xmodem send packet: %ld , progress: %ld %%, rem: %ld", seq, 100 * (image_len - rem) / image_len, rem);
        if (rem == 0)
            break;
    }
    r = EOT;
    uart_write(&r, 1);
    r = wait_respond(10, 1);
    if (r != ACK) {
        say("1k xmodem error, = %d (after EOT)", r);
        return false;
    }
    say("1k xmodem send finished.");
    return true;
}

/* An X6200: its device tree says so. Anything that touches the base is refused on any other radio: an X6100's base
 * is written another way (its Morse key line held low while it is reset), and this file's I2C command and serial
 * bytes are not for it */
static bool is_x6200(void) {
    char   buf[512];
    FILE  *f = fopen(DT_COMPAT, "rb");
    size_t n;

    if (!f) return false;
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    for (size_t i = 0; i + 11 <= n; i++)
        if (!memcmp(buf + i, "xiegu,x6200", 11)) return true;
    return false;
}

static int check(bool for_flash, bool recover) {
    char v[160];
    int  busy;

    if (!is_x6200()) {
        say("refused: this radio is not an X6200 (%s has no xiegu,x6200): only the X6200's base is written by this, "
            "and nothing was sent to the base", DT_COMPAT);
        return EXIT_REFUSED;
    }
    busy = holders(UART) + holders(I2C_BUS);
    if (busy && for_flash) {
        say("refused: stop what holds them first (the GUI: tools/base_update/flash.sh does it)");
        return EXIT_REFUSED;
    }
    if (!busy)
        say("nothing else holds %s or %s", UART, I2C_BUS);
    i2c = open(I2C_BUS, O_RDWR);
    if (i2c < 0) {
        say("%s: %s", I2C_BUS, strerror(errno));
        return EXIT_REFUSED;
    }
    uint8_t ready = 0;

    if (!base_read(REG_READY, &ready, 1) || !base_version(v, sizeof(v))) {
        say("the base does not answer on I2C (0x%02x): %s", BASE_ADDR, strerror(errno));
        if (recover) {
            say("--recover: going on without it (a base with no application has nothing to answer with)");
            return EXIT_DONE;
        }
        return EXIT_REFUSED;
    }
    say("the base answers on I2C: ready 0x%02x, version \"%s\"", ready, v);
    if (for_flash && !ready) {
        say("refused: the base says it is not ready");
        return EXIT_REFUSED;
    }
    return EXIT_DONE;
}

int main(int argc, char **argv) {
    bool        do_flash = false, yes = false, recover = false, identify = false;
    const char *path     = NULL;
    int         rc;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check") && i + 1 < argc)
            path = argv[++i];
        else if (!strcmp(argv[i], "--flash") && i + 1 < argc)
            path = argv[++i], do_flash = true;
        else if (!strcmp(argv[i], "--yes"))
            yes = true;
        else if (!strcmp(argv[i], "--recover"))
            recover = true;
        else if (!strcmp(argv[i], "--held-by") && i + 1 < argc)
            held_by = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--unknown-ok"))
            unknown_ok = true;
        else if (!strcmp(argv[i], "--identify") && i + 1 < argc)
            path = argv[++i], identify = true;
        else
            path = NULL, i = argc;
    }
    if (!path || (do_flash && !yes) || (recover && !do_flash) || (identify && do_flash)) {
        fprintf(stderr, "usage: %s --check FILE | --flash FILE --yes [--recover] [--held-by PID] | --identify FILE  "
                        "[--unknown-ok]\n", argv[0]);
        return EXIT_REFUSED;
    }
    if (load(path) < 0)
        return EXIT_REFUSED;
    if (identify) {                     /* the file only: its version and SHA-256 on a line */
        printf("xiegu-base %s %s\n", image_ver, image_sha);
        return EXIT_DONE;
    }
    rc = check(do_flash, recover);
    if (rc != EXIT_DONE || !do_flash) {
        if (!do_flash)
            say(rc == EXIT_DONE ? "check done: nothing was sent to the base" : "check failed");
        return rc;
    }
    if (uart_open() < 0)
        return EXIT_REFUSED;

    /* From here on the transfer finishes whatever happens to the session that started it */
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    say("writing the base: hang-up, interrupt and terminate signals ignored until it ends");

    say("started, %ld bytes", image_len);
    to_bootloader();
    say("[OK] Start IAP connection");
    if (!send_cmd('c', 'y')) {
        say("[ERR] IAP failed, connect timeout");
        say("FAILED before the erase: nothing was erased; the base's application should still be there");
        return EXIT_BEFORE_ERASE;
    }
    say("[OK] Start IAP erase app flash");
    if (!send_cmd('r', 'g')) {
        say("[ERR] IAP failed, can't erase flash");
        say("FAILED at the erase: the base's application may be gone. Run the same with --recover; do not switch off");
        return EXIT_AFTER_ERASE;
    }
    say("[OK] Start IAP xmodem transfer");
    if (!send_cmd('p', 'g')) {
        say("[ERR] IAP failed, can't start xmodem");
        say("FAILED after the erase: the base has no application. Run the same with --recover; do not switch off");
        return EXIT_AFTER_ERASE;
    }
    say("[OK] xmodem started");
    if (!send_file()) {
        say("[ERR] xmodem send file failed");
        say("FAILED after the erase: the base has no application. Run the same with --recover; do not switch off");
        return EXIT_AFTER_ERASE;
    }
    say("finished.");
    /* Xiegu's app ends here. Its bootloader did not start the new application by itself on the radio this was first
     * run on (2026-11-13): the I2C bus stayed locked ("mv64xxx: I2C bus locked") and the radio switched itself off
     * about two minutes later; switched on again, its base ran 1.0.8. So a minute's look, by the clock (a locked bus
     * makes each read take seconds), then the power cycle said */
    say("written. Looking for the base's new application on I2C for %d s", BACK_WAIT_S);
    struct timespec until;

    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_sec += BACK_WAIT_S;
    for (;;) {
        struct timespec now;
        char            v[160];
        uint8_t         ready = 0;

        msleep(1000);
        if (base_read(REG_READY, &ready, 1) && ready && base_version(v, sizeof(v))) {
            say("the base is back: version \"%s\"", v);
            return EXIT_DONE;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec >= until.tv_sec)
            break;
    }
    say("written. The base did not start the new firmware by itself: switch the radio on again (it may switch itself "
        "off first, a minute or two from now); then tools/base_update/flash.sh --check shows the version");
    return EXIT_DONE;
}

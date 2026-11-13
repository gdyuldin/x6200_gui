/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  base_flash.c's pieces on their own, on this computer (tools/base_update/test/run.sh builds it with the
 *  stub headers): SHA-256 against FIPS 180-2's test vectors, the packet CRC against CRC-16/XMODEM's check value and
 *  against Xiegu's own Xmodem::crc16 as disassembled from x6200_ui_v100 1.0.8 (2000 random packets), the packet's
 *  frame, and the file check: Xiegu's base files taken (when build/base_update has them), anything else refused.
 */

#define main base_flash_main
#include "../base_flash.c"
#undef main

/* Xmodem::crc16 (x6200_ui_v100 1.0.8, 0x9adf4): for each data bit, MSB first, the register shifted, the polynomial
 * folded in once if its top bit was set and once if the data bit is */
static uint16_t xiegu_crc16(const uint8_t *p, unsigned n, uint32_t poly) {
    uint32_t r = 0;

    for (unsigned i = 0; i < n; i++)
        for (int m = 0x80; m; m >>= 1) {
            uint32_t top = r & 0x8000;

            r <<= 1;
            if (top)
                r ^= poly;
            if (p[i] & m)
                r ^= poly;
        }
    return (uint16_t)r;
}

static int fails;

static void expect(bool ok, const char *what) {
    printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    fails += !ok;
}

int main(int argc, char **argv) {
    char    h[65];
    uint8_t pkt[1029], blk[1024];

    clock_gettime(CLOCK_MONOTONIC, &t0);
    sha256_hex((const uint8_t *)"abc", 3, h);
    expect(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 of \"abc\"");
    sha256_hex((const uint8_t *)"", 0, h);
    expect(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "SHA-256 of \"\"");
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256_hex((const uint8_t *)m, strlen(m), h);
    expect(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "SHA-256 of the 448-bit vector");

    memset(blk, 0, sizeof(blk));
    memcpy(blk, "123456789", 9);
    expect(xiegu_crc16(blk, 9, 0x1021) == 0x31c3, "Xiegu's CRC gives CRC-16/XMODEM's check value 0x31c3");

    bool same = true;
    srand(7);
    for (int t = 0; t < 2000 && same; t++) {
        for (int i = 0; i < 1024; i++)
            blk[i] = (uint8_t)rand();
        put_packet((uint8_t)(t + 1), blk, pkt);
        uint16_t x = xiegu_crc16(blk, 1024, 0x1021);
        same = pkt[0] == 0x02 && pkt[1] == (uint8_t)(t + 1) && pkt[2] == (uint8_t)~(t + 1) &&
               !memcmp(pkt + 3, blk, 1024) && pkt[1027] == x >> 8 && pkt[1028] == (x & 0xff);
    }
    expect(same, "2000 packets: STX, number, complement, data, Xiegu's CRC high byte first");

    /* base_flash's known[] and the release list: the same files, both ways (argv[1]: base_releases.txt) */
    FILE *rf = argc > 1 ? fopen(argv[1], "r") : NULL;
    if (rf) {
        char   line[600];
        size_t n_known = sizeof(known) / sizeof(known[0]), in_list = 0;
        bool   all = true;

        while (fgets(line, sizeof(line), rf)) {
            char ver[16], built[16], name[80], sha[80];
            long size;

            if (line[0] == '#' || sscanf(line, "%15s %15s %79s %ld %79s", ver, built, name, &size, sha) != 5) continue;
            bool found = false;
            for (size_t k = 0; k < n_known; k++)
                if (!strncmp(known[k].name, name, strlen(name)) && known[k].size == size &&
                    !strcmp(known[k].sha256, sha) && !strcmp(known[k].ver, ver))
                    found = true;
            if (!found) printf("      %s %s is in the list but not in base_flash's known[]\n", ver, name);
            all = all && found;
            in_list++;
        }
        fclose(rf);
        expect(all && in_list == n_known, "base_flash's known[] and base_releases.txt: the same files");
    }
    for (int i = 2; i < argc; i++) {
        image = NULL;
        expect(load(argv[i]) == 0, argv[i]);
        free(image);
    }
    FILE *f = fopen("units_junk.xgf", "wb");     /* in the directory run.sh works in */
    if (f) {
        for (int i = 0; i < 32768; i++)
            fputc(rand(), f);
        fclose(f);
        expect(load("units_junk.xgf") < 0, "a file not Xiegu's is refused");
        /* --unknown-ok takes it, unless it is named for another radio */
        unknown_ok = true;
        image      = NULL;
        expect(load("units_junk.xgf") == 0, "--unknown-ok takes an unknown file that passes the checks");
        free(image);
        rename("units_junk.xgf", "XIEGU_X6100_units_junk.xgf");
        image = NULL;
        expect(load("XIEGU_X6100_units_junk.xgf") < 0, "an X6100 base file is refused, --unknown-ok or not");
        rename("XIEGU_X6100_units_junk.xgf", "g90_units_junk.xgf");
        image = NULL;
        expect(load("g90_units_junk.xgf") < 0, "a G90 base file is refused, --unknown-ok or not");
        unknown_ok = false;
        remove("g90_units_junk.xgf");
    }
    printf("%s\n", fails ? "UNIT TESTS FAILED" : "unit tests: all pass");
    return fails != 0;
}

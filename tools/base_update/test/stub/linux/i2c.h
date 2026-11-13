/* For the host tests only: <linux/i2c.h> as base_flash.c uses it, and B1152000, which a Mac's <termios.h> lacks (its
 * speeds are plain numbers there, so a pseudo-terminal takes it) */
#pragma once
#include <stdint.h>
struct i2c_msg {
    uint16_t addr, flags, len;
    uint8_t *buf;
};
struct i2c_rdwr_ioctl_data {
    struct i2c_msg *msgs;
    uint32_t        nmsgs;
};
#define I2C_M_RD 1
#ifndef B1152000
#define B1152000 1152000
#endif

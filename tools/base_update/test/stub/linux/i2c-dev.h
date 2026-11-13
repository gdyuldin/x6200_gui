/* For the host tests only (tools/base_update/test): what base_flash.c needs of <linux/i2c-dev.h> on a host that has
 * no Linux headers (a Mac). The I2C ioctls then go to /dev/null and fail, as a base without an application does */
#pragma once
#define I2C_RDWR 0x0707

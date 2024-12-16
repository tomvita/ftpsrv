/*
 * PMIC Real Time Clock driver for Nintendo Switch's MAX77620-RTC
 *
 * Copyright (c) 2018-2022 CTCaer
 * Copyright (c) 2019 shchmue
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "max77620-rtc.h"

#define MAX77620_RTC_NR_TIME_REGS   7

#define MAX77620_RTC_UPDATE0_REG    0x04
#define  MAX77620_RTC_WRITE_UPDATE  BIT(0)
#define  MAX77620_RTC_READ_UPDATE   BIT(4)

#define MAX77620_ALARM1_SEC_REG     0x0E
#define MAX77620_ALARM1_WEEKDAY_REG 0x11
#define MAX77620_ALARM1_YEAR_REG    0x13
#define MAX77620_ALARM2_SEC_REG     0x15
#define MAX77620_ALARM2_WEEKDAY_REG 0x18
#define MAX77620_ALARM2_YEAR_REG    0x1A
#define  MAX77620_RTC_ALARM_EN_MASK	BIT(7)

#define RTC_REBOOT_REASON_MAGIC 0x77 // 7-bit reg.

#define R_TRY(x) { Result _rc = x; if (R_FAILED(_rc)) { return _rc; } }

static Result i2c_send_byte(I2cSession* s, u8 addr, u8 value) {
    const struct {
		u8 addr;
		u8 value;
	} in = { addr, value };
    return i2csessionSendAuto(s, &in, sizeof(in), I2cTransactionOption_All);
}

static Result i2c_recv_byte(I2cSession* s, u8 addr, u8* value) {
    R_TRY(i2csessionSendAuto(s, &addr, sizeof(addr), I2cTransactionOption_All));
    return i2csessionReceiveAuto(s, value, sizeof(*value), I2cTransactionOption_All);
}

static void msleep(u64 ms) {
    svcSleepThread(ms * 1000000ULL);
}

Result max77620_rtc_stop_alarm(I2cSession* s)
{
	u8 val = 0;

	// Update RTC regs from RTC clock.
	R_TRY(i2c_send_byte(s, MAX77620_RTC_UPDATE0_REG, MAX77620_RTC_READ_UPDATE));
	msleep(16);

	// Stop alarm for both ALARM1 and ALARM2. Horizon uses ALARM2.
	for (int i = 0; i < (MAX77620_RTC_NR_TIME_REGS * 2); i++)
	{
		R_TRY(i2c_recv_byte(s, MAX77620_ALARM1_SEC_REG + i, &val));
		val &= ~MAX77620_RTC_ALARM_EN_MASK;
		R_TRY(i2c_send_byte(s, MAX77620_ALARM1_SEC_REG + i, val));
	}

	// Update RTC clock from RTC regs.
	return i2c_send_byte(s, MAX77620_RTC_UPDATE0_REG, MAX77620_RTC_WRITE_UPDATE);
}

Result max77620_rtc_set_reboot_reason(I2cSession* s, rtc_reboot_reason_t* rr)
{
	R_TRY(max77620_rtc_stop_alarm(s));

	// Set reboot reason.
	R_TRY(i2c_send_byte(s, MAX77620_ALARM1_YEAR_REG, rr->enc.val1));
	R_TRY(i2c_send_byte(s, MAX77620_ALARM2_YEAR_REG, rr->enc.val2));

	// Set reboot reason magic.
	R_TRY(i2c_send_byte(s, MAX77620_ALARM1_WEEKDAY_REG, RTC_REBOOT_REASON_MAGIC));
	R_TRY(i2c_send_byte(s, MAX77620_ALARM2_WEEKDAY_REG, RTC_REBOOT_REASON_MAGIC));

	// Update RTC clock from RTC regs.
	R_TRY(i2c_send_byte(s, MAX77620_RTC_UPDATE0_REG, MAX77620_RTC_WRITE_UPDATE));
	msleep(16);

    return 0;
}

/*
 * PMIC Real Time Clock driver for Nintendo Switch's MAX77620-RTC
 *
 * Copyright (c) 2018-2022 CTCaer
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

#ifndef _MFD_MAX77620_RTC_H_
#define _MFD_MAX77620_RTC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <switch.h>

typedef struct _rtc_rr_decoded_t
{
	u16 reason:4;
	u16 autoboot_idx:4;
	u16 autoboot_list:1;
	u16 ums_idx:3;
} rtc_rr_decoded_t;

typedef struct _rtc_rr_encoded_t
{
	u16 val1:6; // 6-bit reg.
	u16 val2:6; // 6-bit reg.
} rtc_rr_encoded_t;

typedef struct _rtc_reboot_reason_t
{
	union {
		rtc_rr_decoded_t dec;
		rtc_rr_encoded_t enc;
	};
} rtc_reboot_reason_t;

Result max77620_rtc_set_reboot_reason(I2cSession* s, rtc_reboot_reason_t* rr);

#ifdef __cplusplus
}
#endif

#endif /* _MFD_MAX77620_RTC_H_ */

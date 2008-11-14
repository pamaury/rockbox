/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2007 by Daniel Ankers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#ifndef _AS3514_H
#define _AS3514_H

extern int tenthdb2master(int db);

extern void audiohw_set_master_vol(int vol_l, int vol_r);
extern void audiohw_set_lineout_vol(int vol_l, int vol_r);
extern void audiohw_set_sample_rate(int sampling_control);

/* Register Descriptions */
#define AS3514_LINE_OUT_R 0x00
#define AS3514_LINE_OUT_L 0x01
#define AS3514_HPH_OUT_R  0x02
#define AS3514_HPH_OUT_L  0x03
#define AS3514_LSP_OUT_R  0x04
#define AS3514_LSP_OUT_L  0x05
#define AS3514_MIC1_R     0x06
#define AS3514_MIC1_L     0x07
#define AS3514_MIC2_R     0x08
#define AS3514_MIC2_L     0x09
#define AS3514_LINE_IN1_R 0x0a
#define AS3514_LINE_IN1_L 0x0b
#define AS3514_LINE_IN2_R 0x0c
#define AS3514_LINE_IN2_L 0x0d
#define AS3514_DAC_R      0x0e
#define AS3514_DAC_L      0x0f
#define AS3514_ADC_R      0x10
#define AS3514_ADC_L      0x11
#define AS3514_AUDIOSET1  0x14
#define AS3514_AUDIOSET2  0x15
#define AS3514_AUDIOSET3  0x16
#define AS3514_USB_UTIL   0x17  /* Only in AS3525 ? */
#define AS3514_PLLMODE    0x1d

#define AS3514_SYSTEM     0x20
#define AS3514_CVDD_DCDC3 0x21
#define AS3514_CHARGER    0x22
#define AS3514_DCDC15     0x23
#define AS3514_SUPERVISOR 0x24

#define AS3514_IRQ_ENRD0  0x25
#define AS3514_IRQ_ENRD1  0x26
#define AS3514_IRQ_ENRD2  0x27

#define AS3514_RTCV       0x28
#define AS3514_RTCT       0x29
#define AS3514_RTC_0      0x2a
#define AS3514_RTC_1      0x2b
#define AS3514_RTC_2      0x2c
#define AS3514_RTC_3      0x2d
#define AS3514_ADC_0      0x2e
#define AS3514_ADC_1      0x2f

#define AS3514_UID_0      0x30

/* Headphone volume goes from -73.5 ... +6dB */
#define VOLUME_MIN -735
#define VOLUME_MAX   60

/* ADC channels */
#define NUM_ADC_CHANNELS 13

#define ADC_BVDD         0  /* Battery voltage of 4V LiIo accumulator */
#define ADC_RTCSUP       1  /* RTC backup battery voltage */
#define ADC_UVDD         2  /* USB host voltage */
#define ADC_CHG_IN       3  /* Charger input voltage */
#define ADC_CVDD         4  /* Charger pump output voltage */
#define ADC_BATTEMP      5  /* Battery charging temperature */
#define ADC_MICSUP1      6  /* Voltage on MicSup1 for remote control
                               or external voltage measurement */
#define ADC_MICSUP2      7  /* Voltage on MicSup1 for remote control
                               or external voltage measurement */
#define ADC_VBE1         8  /* Measuring junction temperature @ 2uA */
#define ADC_VBE2         9  /* Measuring junction temperature @ 1uA */
#define ADC_I_MICSUP1    10 /* Current of MicSup1 for remote control detection */
#define ADC_I_MICSUP2    11 /* Current of MicSup2 for remote control detection */
#define ADC_VBAT         12 /* Single cell battery voltage */

#define ADC_UNREG_POWER  ADC_BVDD   /* For compatibility */


#define AS3514_I2C_ADDR 0x46

#endif /* _AS3514_H */

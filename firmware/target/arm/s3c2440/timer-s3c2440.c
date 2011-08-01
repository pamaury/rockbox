/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2011 by Amaury Pouly
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
#include "config.h"
#include "cpu.h"
#include "system.h"
#include "timer.h"
#include "logf.h"
#include "timer-target.h"

#if TIMER_NR == 0
# define TIMER              TIMER0
# define TIMER_MASK         TIMER0_MASK
# define TCMPB              TCMPB0
# define TCNTB              TCNTB0
#elif TIMER_NR == 1
# define TIMER              TIMER1
# define TIMER_MASK         TIMER1_MASK
# define TCMPB              TCMPB1
# define TCNTB              TCNTB1
#elif TIMER_NR == 2
# define TIMER              TIMER2
# define TIMER_MASK         TIMER2_MASK
# define TCMPB              TCMPB2
# define TCNTB              TCNTB2
#elif TIMER_NR == 3
# define TIMER              TIMER2
# define TIMER_MASK         TIMER2_MASK
# define TCMPB              TCMPB2
# define TCNTB              TCNTB2
#else
/* Timer 4 is used for tick */
# error You can only use timers 0, 1, 2 or 3 for timer
#endif

/* TCON fields shift */
#if TIMER_NR == 0
# define TCON_SHIFT     0
#else
# define TCON_SHIFT     (TIMER_NR * 4 + 4)
#endif

/* Timers 0,1 use prescaler 0 and 2,3 use prescaler 1 */
#define PRESCALER_SHIFT (TIMER_NR / 2)
#define DIVIDER_SHIFT   (TIMER_NR * 4)
#define GPBCON_MASK     (0x3 << (TIMER_NR * 2))

void TIMER(void)
{
    if(pfn_timer)
        pfn_timer();

    SRCPND = TIMER_MASK;
    INTPND = TIMER_MASK;
}

static void stop_timer(void)
{
    /* mask interrupt */
    INTMSK |= TIMER_MASK;

    /* stop any running TIMER */
    TCON &= ~(1 << TCON_SHIFT);

    /* clear pending */
    SRCPND = TIMER_MASK;
    INTPND = TIMER_MASK;
}

bool timer_set(long cycles, bool start)
{
    bool retval = false;

    /* Find the minimum factor that puts the counter in range 1-65535 */
    unsigned int prescaler = (cycles + 65534) / 65535;

    /* Maximum divider setting is x / 256 / 16 = x / 4096 - min divider
       is x / 2 however */
    if (prescaler <= 2048)
    {
        int oldlevel;
        unsigned int divider;

        if(start && pfn_unregister)
        {
            pfn_unregister();
            pfn_unregister = NULL;
        }

        oldlevel = disable_irq_save();

        TCMPB = 0;
        TCNTB = (unsigned int)cycles / prescaler;

        /* Max prescale is 255+1 */
        for (divider = 0; prescaler > 256; prescaler >>= 1, divider++);

        TCFG0 = (TCFG0 & ~(0xff << PRESCALER_SHIFT)) | (prescaler - 1) << PRESCALER_SHIFT;
        TCFG1 = (TCFG1 & ~(0xf << DIVIDER_SHIFT)) | divider << DIVIDER_SHIFT;

        restore_irq(oldlevel);

        retval = true;
    }

    return retval;
}

bool timer_start(void)
{
    bool retval = true;

    int oldstatus = disable_interrupt_save(IRQ_FIQ_STATUS);

    stop_timer();

    /* make sure GPBx is not set to TOUTx */
    if ((GPBCON & GPBCON_MASK) != 0x2)
    {
        /* manual update: on (to reset count) */
        TCON |= (2 << TCON_SHIFT);
        /* inverter: off, manual off */
        TCON &= ~(6 << TCON_SHIFT);
        /* auto reload: on */
        TCON |= (8 << TCON_SHIFT);
        /* start timer */
        TCON |= (1 << TCON_SHIFT);
        /* unmask interrupt */
        INTMSK &= ~TIMER_MASK;
    }

    /* check if timer was started */
    if(!(TCON & (1 << TCON_SHIFT)))
    {
        /* timer could not be started due to config error */
        logf("Timer error: GPB"#TIMER_NR" set to TOUT"#TIMER_NR);
        retval = false;
    }

    restore_interrupt(oldstatus);

    return retval;
}

void timer_stop(void)
{
    int oldstatus = disable_interrupt_save(IRQ_FIQ_STATUS);
    stop_timer();
    restore_interrupt(oldstatus);
}

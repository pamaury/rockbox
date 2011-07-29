/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (c) 2010 Thomas Martitz
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


#include <jni.h>
#include <stdbool.h>
#include "button.h"
#include "buttonmap.h"
#include "config.h"
#include "kernel.h"
#include "system.h"
#include "touchscreen.h"
#include "powermgmt.h"

extern JNIEnv *env_ptr;
extern jclass  RockboxService_class;
extern jobject RockboxService_instance;

static int last_y, last_x;
static int last_btns;

static enum {
    STATE_UNKNOWN,
    STATE_UP,
    STATE_DOWN,
} last_touch_state = STATE_UNKNOWN;

/*
 * this notifies us in an interrupt-like fashion whether the user just
 * began or stopped the touch action + where (pixel coordinates) */
JNIEXPORT void JNICALL
Java_org_rockbox_RockboxFramebuffer_touchHandler(JNIEnv*env, jobject this,
                                                 jboolean down, jint x, jint y)
{
    (void)env;
    (void)this;

    if (down)
        last_touch_state = STATE_DOWN;
    else
        last_touch_state = STATE_UP;

    last_x = x;
    last_y = y;
}

/*
 * this writes in an interrupt-like fashion the button events that the user
 * generated by pressing/releasing them to a variable */
JNIEXPORT bool JNICALL
Java_org_rockbox_RockboxFramebuffer_buttonHandler(JNIEnv*env, jclass class,
                                                  jint keycode, jboolean state)
{
    (void)env;
    (void)class;

    unsigned button = 0;

    if (!state)
    {
        button = multimedia_to_button((int)keycode);
        if (!button)
            button = dpad_to_button((int)keycode);
        if (button)
        {
            reset_poweroff_timer();
            queue_post(&button_queue, button, 0);
            return true;
        }
    }

    if (!button)
    {
        button = key_to_button(keycode);
    }

    if (button == BUTTON_NONE)
    {
        last_btns = button;
        return false;
    }

    if (state)
    {
        last_btns |= button;
    }
    else
    {
        last_btns &= (~button);
        return false;
    }

    return true;
}

void button_init_device(void)
{
    JNIEnv e = *env_ptr;
    jclass class = e->FindClass(env_ptr, "org/rockbox/monitors/HeadphoneMonitor");
    jmethodID constructor =     e->GetMethodID(env_ptr, class,
                                                "<init>",
                                                "(Landroid/content/Context;)V");
    e->NewObject(env_ptr, class,         
                        constructor,
                        RockboxService_instance);
}

int button_read_device(int *data)
{
    int btn = last_btns;
    /* Get grid button/coordinates based on the current touchscreen mode
     *
     * Caveat: the caller seemingly depends on *data always being filled with
     *         the last known touchscreen position, so always call
     *         touchscreen_to_pixels() */
    int touch = touchscreen_to_pixels(last_x, last_y, data);

    if (last_touch_state == STATE_DOWN)
        btn |= touch;

    return btn;
}

static int hp_state;
JNIEXPORT void JNICALL
Java_org_rockbox_monitors_HeadphoneMonitor_postHpStateChanged(JNIEnv *env,
                                                            jobject this,
                                                            jint state)
{
    (void)env; (void)this;
    hp_state = state;
}
/* Tell if anything is in the jack.
 * 
 * since this is called from the tick task, which isn't attached to
 * the dalvik VM, it's not permitted to make JNI calls (therefore
 * we need the above callback) */
bool headphones_inserted(void)
{
    /* 0 is disconnected, 1 and 2 are connected */
    return (hp_state == 0) ? false : true;
}


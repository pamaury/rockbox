/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id: $
 *
 * Copyright (C) 2007 by Karl Kurbjun
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
 
#include "inttypes.h"
#include "string.h"
#include "cpu.h"
#include "system.h"
#include "lcd.h"
#include "kernel.h"
#include "thread.h"
#include "ata.h"
#include "fat.h"
#include "disk.h"
#include "font.h"
#include "adc.h"
#include "backlight.h"
#include "backlight-target.h"
#include "button.h"
#include "panic.h"
#include "power.h"
#include "file.h"
#include "common.h"
#include "rbunicode.h"
#include "usb.h"
#include "spi.h"
#include "uart-target.h"
#include "tsc2100.h"
#include "time.h"

extern int line;

struct touch_calibration_point tl, br;

void touchpad_get_one_point(struct touch_calibration_point *p)
{
    int data = 0;
    int start = current_tick;
    while (TIME_AFTER(start+(HZ/3), current_tick))
    {
        if (button_read_device()&BUTTON_TOUCHPAD)
        {
            data = button_get_last_touch();
            p->val_x = data>>16;
            p->val_y = data&0xffff;
            start = current_tick;
        }
        else if (data == 0)
            start = current_tick;
    }
}

#define MARGIN 25
#define LEN    7
void touchpad_calibrate_screen(void)
{
    reset_screen();
    printf("touch the center of the crosshairs to calibrate");
    /* get the topleft value */
    lcd_hline(MARGIN-LEN, MARGIN+LEN, MARGIN);
    lcd_vline(MARGIN, MARGIN-LEN, MARGIN+LEN);
    lcd_update();
    tl.px_x = MARGIN; tl.px_y = MARGIN;
    touchpad_get_one_point(&tl);
    reset_screen();
    printf("touch the center of the crosshairs to calibrate");
    /* get the topright value */
    lcd_hline(LCD_WIDTH-MARGIN-LEN, LCD_WIDTH-MARGIN+LEN, LCD_HEIGHT-MARGIN);
    lcd_vline(LCD_WIDTH-MARGIN, LCD_HEIGHT-MARGIN-LEN, LCD_HEIGHT-MARGIN+LEN);
    lcd_update();
    br.px_x = LCD_WIDTH-MARGIN; br.px_y = LCD_HEIGHT-MARGIN;
    touchpad_get_one_point(&br);
    reset_screen();
    line++;
    printf("tl %d %d", tl.val_x, tl.val_y);
    printf("br %d %d", br.val_x, br.val_y);
    line++;
    set_calibration_points(&tl, &br);
}


void main(void)
{
    unsigned char* loadbuffer;
    int buffer_size;
    int rc;
    int(*kernel_entry)(void);

    power_init();
    lcd_init();
    system_init();
    kernel_init();
    adc_init();
    button_init();
    backlight_init();
    uart_init();

    font_init();
    spi_init();

    lcd_setfont(FONT_SYSFIXED);

    /* Show debug messages if button is pressed */
//    if(button_read_device())
        verbose = true;

    printf("Rockbox boot loader");
    printf("Version %s", APPSVERSION);

    usb_init();

    /* Enter USB mode without USB thread */
    if(usb_detect())
    {
        const char msg[] = "Bootloader USB mode";
        reset_screen();
        lcd_putsxy( (LCD_WIDTH - (SYSFONT_WIDTH * strlen(msg))) / 2,
                    (LCD_HEIGHT - SYSFONT_HEIGHT) / 2, msg);
        lcd_update();

        ide_power_enable(true);
        ata_enable(false);
        sleep(HZ/20);
        usb_enable(true);

        while (usb_detect())
        {
            ata_spin(); /* Prevent the drive from spinning down */
            sleep(HZ);
        }

        usb_enable(false);

        reset_screen();
        lcd_update();
    }
#if 0
    int button=0, *address=0x0, count=0;
    use_calibration(false);
    touchpad_calibrate_screen();
    use_calibration(true);
    while(true)
    {
        struct tm *t = get_time();
        printf("%d:%d:%d %d %d %d", t->tm_hour, t->tm_min, t->tm_sec, t->tm_mday, t->tm_mon, t->tm_year);
        printf("time: %d", mktime(t));
        button = button_read_device();
        if (button == BUTTON_POWER)
        {
            printf("reset");
            IO_GIO_BITSET1|=1<<10;
        }
        if(button==BUTTON_RC_PLAY)
            address+=0x02;
        else if (button==BUTTON_RC_DOWN)
            address-=0x02;
        else if (button==BUTTON_RC_FF)
            address+=0x1000;
        else if (button==BUTTON_RC_REW)
            address-=0x1000;
        if (button&BUTTON_TOUCHPAD)
        {
            unsigned int data = button_get_last_touch();
            printf("x: %d, y: %d", data>>16, data&0xffff);
            line-=3;
        }
        else line -=2;
    }
#endif
    printf("ATA");
    rc = ata_init();
    if(rc)
    {
        reset_screen();
        error(EATA, rc);
    }

    printf("disk");
    disk_init();

    printf("mount");
    rc = disk_mount_all();
    if (rc<=0)
    {
        error(EDISK,rc);
    }

    printf("Loading firmware");

    loadbuffer = (unsigned char*) 0x00900000;
    buffer_size = (unsigned char*)0x04900000 - loadbuffer;

    rc = load_firmware(loadbuffer, BOOTFILE, buffer_size);
    if(rc < 0)
        error(EBOOTFILE, rc);

    if (rc == EOK)
    {
        kernel_entry = (void*) loadbuffer;
        rc = kernel_entry();
    }
}

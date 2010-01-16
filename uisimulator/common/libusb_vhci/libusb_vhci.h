/*
 * Copyright (C) 2009 Michael Singer <michael@a-singer.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _LIBUSB_VHCI_H
#define _LIBUSB_VHCI_H 1

#include <pthread.h>
#include <stdint.h>

#define USB_VHCI_DEVICE_FILE "/dev/vhci-ctrl"

#ifdef _LIB_USB_VHCI_NOTHROW
#undef _LIB_USB_VHCI_NOTHROW
#endif

#define _LIB_USB_VHCI_NOTHROW

struct usb_vhci_iso_packet
{
	uint32_t offset;
	int32_t packet_length, packet_actual;
	int32_t status;
};

struct usb_vhci_urb
{
	uint64_t handle;
	uint8_t *buffer;
	struct usb_vhci_iso_packet *iso_packets;
	int32_t buffer_length, buffer_actual;
	int32_t packet_count, error_count;
	int32_t status, interval;
	uint16_t flags;
#define USB_VHCI_URB_FLAGS_SHORT_NOT_OK 0x0001
#define USB_VHCI_URB_FLAGS_ISO_ASAP     0x0002
#define USB_VHCI_URB_FLAGS_ZERO_PACKET  0x0040
	uint16_t wValue, wIndex, wLength;
	uint8_t bmRequestType, bRequest;
	uint8_t devadr, epadr;
	uint8_t type;
#define USB_VHCI_URB_TYPE_ISO     0
#define USB_VHCI_URB_TYPE_INT     1
#define USB_VHCI_URB_TYPE_CONTROL 2
#define USB_VHCI_URB_TYPE_BULK    3
};

struct usb_vhci_port_stat
{
	uint16_t status, change;
#define USB_VHCI_PORT_STAT_CONNECTION    0x0001
#define USB_VHCI_PORT_STAT_ENABLE        0x0002
#define USB_VHCI_PORT_STAT_SUSPEND       0x0004
#define USB_VHCI_PORT_STAT_OVERCURRENT   0x0008
#define USB_VHCI_PORT_STAT_RESET         0x0010
#define USB_VHCI_PORT_STAT_POWER         0x0100
#define USB_VHCI_PORT_STAT_LOW_SPEED     0x0200
#define USB_VHCI_PORT_STAT_HIGH_SPEED    0x0400
#define USB_VHCI_PORT_STAT_C_CONNECTION  0x0001
#define USB_VHCI_PORT_STAT_C_ENABLE      0x0002
#define USB_VHCI_PORT_STAT_C_SUSPEND     0x0004
#define USB_VHCI_PORT_STAT_C_OVERCURRENT 0x0008
#define USB_VHCI_PORT_STAT_C_RESET       0x0010
	uint8_t index, flags;
#define USB_VHCI_PORT_STAT_FLAG_RESUMING 0x01
};
#define USB_VHCI_DATA_RATE_FULL 0
#define USB_VHCI_DATA_RATE_LOW  1
#define USB_VHCI_DATA_RATE_HIGH 2

struct usb_vhci_work
{
	union
	{
		uint64_t handle;
		struct usb_vhci_urb urb;
		struct usb_vhci_port_stat port_stat;
	} work;

	int type;
#define USB_VHCI_WORK_TYPE_PORT_STAT   0
#define USB_VHCI_WORK_TYPE_PROCESS_URB 1
#define USB_VHCI_WORK_TYPE_CANCEL_URB  2
};

int usb_vhci_open(uint8_t port_count,
                  int32_t *id,
                  int32_t *usb_busnum,
                  char    **bus_id) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_close(int fd) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_fetch_work(int fd, struct usb_vhci_work *work) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_fetch_data(int fd, const struct usb_vhci_urb *urb) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_giveback(int fd, const struct usb_vhci_urb *urb) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_port_connect(int fd, uint8_t port, uint8_t data_rate) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_port_disconnect(int fd, uint8_t port) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_port_disable(int fd, uint8_t port) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_port_resumed(int fd, uint8_t port) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_port_overcurrent(int fd, uint8_t port, uint8_t set) _LIB_USB_VHCI_NOTHROW;
int usb_vhci_port_reset_done(int fd, uint8_t port, uint8_t enable) _LIB_USB_VHCI_NOTHROW;

#define usb_vhci_is_out(epadr)    !((epadr) & 0x80)
#define usb_vhci_is_in(epadr)     !!((epadr) & 0x80)
#define usb_vhci_is_iso(type)     ((type) == USB_VHCI_URB_TYPE_ISO)
#define usb_vhci_is_int(type)     ((type) == USB_VHCI_URB_TYPE_INT)
#define usb_vhci_is_control(type) ((type) == USB_VHCI_URB_TYPE_CONTROL)
#define usb_vhci_is_bulk(type)    ((type) == USB_VHCI_URB_TYPE_BULK)

#endif // _LIBUSB_VHCI_H


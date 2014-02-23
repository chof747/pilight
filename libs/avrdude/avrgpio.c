/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Support for bitbanging GPIO pins using the /sys/class/gpio interface
 *
 * Copyright (C) 2010 Radoslav Kolev <radoslav@kolev.info>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "avrdude.h"
#include "avr.h"
#include "pindefs.h"
#include "pgm.h"
#include "avrbitbang.h"

/*
 * GPIO user space helpers
 *
 * Copyright 2009 Analog Devices Inc.
 * Michael Hennerich (hennerich@blackfin.uclinux.org)
 *
 * Licensed under the GPL-2 or later
 */

#define GPIO_DIR_IN	0
#define GPIO_DIR_OUT	1

static int gpio_export(unsigned gpio)
{
	int fd, len;
	char buf[11];

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0) {
		perror("gpio/export");
		return fd;
	}

	len = snprintf(buf, sizeof(buf), "%d", gpio);
	write(fd, buf, len);
	close(fd);

	return 0;
}

static int gpio_unexport(unsigned gpio)
{
	int fd, len;
	char buf[11];

	fd = open("/sys/class/gpio/unexport", O_WRONLY);
	if (fd < 0) {
		perror("gpio/export");
		return fd;
	}

	len = snprintf(buf, sizeof(buf), "%d", gpio);
	write(fd, buf, len);
	close(fd);
	return 0;
}

static int gpio_dir(unsigned gpio, unsigned dir)
{
	int fd, len;
	char buf[60];

	len = snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", gpio);

	fd = open(buf, O_WRONLY);
	if (fd < 0) {
		perror("gpio/direction");
		return fd;
	}

	if (dir == GPIO_DIR_OUT)
		write(fd, "out", 4);
	else
		write(fd, "in", 3);

	close(fd);
	return 0;
}

static int gpio_dir_out(unsigned gpio)
{
	return gpio_dir(gpio, GPIO_DIR_OUT);
}

static int gpio_dir_in(unsigned gpio)
{
	return gpio_dir(gpio, GPIO_DIR_IN);
}

/*
 * End of GPIO user space helpers
 */

#define N_GPIO 256

/*
* an array which holds open FDs to /sys/class/gpio/gpioXX/value for all needed pins
*/
static int gpio_fds[N_GPIO] ;


static int gpio_setpin(PROGRAMMER * pgm, int pin, int value)
{
  int r;

  if (pin & PIN_INVERSE)
  {
    value  = !value;
    pin   &= PIN_MASK;
  }

    if ( gpio_fds[pin] < 0 )
      return -1;

  if (value)
    r=write(gpio_fds[pin], "1", 1);
  else
    r=write(gpio_fds[pin], "0", 1);

  if (r!=1) return -1;

  if (pgm->ispdelay > 1)
    bitbang_delay(pgm->ispdelay);

  return 0;
}

static int gpio_getpin(PROGRAMMER * pgm, int pin)
{
  unsigned char invert=0;
  char c;

  if (pin & PIN_INVERSE)
  {
    invert = 1;
    pin   &= PIN_MASK;
  }

  if ( gpio_fds[pin] < 0 )
    return -1;

  if (lseek(gpio_fds[pin], 0, SEEK_SET)<0)
    return -1;

  if (read(gpio_fds[pin], &c, 1)!=1)
    return -1;

  if (c=='0')
    return 0+invert;
  else if (c=='1')
    return 1-invert;
  else
    return -1;

}

static int gpio_highpulsepin(PROGRAMMER * pgm, int pin)
{

  if ( gpio_fds[pin & PIN_MASK] < 0 )
    return -1;

  gpio_setpin(pgm, pin, 1);
  gpio_setpin(pgm, pin, 0);

  return 0;
}



static void gpio_display(PROGRAMMER *pgm, const char *p)
{
  /* MAYBE */
}

static void gpio_enable(PROGRAMMER *pgm)
{
  /* nothing */
}

static void gpio_disable(PROGRAMMER *pgm)
{
  /* nothing */
}

static void gpio_powerup(PROGRAMMER *pgm)
{
  /* nothing */
}

static void gpio_powerdown(PROGRAMMER *pgm)
{
  /* nothing */
}

static int gpio_open(PROGRAMMER *pgm, char *port)
{
  int r,i;
  char filepath[60];


  bitbang_check_prerequisites(pgm);


  for (i=0; i<N_GPIO; i++)
	gpio_fds[i]=-1;

  for (i=0; i<N_PINS; i++) {
    if (pgm->pinno[i] != 0) {
        if ((r=gpio_export(pgm->pinno[i])) < 0)
	    return r;


	if (i == PIN_AVR_MISO)
	    r=gpio_dir_in(pgm->pinno[i]);
	else
	    r=gpio_dir_out(pgm->pinno[i]);

	if (r < 0)
	    return r;

	if ((r=snprintf(filepath, sizeof(filepath), "/sys/class/gpio/gpio%d/value", pgm->pinno[i]))<0)
	    return r;

        if ((gpio_fds[pgm->pinno[i]]=open(filepath, O_RDWR))<0)
	    return gpio_fds[pgm->pinno[i]];

    }
  }

 return(0);
}

static void gpio_close(PROGRAMMER *pgm)
{
  int i;

  pgm->setpin(pgm, pgm->pinno[PIN_AVR_RESET], 1);

  for (i=0; i<N_GPIO; i++)
    if (gpio_fds[i]>=0) {
       close(gpio_fds[i]);
       gpio_unexport(i);
    }
  return;
}

void gpio_initpgm(PROGRAMMER *pgm)
{
  strcpy(pgm->type, "GPIO");

  pgm->rdy_led        = bitbang_rdy_led;
  pgm->err_led        = bitbang_err_led;
  pgm->pgm_led        = bitbang_pgm_led;
  pgm->vfy_led        = bitbang_vfy_led;
  pgm->initialize     = bitbang_initialize;
  pgm->display        = gpio_display;
  pgm->enable         = gpio_enable;
  pgm->disable        = gpio_disable;
  pgm->powerup        = gpio_powerup;
  pgm->powerdown      = gpio_powerdown;
  pgm->program_enable = bitbang_program_enable;
  pgm->chip_erase     = bitbang_chip_erase;
  pgm->cmd            = bitbang_cmd;
  pgm->open           = gpio_open;
  pgm->close          = gpio_close;
  pgm->setpin         = gpio_setpin;
  pgm->getpin         = gpio_getpin;
  pgm->highpulsepin   = gpio_highpulsepin;
  pgm->read_byte      = avr_read_byte_default;
  pgm->write_byte     = avr_write_byte_default;
}
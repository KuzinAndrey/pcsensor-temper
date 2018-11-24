PCSensor / TEMPer driver for Linux/Solaris 
====

A CLI tool for PCsensor TEMPer USB thermometer. http://www.pcsensor.com/

This is based on PCSensor v1.0.1 by Juan Carlos Perez, introduced at https://relavak.wordpress.com/2009/10/17/temper-temperature-sensor-linux-driver/

Supported devices:
- 0c45:7401 with 1 temperature sensor (Gold TEMPer)
  - Tested with TEMPerV1.4

![Gold TEMPer](images/goldtemper.jpg)

- 0c45:7401 with 2 temperature sensors (TEMPer2)
  - Tested with TEMPer2_M12_V1.3

![TEMPer2](images/temper2.jpg)

- 0c45:7402 with temperature and humidity sensors (TEMPerHUM)
  - Tested with TEMPer1F_H1_V1.4

![TEMPerHUM](images/temperhum.jpg)

I have worked on, 
- Change libusb-0.1 -> libusb-1.0 (to be able to build on Solaris)
- Support TEMperHUM
- Support multiple devices
- Add Munin plugin http://munin-monitoring.org/
- Some code cleanups, fix indents, typos, ...

# Build
## Solaris 11
```
# pkg install gcc libusb-1
$ make
...
# cp pcsensor /usr/local/bin/
```

## RHEL 6
```
# yum install gcc libusb1-devel
$ make
...
# cp pcsensor /usr/local/bin/
```

# Usage
```
$ sudo ./pcsensor
2017-09-26T20:49:24     0       temperature     31.69 C
2017-09-26T20:49:24     1       temperature     32.12 C
2017-09-26T20:49:24     2       temperature     33.44 C
2017-09-26T20:49:24     2       humidity        62.63 %
...
```

libusb_detach_kernel_driver does not seem to work on Solaris. Thus, it looks necessary to unload hid driver manually.
Are there any better solutions? (See pcsensor.sh)

(Added on 2018-11-24)
It has been tested on Solaris 11.3 and libusb-1.0.20, and looked working fine.
On Solaris 11.4, libusb-1.0.21 was introduced. But, pcsensor doesn't seem to work with 1.0.21.
I found it can work with libusb-1.0.21-rc2, but cannot work with libusb-1.0.21-rc3 or above.
If you would like to use it on Solaris 11.4, get libusb-1.0.21-rc2 from github and install it.

# Example

Munin graph sample

![Munin](images/munin-temper-day.png)

Original document below
====
```
pcsensor
========

PCSensor / TEMPer2 driver for linux

This is based on PCSensor v. 1.0.1 by Juan Carlos Perez

All I've done is tweak the tool to output both the temperature from the internal
as well as the external sensor.

/*
 * pcsensor.c by Juan Carlos Perez (c) 2011 (cray@isp-sl.com)
 * based on Temper.c by Robert Kavaler (c) 2009 (relavak.com)
 * All rights reserved.
 *
 * 2011/08/30 Thanks to EdorFaus: bugfix to support negative temperatures
 *
 * Temper driver for linux. This program can be compiled either as a library
 * or as a standalone program (-DUNIT_TEST). The driver will work with some
 * TEMPer usb devices from RDing (www.PCsensor.com).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY Juan Carlos Perez ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Robert kavaler BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
```

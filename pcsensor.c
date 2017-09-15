/*
 * pcsensor.c by Juan Carlos Perez (c) 2011 (cray@isp-sl.com)
 * based on Temper.c by Robert Kavaler (c) 2009 (relavak.com)
 * All rights reserved.
 *
 * 2011/08/30 Thanks to EdorFaus: bugfix to support negative temperatures
 * 2017/08/30 Improved by K.Cima: changed libusb-0.1 -> libusb-1.0
 *            https://github.com/shakemid/pcsensor
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


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <libusb.h>

#define VERSION "1.1.0"

#define VENDOR_ID  0x0c45
#define PRODUCT_ID 0x7401

#define INTERFACE1 0x00
#define INTERFACE2 0x01

#define MAX_DEV 8

const static int reqIntLen=8;
const static int endpoint_Int_in=0x82; /* endpoint 0x81 address for IN */
const static int timeout=5000; /* timeout in ms */

const static char uTemperature[] = { 0x01, 0x80, 0x33, 0x01, 0x00, 0x00, 0x00, 0x00 };
const static char uIni1[] = { 0x01, 0x82, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00 };
const static char uIni2[] = { 0x01, 0x86, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00 };

static int bsalir=1;
static int debug=0;
static int seconds=5;
static int formato=1; //Celsius

static libusb_context *ctx = NULL;

void bad(const char *why) {
    fprintf(stderr,"Fatal error> %s\n",why);
    exit(17);
}

void usb_detach(libusb_device_handle *lvr_winusb, int iInterface) {
    int ret;

    ret = libusb_detach_kernel_driver(lvr_winusb, iInterface);
    if(ret) {
        if(errno == ENODATA) {
            if(debug) {
                printf("Device already detached\n");
            }
        } else {
            if(debug) {
                printf("Detach failed: %s[%d]\n", strerror(errno), errno);
                printf("Continuing anyway\n");
            }
        }
    } else {
        if(debug) {
            printf("detach successful\n");
        }
    }
}

int find_lvr_winusb(libusb_device_handle **handles) {
    int i, s, cnt, numdev;
    libusb_device **devs;

    //handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);

    cnt = libusb_get_device_list(ctx, &devs);
    if (cnt < 1) {
        fprintf(stderr, "Could not find USB device: %d\n", cnt);
    }

    numdev = 0;
    for (i = 0; i < cnt && numdev < MAX_DEV; i++) {
        struct libusb_device_descriptor desc;

        if ((s = libusb_get_device_descriptor(devs[i], &desc)) < 0) {
            fprintf(stderr, "Could not get USB device descriptor: %d\n", s);
            continue;
        }

        if (desc.idVendor == VENDOR_ID && desc.idProduct == PRODUCT_ID) {
            if ((s = libusb_open(devs[i], &handles[numdev])) < 0) {
                fprintf(stderr, "Could not open USB device: %d\n", s);
                continue;
            }

            if(debug) {
                unsigned char descmanu[256], descprod[256], descseri[256];
                libusb_get_string_descriptor_ascii(handles[numdev], desc.iManufacturer, descmanu, 256);
                libusb_get_string_descriptor_ascii(handles[numdev], desc.iProduct, descprod, 256);
                libusb_get_string_descriptor_ascii(handles[numdev], desc.iSerialNumber, descseri, 256);
                printf("lvr_winusb with VendorID:%04x ProductID:%04x Manufacturer:%s Product:%s Serial:%s found.\n", 
                       desc.idVendor, desc.idProduct, descmanu, descprod, descseri);
            }

            numdev++;
        }
    }

    libusb_free_device_list(devs, 1);

    return numdev;
}

int setup_libusb_access(libusb_device_handle **handles) {
    int i,numdev;

    libusb_init(&ctx);

    if(debug) {
        libusb_set_debug(ctx, 4); //LIBUSB_LOG_LEVEL_DEBUG
    } else {
        libusb_set_debug(ctx, 0); //LIBUSB_LOG_LEVEL_NONE
    }

    if((numdev = find_lvr_winusb(handles)) < 1) {
        fprintf(stderr, "Couldn't find the USB device, Exiting: %d\n", numdev);
        return -1;
    }

    for (i = 0; i < numdev; i++) {
        usb_detach(handles[i], INTERFACE1);
        usb_detach(handles[i], INTERFACE2);

        if (libusb_set_configuration(handles[i], 0x01) < 0) {
            fprintf(stderr, "Could not set configuration 1\n");
            return -1;
        }

        // Microdia tiene 2 interfaces
        int s;
        if ((s = libusb_claim_interface(handles[i], INTERFACE1)) < 0) {
            fprintf(stderr, "Could not claim interface. Error:%d\n", s);
            return -1;
        }

        if ((s = libusb_claim_interface(handles[i], INTERFACE2)) < 0) {
            fprintf(stderr, "Could not claim interface. Error:%d\n", s);
            return -1;
        }
    }

    return numdev;
}

void ini_control_transfer(libusb_device_handle *dev) {
    int r,i;

    char question[] = { 0x01,0x01 };

    r = libusb_control_transfer(dev, 0x21, 0x09, 0x0201, 0x00, (unsigned char *) question, 2, timeout);
    if(r < 0) {
        perror("USB control write"); bad("USB write failed");
    }

    if(debug) {
        for (i=0;i<reqIntLen; i++) printf("%02x ",question[i] & 0xFF);
        printf("\n");
    }
}

void control_transfer(libusb_device_handle *dev, const char *pquestion) {
    int r,i;

    char question[reqIntLen];

    memcpy(question, pquestion, sizeof question);

    r = libusb_control_transfer(dev, 0x21, 0x09, 0x0200, 0x01, (unsigned char *) question, reqIntLen, timeout);
    if(r < 0) {
        perror("USB control write"); bad("USB write failed");
    }

    if(debug) {
        for (i=0;i<reqIntLen; i++) printf("%02x ",question[i]  & 0xFF);
        printf("\n");
    }
}

void interrupt_read(libusb_device_handle *dev, unsigned char *answer) {
    int r,s,i;
    bzero(answer, reqIntLen);

    s = libusb_interrupt_transfer(dev, endpoint_Int_in, answer, reqIntLen, &r, timeout);
    if(r != reqIntLen) {
        fprintf(stderr, "USB read failed: %d\n", s);
        perror("USB interrupt read"); bad("USB read failed");
    }

    if(debug) {
        for (i=0;i<reqIntLen; i++) printf("%02x ",answer[i]  & 0xFF);

        printf("\n");
    }
}

void ex_program(int sig) {
    bsalir=1;

    (void) signal(SIGINT, SIG_DFL);
}

int main( int argc, char **argv) {
    libusb_device_handle **handles;
    int numdev,i;
    unsigned char *answer;
    int temperature;
    char strdate[20];
    float tempInC, tempExC;
    int c;
    struct tm *local;
    time_t t;

    while ((c = getopt (argc, argv, "vcfl::h")) != -1)
        switch (c)
        {
            case 'v':
                debug = 1;
                break;
            case 'c':
                formato=1; //Celsius
                break;
            case 'f':
                formato=2; //Fahrenheit
                break;
            case 'l':
                if (optarg!=NULL){
                    if (!sscanf(optarg,"%i",&seconds)==1) {
                        fprintf (stderr, "Error: '%s' is not numeric.\n", optarg);
                        exit(EXIT_FAILURE);
                    } else {
                        bsalir = 0;
                        break;
                    }
                } else {
                    bsalir = 0;
                    seconds = 5;
                    break;
                }
            case '?':
            case 'h':
                printf("pcsensor version %s\n",VERSION);
                printf("    Available options:\n");
                printf("        -h help\n");
                printf("        -v verbose\n");
                printf("        -l[n] loop every 'n' seconds, default value is 5s\n");
                printf("        -c output in Celsius (default)\n");
                printf("        -f output in Fahrenheit\n");

                exit(EXIT_FAILURE);
            default:
                if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(EXIT_FAILURE);
        }

    if (optind < argc) {
        fprintf(stderr, "Non-option ARGV-elements, try -h for help.\n");
        exit(EXIT_FAILURE);
    }

    handles = calloc(MAX_DEV, sizeof(libusb_device_handle*));
    if ((numdev = setup_libusb_access(handles)) < 1) {
        exit(EXIT_FAILURE);
    }

    (void) signal(SIGINT, ex_program);

    answer = calloc(reqIntLen, sizeof(unsigned char));
    /* This looks unnecessary
    for (i = 0; i < numdev; i++) {
        ini_control_transfer(handles[i]);

        control_transfer(handles[i], uTemperature);
        interrupt_read(handles[i], answer);

        control_transfer(handles[i], uIni1);
        interrupt_read(handles[i], answer);

        control_transfer(handles[i], uIni2);
        interrupt_read(handles[i], answer);
        interrupt_read(handles[i], answer);
    }
    */

    do {
        for (i = 0; i < numdev; i++) {
            control_transfer(handles[i], uTemperature);
            interrupt_read(handles[i], answer);

            temperature = (answer[3] & 0xFF) + ((signed char)answer[2] << 8);
            tempInC = temperature * (125.0 / 32000.0);

            temperature = (answer[5] & 0xFF) + ((signed char)answer[4] << 8);
            tempExC = temperature * (125.0 / 32000.0);

            t = time(NULL);
            local = localtime(&t);

            sprintf(strdate, "%04d-%02d-%02dT%02d:%02d:%02d",
                    local->tm_year +1900,
                    local->tm_mon + 1,
                    local->tm_mday,
                    local->tm_hour,
                    local->tm_min,
                    local->tm_sec);

            if (formato==2) {
                printf("%s\t%d\tinternal\t%.2f F\n", strdate, i, (9.0 / 5.0 * tempInC + 32.0));
                printf("%s\t%d\texternal\t%.2f F\n", strdate, i, (9.0 / 5.0 * tempExC + 32.0));
            } else {
                printf("%s\t%d\tinternal\t%.2f C\n", strdate, i, tempInC);
                printf("%s\t%d\texternal\t%.2f C\n", strdate, i, tempExC);
            }

            if (!bsalir)
                sleep(seconds);
        }
    } while (!bsalir);

    for (i = 0; i < numdev; i++) {
        libusb_release_interface(handles[i], INTERFACE1);
        libusb_release_interface(handles[i], INTERFACE2);

        libusb_close(handles[i]);
    }

    return 0;
}

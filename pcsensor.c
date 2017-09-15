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

#define VERSION "1.2.0"

/* TEMPer type definition */

typedef struct temper_type {
    const int vendor_id;
    const int product_id;
    const char product_name[256];
    const int check_product_name; // if set, check product name in forward match
    const int has_sensor; // number of temperature sensor
    const int has_humid;  // flag for humidity sensor
    void (*decode_func)();
} temper_type_t;

typedef struct temper_device {
    libusb_device_handle *handle;
    temper_type_t *type;
} temper_device_t;

void decode_answer_fm75();
void decode_answer_sht1x();

#define TEMPER_TYPES 3

temper_type_t tempers[TEMPER_TYPES] = {
    { 0x0c45, 0x7401, "TEMPer2",   1, 2, 0, decode_answer_fm75  }, // TEMPer2* eg. TEMPer2V1.3
    { 0x0c45, 0x7401, "TEMPer1",   0, 1, 0, decode_answer_fm75  }, // other 0c45:7401 eg. TEMPerV1.4
    { 0x0c45, 0x7402, "TEMPerHUM", 0, 1, 1, decode_answer_sht1x },
};

/* global variables */

#define MAX_DEV 8

#define INTERFACE1 0x00
#define INTERFACE2 0x01

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

/* functions */

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

int find_lvr_winusb(temper_device_t *devices) {
    int i, j, s, cnt, numdev;
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

        for (j = 0; j < TEMPER_TYPES; j++) {
            if (desc.idVendor == tempers[j].vendor_id && desc.idProduct == tempers[j].product_id) {
                unsigned char bus, addr, descmanu[256], descprod[256], descseri[256];

                bus = libusb_get_bus_number(devs[i]);
                addr = libusb_get_device_address(devs[i]);

                if ((s = libusb_open(devs[i], &devices[numdev].handle)) < 0) {
                    fprintf(stderr, "Could not open USB device: %d\n", s);
                    continue;
                }

                libusb_get_string_descriptor_ascii(devices[numdev].handle, desc.iManufacturer, descmanu, 256);
                libusb_get_string_descriptor_ascii(devices[numdev].handle, desc.iProduct, descprod, 256);
                libusb_get_string_descriptor_ascii(devices[numdev].handle, desc.iSerialNumber, descseri, 256);

                if (tempers[j].check_product_name) {
                    if (strncmp((const char*)descprod, tempers[j].product_name, strlen(tempers[j].product_name)) == 0) {
                        devices[numdev].type = &tempers[j];
                    }
                    else {
                        // vid and pid match, but product name unmatch
                        libusb_close(devices[numdev].handle);
                        continue; 
                    }
                }
                else {
                    devices[numdev].type = &tempers[j];
                } 

                if (debug) {
                    printf("lvr_winusb with Bus:%03d Addr:%03d VendorID:%04x ProductID:%04x Manufacturer:%s Product:%s Serial:%s found.\n", 
                            bus, addr, desc.idVendor, desc.idProduct, descmanu, descprod, descseri);
                }

                numdev++;
            }
        }
    }

    libusb_free_device_list(devs, 1);

    return numdev;
}

int setup_libusb_access(temper_device_t *devices) {
    int i;
    int numdev;

    libusb_init(&ctx);

    if(debug) {
        libusb_set_debug(ctx, 4); //LIBUSB_LOG_LEVEL_DEBUG
    } else {
        libusb_set_debug(ctx, 0); //LIBUSB_LOG_LEVEL_NONE
    }

    if((numdev = find_lvr_winusb(devices)) < 1) {
        fprintf(stderr, "Couldn't find the USB device, Exiting: %d\n", numdev);
        return -1;
    }

    for (i = 0; i < numdev; i++) {
        usb_detach(devices[i].handle, INTERFACE1);
        usb_detach(devices[i].handle, INTERFACE2);

        if (libusb_set_configuration(devices[i].handle, 0x01) < 0) {
            fprintf(stderr, "Could not set configuration 1\n");
            return -1;
        }

        // Microdia tiene 2 interfaces
        int s;
        if ((s = libusb_claim_interface(devices[i].handle, INTERFACE1)) < 0) {
            fprintf(stderr, "Could not claim interface. Error:%d\n", s);
            return -1;
        }

        if ((s = libusb_claim_interface(devices[i].handle, INTERFACE2)) < 0) {
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

/* decode funcs */
/* Thanks to https://github.com/edorfaus/TEMPered */

void decode_answer_fm75(unsigned char *answer, float *tempd, float *calibration) {
    int buf;

    // temp C internal
    buf = ((signed char)answer[2] << 8) + (answer[3] & 0xFF);
    tempd[0] = buf * (125.0 / 32000.0);
    tempd[0] = tempd[0] * calibration[0] + calibration[1];

    // temp C external
    buf = ((signed char)answer[4] << 8) + (answer[5] & 0xFF);
    tempd[1] = buf * (125.0 / 32000.0);
    tempd[1] = tempd[1] * calibration[0] + calibration[1];
};

void decode_answer_sht1x(unsigned char *answer, float *tempd, float *calibration){
    int buf;

    // temp C
    buf = ((signed char)answer[2] << 8) + (answer[3] && 0xFF);
    tempd[0] = -39.7 + 0.01 * buf;
    tempd[0] = tempd[0] * calibration[0] + calibration[1];

    // relative humidity
    buf = ((signed char)answer[4] << 8) + (answer[5] && 0xFF);
    tempd[1] = -2.0468 + 0.0367 * buf - 1.5955e-6 * buf * buf;
    tempd[1] = ( tempd[0] - 25 ) * ( 0.01 + 0.00008 * tempd[1] ) + tempd[1];
};

int main(int argc, char **argv) {
    temper_device_t *devices;
    int numdev,i;
    unsigned char *answer;
    float tempd[2];
    float calibration[2] = { 1, 0 }; //scale, offset
    char strdate[20];
    int c;
    struct tm *local;
    time_t t;

    while ((c = getopt(argc, argv, "vcfl::a:h")) != -1)
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
                    if (!(sscanf(optarg,"%i",&seconds) == 1)) {
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
            case 'a':
                if (!(sscanf(optarg,"%f:%f", &calibration[0], &calibration[1]) == 2)) {
                    fprintf (stderr, "Error: '%s' is not numeric.\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case '?':
            case 'h':
                printf("pcsensor version %s\n",VERSION);
                printf("    Available options:\n");
                printf("        -h help\n");
                printf("        -v verbose\n");
                printf("        -l[n] loop every 'n' seconds, default value is 5s\n");
                printf("        -a scale:offset set values calibration TempC*scale+offset eg. 1.02:-0.55 \n");
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

    devices = calloc(MAX_DEV, sizeof(temper_device_t*));
    if ((numdev = setup_libusb_access(devices)) < 1) {
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
            control_transfer(devices[i].handle, uTemperature);
            interrupt_read(devices[i].handle, answer);
            devices[i].type->decode_func(answer, tempd, calibration);

            t = time(NULL);
            local = localtime(&t);

            sprintf(strdate, "%04d-%02d-%02dT%02d:%02d:%02d",
                    local->tm_year +1900,
                    local->tm_mon + 1,
                    local->tm_mday,
                    local->tm_hour,
                    local->tm_min,
                    local->tm_sec);

            // print temperature
            if (formato==2) {
                // in Fahrenheit
                printf("%s\t%d\tinternal\t%.2f F\n", strdate, i, (9.0 / 5.0 * tempd[0] + 32.0));
                if (devices[i].type->has_sensor == 2) {
                    printf("%s\t%d\texternal\t%.2f F\n", strdate, i, (9.0 / 5.0 * tempd[1] + 32.0));
                }
            } else {
                // in Celsius
                printf("%s\t%d\tinternal\t%.2f C\n", strdate, i, tempd[0]);
                if (devices[i].type->has_sensor == 2) {
                    printf("%s\t%d\texternal\t%.2f C\n", strdate, i, tempd[1]);
                }
            }

            // print humidity
            if (devices[i].type->has_humid == 1) {
                printf("%s\t%d\thumidity\t%.2f %%\n", strdate, i, tempd[1]);
            }

            if (!bsalir)
                sleep(seconds);
        }
    } while (!bsalir);

    for (i = 0; i < numdev; i++) {
        libusb_release_interface(devices[i].handle, INTERFACE1);
        libusb_release_interface(devices[i].handle, INTERFACE2);

        libusb_close(devices[i].handle);
    }

    return 0;
}

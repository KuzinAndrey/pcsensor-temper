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
#include <time.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <libusb.h>

#define VERSION "1.1.0"

#define VENDOR_ID  0x0c45
#define PRODUCT_ID 0x7401

#define INTERFACE1 0x00
#define INTERFACE2 0x01

const static int reqIntLen=8;
const static int reqBulkLen=8;
const static int endpoint_Int_in=0x82; /* endpoint 0x81 address for IN */
const static int endpoint_Int_out=0x00; /* endpoint 1 address for OUT */
const static int endpoint_Bulk_in=0x82; /* endpoint 0x81 address for IN */
const static int endpoint_Bulk_out=0x00; /* endpoint 1 address for OUT */
const static int timeout=5000; /* timeout in ms */

const static char uTemperatura[] = { 0x01, 0x80, 0x33, 0x01, 0x00, 0x00, 0x00, 0x00 };
const static char uIni1[] = { 0x01, 0x82, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00 };
const static char uIni2[] = { 0x01, 0x86, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00 };

static int bsalir=1;
static int debug=0;
static int seconds=5;
static int formato=0;
static int mrtg=0;
static int calibration=0;


void bad(const char *why) {
    fprintf(stderr,"Fatal error> %s\n",why);
    exit(17);
}

libusb_context *ctx = NULL;
libusb_device_handle *find_lvr_winusb();

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
                printf("Detach failed: %s[%d]\n",
                        strerror(errno), errno);
                printf("Continuing anyway\n");
            }
        }
    } else {
        if(debug) {
            printf("detach successful\n");
        }
    }
}

libusb_device_handle* setup_libusb_access() {
    libusb_device_handle *lvr_winusb;

    libusb_init(&ctx);

    if(debug) {
        libusb_set_debug(ctx, 4); //LIBUSB_LOG_LEVEL_DEBUG
    } else {
        libusb_set_debug(ctx, 0); //LIBUSB_LOG_LEVEL_NONE
    }

    if(!(lvr_winusb = find_lvr_winusb())) {
        fprintf(stderr, "Couldn't find the USB device, Exiting\n");
        return NULL;
    }

    usb_detach(lvr_winusb, INTERFACE1);
    usb_detach(lvr_winusb, INTERFACE2);

    if (libusb_set_configuration(lvr_winusb, 0x01) < 0) {
        fprintf(stderr, "Could not set configuration 1\n");
        return NULL;
    }

    // Microdia tiene 2 interfaces
    int s;
    if ( ( s = libusb_claim_interface(lvr_winusb, INTERFACE1) ) != 0) {
        fprintf(stderr, "Could not claim interface. Error:%d\n", s);
        return NULL;
    }

    if ( ( s = libusb_claim_interface(lvr_winusb, INTERFACE2) ) != 0) {
        fprintf(stderr, "Could not claim interface. Error:%d\n", s);
        return NULL;
    }

    return lvr_winusb;
}



libusb_device_handle *find_lvr_winusb() {
    libusb_device_handle *handle;

    handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!handle) {
        fprintf(stderr, "Could not open USB device\n");
        return NULL;
    }
    return handle;
}


void ini_control_transfer(libusb_device_handle *dev) {
    int r,i;

    char question[] = { 0x01,0x01 };

    r = libusb_control_transfer(dev, 0x21, 0x09, 0x0201, 0x00, (char *) question, 2, timeout);
    if( r < 0 ) {
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

    r = libusb_control_transfer(dev, 0x21, 0x09, 0x0200, 0x01, (char *) question, reqIntLen, timeout);
    if( r < 0 ) {
        perror("USB control write"); bad("USB write failed");
    }

    if(debug) {
        for (i=0;i<reqIntLen; i++) printf("%02x ",question[i]  & 0xFF);
        printf("\n");
    }
}

void interrupt_read(libusb_device_handle *dev) {
    int r,s,i;
    unsigned char answer[reqIntLen];
    bzero(answer, reqIntLen);

    s = libusb_interrupt_transfer(dev, endpoint_Int_in, answer, reqIntLen, &r, timeout);
    if( r != reqIntLen ) {
        fprintf(stderr, "USB read failed: %d\n", s);
        perror("USB interrupt read"); bad("USB read failed");
    }

    if(debug) {
        for (i=0;i<reqIntLen; i++) printf("%02x ",answer[i]  & 0xFF);

        printf("\n");
    }
}

void interrupt_read_temperatura(libusb_device_handle *dev, float *tempInC, float *tempOutC) {
    int r,s,i, temperature;
    unsigned char answer[reqIntLen];
    bzero(answer, reqIntLen);

    s = libusb_interrupt_transfer(dev, endpoint_Int_in, answer, reqIntLen, &r, timeout);
    if( r != reqIntLen ) {
        fprintf(stderr, "USB read failed: %d\n", s);
        perror("USB interrupt read"); bad("USB read failed");
    }

    if(debug) {
        for (i=0;i<reqIntLen; i++) printf("%02x ",answer[i]  & 0xFF);

        printf("\n");
    }

    temperature = (answer[3] & 0xFF) + ((signed char)answer[2] << 8);
    temperature += calibration;
    *tempInC = temperature * (125.0 / 32000.0);

    temperature = (answer[5] & 0xFF) + ((signed char)answer[4] << 8);
    temperature += calibration;
    *tempOutC = temperature * (125.0 / 32000.0);

}

void ex_program(int sig) {
    bsalir=1;

    (void) signal(SIGINT, SIG_DFL);
}

int main( int argc, char **argv) {
    libusb_device_handle *lvr_winusb = NULL;
    float tempInC;
    float tempOutC;
    int c;
    struct tm *local;
    time_t t;

    while ((c = getopt (argc, argv, "mfcvhl::a:")) != -1)
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
            case 'm':
                mrtg=1;
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
            case 'a':
                if (!sscanf(optarg,"%i",&calibration)==1) {
                    fprintf (stderr, "Error: '%s' is not numeric.\n", optarg);
                    exit(EXIT_FAILURE);
                } else {
                    break;
                }
            case '?':
            case 'h':
                printf("pcsensor version %s\n",VERSION);
                printf("      Aviable options:\n");
                printf("          -h help\n");
                printf("          -v verbose\n");
                printf("          -l[n] loop every 'n' seconds, default value is 5s\n");
                printf("          -c output only in Celsius\n");
                printf("          -f output only in Fahrenheit\n");
                printf("          -a[n] increase or decrease temperature in 'n' degrees for device calibration\n");
                printf("          -m output for mrtg integration\n");

                exit(EXIT_FAILURE);
            default:
                if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr,
                            "Unknown option character `\\x%x'.\n",
                            optopt);
                exit(EXIT_FAILURE);
        }

    if (optind < argc) {
        fprintf(stderr, "Non-option ARGV-elements, try -h for help.\n");
        exit(EXIT_FAILURE);
    }

    if ((lvr_winusb = setup_libusb_access()) == NULL) {
        exit(EXIT_FAILURE);
    }

    (void) signal(SIGINT, ex_program);

    ini_control_transfer(lvr_winusb);

    control_transfer(lvr_winusb, uTemperatura );
    interrupt_read(lvr_winusb);

    control_transfer(lvr_winusb, uIni1 );
    interrupt_read(lvr_winusb);

    control_transfer(lvr_winusb, uIni2 );
    interrupt_read(lvr_winusb);
    interrupt_read(lvr_winusb);

    do {
        control_transfer(lvr_winusb, uTemperatura );
        interrupt_read_temperatura(lvr_winusb, &tempInC, &tempOutC);

        t = time(NULL);
        local = localtime(&t);

        if (mrtg) {
            if (formato==2) {
                printf("%.2f\n", (9.0 / 5.0 * tempInC + 32.0));
                printf("%.2f\n", (9.0 / 5.0 * tempOutC + 32.0));
            } else {
                printf("%.2f\n", tempInC);
                printf("%.2f\n", tempOutC);
            }

            printf("%02d:%02d\n",
                    local->tm_hour,
                    local->tm_min);

            printf("pcsensor\n");
        } else {
            printf("%04d/%02d/%02d %02d:%02d:%02d\n",
                    local->tm_year +1900,
                    local->tm_mon + 1,
                    local->tm_mday,
                    local->tm_hour,
                    local->tm_min,
                    local->tm_sec);

            if (formato==2) {
                printf("Temperature (internal) %.2fF\n", (9.0 / 5.0 * tempInC + 32.0));
                printf("Temperature (external) %.2fF\n", (9.0 / 5.0 * tempOutC + 32.0));
            } else if (formato==1) {
                printf("Temperature (internal) %.2fC\n", tempInC);
                printf("Temperature (external) %.2fC\n", tempOutC);
            } else {
                printf("Temperature (internal) %.2fF %.2fC\n", (9.0 / 5.0 * tempInC + 32.0), tempInC);
                printf("Temperature (external) %.2fF %.2fC\n", (9.0 / 5.0 * tempOutC + 32.0), tempOutC);
            }
        }

        if (!bsalir)
            sleep(seconds);
    } while (!bsalir);

    libusb_release_interface(lvr_winusb, INTERFACE1);
    libusb_release_interface(lvr_winusb, INTERFACE2);

    libusb_close(lvr_winusb);

    return 0;
}

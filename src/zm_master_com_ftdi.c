#include <ftdi.h>
#include <stdio.h>
#include <string.h>

#include "zm_master.h"
#include "zm_master_com.h"

#define MAX_FAIL_TRANSMISSIONS 5
#define MAX_RECONNECT_RETRIES  3

struct _ftdi_controler
{
    unsigned int initialized : 1;
    struct ftdi_context ftdic;
    unsigned char rxbuf[1];
    int vendor;
    int product;
    char serial[32];
    unsigned int fail_thres;
    unsigned int reconnects;
};

static struct _ftdi_controler ctrl;

static char zm_master_com_getc(void);
static char zm_master_com_iskey(void);
static short zm_master_com_putc(char c);
static void zm_master_com_reconnect(void);

static void zm_master_com_reconnect(void)
{
    if (ctrl.initialized)
    {
        zm_master_com_exit();
    }

    if (ctrl.reconnects < MAX_RECONNECT_RETRIES)
    {
        ctrl.fail_thres = 0;
        if ((ctrl.vendor != 0) && (ctrl.product != 0))
        {
            zm_master_com_init_ftdi(ctrl.vendor, ctrl.product, ctrl.serial);
        }
    }
    else // reset
    {
        ctrl.reconnects = 0;
        ctrl.vendor = 0;
        ctrl.product = 0;
        ctrl.serial[0] = '\0';
    }
}

void zm_master_com_init_ftdi(int vendor, int product, const char *serial)
{
    int ret;

    if (ctrl.initialized)
    {
        zm_master_com_exit();
    }

    ctrl.initialized = 0;
    ctrl.fail_thres = 0;
    ctrl.reconnects++;
    ctrl.vendor = 0;
    ctrl.product = 0;
    ctrl.serial[0] = '\0';

    if (ftdi_init(&ctrl.ftdic) < 0)
    {
        fprintf(stderr, "COM: error to to open usb ftdi device");
    }
    else
    {
        ret = ftdi_usb_open_desc(&ctrl.ftdic, vendor, product, NULL, serial);
        if (ret == 0)
        {
            ftdi_set_baudrate(&ctrl.ftdic, 38400);
            ftdi_set_line_property(&ctrl.ftdic, BITS_8, STOP_BIT_1, NONE);
            zm_master_init(zm_master_com_getc, zm_master_com_iskey, zm_master_com_putc);
            ctrl.initialized = 1;
            ctrl.vendor = vendor;
            ctrl.product = product;
            if (serial)
            {
                strncpy(ctrl.serial, serial, sizeof(ctrl.serial));
            }
            else
            {
                ctrl.serial[0] = '\0';
            }
        }
        else
        {
            fprintf(stderr, "COM: %s", ftdi_get_error_string(&ctrl.ftdic));
        }
    }
}

void zm_master_com_exit(void)
{
    if (ctrl.initialized == 1)
    {
        zm_master_exit();
        ftdi_usb_close(&ctrl.ftdic);
        ftdi_deinit(&ctrl.ftdic);
        ctrl.initialized = 0;
    }
}

int zm_master_com_check(void)
{
    if (ctrl.initialized)
    {
        return ZM_MASTER_COM_CONNECTED;
    }

    return ZM_MASTER_COM_DISCONNTED;
}

static char zm_master_com_getc(void)
{
    return (char)ctrl.rxbuf[0];
}

static char zm_master_com_iskey(void)
{
    int ret = ftdi_read_data(&ctrl.ftdic, &ctrl.rxbuf[0], 1);

    if (ret == 1)
    {
//        printf("%X", rxbuf[0]);
        return 1;
    }
    else if (ret == 0) // no data available
    {
        ctrl.rxbuf[0] = 0;
    }
    else
    {
        ctrl.rxbuf[0] = 0;
        ctrl.fail_thres++;

        if (ctrl.fail_thres > MAX_FAIL_TRANSMISSIONS)
        {
            zm_master_com_reconnect();
        }
    }

    //fprintf(stderr, "COM: %s", ftdi_get_error_string(&ftdic));
    return 0;
}

static short zm_master_com_putc(char c)
{
    int ret = ftdi_write_data(&ctrl.ftdic, (unsigned char*)&c, 1);

    if (ret < 0)
    {
        fprintf(stderr, "COM: %s", ftdi_get_error_string(&ctrl.ftdic));
        ctrl.fail_thres++;

        if (ctrl.fail_thres > MAX_FAIL_TRANSMISSIONS)
        {
            zm_master_com_reconnect();
        }
    }

    if (ret == 1)
    {
        return 1;
    }

    return 0;
}

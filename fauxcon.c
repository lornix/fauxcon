#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <linux/uinput.h>
#include <error.h>
#include <errno.h>

void send_event(int ufile,int type,int code,int value)
{
    // send the provided event
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);
    event.type = type;
    event.code = code;
    event.value = value;
    write(ufile, &event, sizeof(event));
    /* usleep(10); */
    // synch
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);
    event.type = EV_SYN;
    event.code = SYN_REPORT;
    event.value = 0;
    write(ufile, &event, sizeof(event));
    /* usleep(10); */
}

void sendkey(int ufile,int val1)
{
#define SHIFT 0x400
    static short keycode[]=
    {
        /*00*/ KEY_RESERVED,0,0,0,0,0,0,0,
        /*08*/ KEY_BACKSPACE,KEY_TAB,0,0,0,KEY_ENTER,0,0,
        /*10*/ 0,0,0,0,0,0,0,0,
        /*18*/ 0,0,0,KEY_ESC,0,0,0,0,
        /*20  !"#$%&' */ KEY_SPACE,KEY_1|SHIFT,KEY_APOSTROPHE|SHIFT,KEY_3|SHIFT,KEY_4|SHIFT,KEY_5|SHIFT,KEY_7|SHIFT,KEY_APOSTROPHE,
        /*28 ()*+,-./ */ KEY_9|SHIFT,KEY_0|SHIFT,KEY_8|SHIFT,KEY_EQUAL|SHIFT,KEY_COMMA,KEY_MINUS,KEY_DOT,KEY_SLASH,
        /*30 01234567 */ KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,
        /*38 89:;<=>? */ KEY_8,KEY_9,KEY_SEMICOLON|SHIFT,KEY_SEMICOLON,KEY_COMMA|SHIFT,KEY_EQUAL,KEY_DOT|SHIFT,KEY_SLASH|SHIFT,
        /*40 @ABCDEFG */ KEY_2|SHIFT,KEY_A|SHIFT,KEY_B|SHIFT,KEY_C|SHIFT,KEY_D|SHIFT,KEY_E|SHIFT,KEY_F|SHIFT,KEY_G|SHIFT,
        /*48 HIJKLMNO */ KEY_H|SHIFT,KEY_I|SHIFT,KEY_J|SHIFT,KEY_K|SHIFT,KEY_L|SHIFT,KEY_M|SHIFT,KEY_N|SHIFT,KEY_O|SHIFT,
        /*50 PQRSTUVW */ KEY_P|SHIFT,KEY_Q|SHIFT,KEY_R|SHIFT,KEY_S|SHIFT,KEY_T|SHIFT,KEY_U|SHIFT,KEY_V|SHIFT,KEY_W|SHIFT,
        /*58 XYZ[\]^_ */ KEY_X|SHIFT,KEY_Y|SHIFT,KEY_Z|SHIFT,KEY_LEFTBRACE,KEY_BACKSLASH,KEY_RIGHTBRACE,KEY_6|SHIFT,KEY_MINUS|SHIFT,
        /*60 `abcdefg */ KEY_GRAVE,KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,
        /*68 hijklmno */ KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,
        /*70 pqrstuvw */ KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,
        /*78 xyz{|}~  */ KEY_X,KEY_Y,KEY_Z,KEY_LEFTBRACE|SHIFT,KEY_BACKSLASH|SHIFT,KEY_RIGHTBRACE|SHIFT,KEY_GRAVE|SHIFT,KEY_UNKNOWN
    };
    int shifted=keycode[val1]|SHIFT;
    int key=keycode[val1]&(~SHIFT);
    // if SHIFT, press SHIFT
    if (shifted) {
        send_event(ufile,EV_KEY,KEY_LEFTSHIFT,1);
    }
    // press key
    send_event(ufile,EV_KEY,key,1);
    // release key
    send_event(ufile,EV_KEY,key,0);
    // now unpress it
    if (shifted) {
        send_event(ufile,EV_KEY,KEY_LEFTSHIFT,0);
    }
}

int create_uinput()
{
    /* Attempt to open uinput to create new device */
    int ufile = open("/dev/uinput", O_WRONLY | O_NDELAY );
    if (ufile<0) {
        error(1,errno,"Could not open uinput. (Permissions?)");
    }

    /* structure with name and other info */
    struct uinput_user_dev uinp;
    memset(&uinp, 0, sizeof(uinp));
    strncpy(uinp.name, "Faux Keyboard [TODO: & Mouse]", UINPUT_MAX_NAME_SIZE-1);
    uinp.id.bustype = BUS_USB;
    uinp.id.vendor  = 0x0d0d;
    uinp.id.product = 0x0d0d;
    uinp.id.version = 13;

    /* we handle EV_KEY & EV_SYN events */
    ioctl(ufile, UI_SET_EVBIT, EV_KEY);
    ioctl(ufile, UI_SET_EVBIT, EV_SYN);

    /* enable handling of all keys */
    for (int i=0; i<256; i++) {
        ioctl(ufile, UI_SET_KEYBIT, i);
    }

    /* write data out to prepare for the magic */
    ssize_t res=write(ufile, &uinp, sizeof(uinp));
    if (res!=sizeof(uinp)) {
        close(ufile);
        error(2,errno,"Write error: %ld != %ld",res,sizeof(uinp));
    }
    /* magic happens here, honest */
    int retcode = ioctl(ufile, UI_DEV_CREATE);
    if (retcode!=0) {
        close(ufile);
        error(2,errno,"Ioctl error: %d", retcode);
    }
    /* ufile is our link to uinput device */
    return ufile;
}

void destroy_uinput(int ufile)
{
    /* skip checking retval, we don't care */
    ioctl(ufile, UI_DEV_DESTROY);
    close(ufile);
}

int main(void)
{
    /* set up uinput device */
    int ufile=create_uinput();

    sendkey(ufile,'#');
    for (int cnt=32; cnt<=127; cnt++) {
        sendkey(ufile,cnt);
    }
    // press key
    send_event(ufile,EV_KEY,KEY_ENTER,1);
    // release key
    send_event(ufile,EV_KEY,KEY_ENTER,0);
    // press key
    send_event(ufile,EV_KEY,KEY_ENTER,1);
    // release key
    send_event(ufile,EV_KEY,KEY_ENTER,0);

    /* remove everything */
    destroy_uinput(ufile);

    return 0;
}

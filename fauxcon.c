#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/time.h>

#define SHIFT 0x400

struct input_event event;

void send_event(int ufile,int type,int code,int value)
{
    // send the provided event
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);
    event.type = type;
    event.code = code;
    event.value = value;
    write(ufile, &event, sizeof(event));
    usleep(1000);
    // synch
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, NULL);
    event.type = EV_SYN;
    event.code = SYN_REPORT;
    event.value = 0;
    write(ufile, &event, sizeof(event));
    usleep(9000);
}


void sendkey(int ufile,int val1)
{
    short int keycode[]=
    {
        /*00*/ KEY_RESERVED,0,0,0,0,0,0,0,
        /*08*/ KEY_BACKSPACE,KEY_TAB,0,0,0,KEY_ENTER,0,0,
        /*10*/ 0,0,0,0,0,0,0,0,
        /*18*/ 0,0,0,KEY_ESC,0,0,0,0,
        /*20*/ KEY_SPACE,KEY_1|SHIFT,KEY_APOSTROPHE|SHIFT,KEY_3|SHIFT,KEY_4|SHIFT,KEY_5|SHIFT,KEY_7|SHIFT,KEY_APOSTROPHE,
        /*28*/ KEY_9|SHIFT,KEY_0|SHIFT,KEY_8|SHIFT,KEY_EQUAL|SHIFT,KEY_COMMA,KEY_MINUS,KEY_DOT,KEY_SLASH,
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
    int shift=keycode[val1]|SHIFT;
    int key=keycode[val1]&(SHIFT-1);
    // if SHIFT, press SHIFT
    if (shift) {
        send_event(ufile,EV_KEY,KEY_LEFTSHIFT,1);
    }
    // press key
    send_event(ufile,EV_KEY,key,1);
    // release key
    send_event(ufile,EV_KEY,key,0);
    // now unpress it
    if (shift) {
        send_event(ufile,EV_KEY,KEY_LEFTSHIFT,0);
    }
}

void movemouse(int ufile,int count,int evcode,int evvalue)
{ int j;
    for (j=0; j<count; j++) {
        // move pointer
        send_event(ufile,EV_REL,evcode,evvalue);
    }
}

int create_uinput()
{
    int retcode, i;
    int ufile;
    struct uinput_user_dev uinp;

    ufile = open("/dev/uinput", O_WRONLY | O_NDELAY );
    if (ufile == 0) {
        printf("Could not open uinput.\n");
        exit(1);
    }

    memset(&uinp, 0, sizeof(uinp));
    strncpy(uinp.name, "simulated mouse", 20);
    uinp.id.version = 4;
    uinp.id.bustype = BUS_USB;

    ioctl(ufile, UI_SET_EVBIT, EV_KEY);
    ioctl(ufile, UI_SET_EVBIT, EV_REL);
    ioctl(ufile, UI_SET_RELBIT, REL_X);
    ioctl(ufile, UI_SET_RELBIT, REL_Y);

    for (i=0; i<256; i++) {
        ioctl(ufile, UI_SET_KEYBIT, i);
    }

    ioctl(ufile, UI_SET_KEYBIT, BTN_MOUSE);
    ioctl(ufile, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(ufile, UI_SET_KEYBIT, BTN_RIGHT);

    // create input device in input subsystem
    retcode = write(ufile, &uinp, sizeof(uinp));
    retcode = (ioctl(ufile, UI_DEV_CREATE));
    if (retcode) {
        close(ufile);
        printf("Error create uinput device %d.\n", retcode);
        exit(2);
    }
    send_event(ufile,EV_SYN,SYN_REPORT,0);
    return ufile;
}

void destroy_uinput(int ufile)
{
    // destroy the device
    ioctl(ufile, UI_DEV_DESTROY);
    close(ufile);
}

int main(void)
{
    int ufile=create_uinput();

    sleep(1);
    sendkey(ufile,'#');
    sendkey(ufile,' ');
    for (int cnt=' '; cnt<=127; cnt++)
    {
        sendkey(ufile,cnt);
    }
    sendkey(ufile,13);

    destroy_uinput(ufile);

    return 0;
}

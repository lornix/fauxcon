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
#include <assert.h>
#include <termios.h>
#include <sys/kd.h>
#include <sys/ioctl.h>

/*
 * not needed, since <linux/uinput.h> includes it already
 * but makes it easy to 'gf' it to view in vim
 * #include <linux/input.h>
 */

typedef enum { KBD_MODE_RAW, KBD_MODE_NORMAL } kbd_mode;

void set_keyboard(kbd_mode kmode)
{
    /* storage for old keyboard mode */
    static struct termios tty_attr_saved;
    static int keyboard_mode_saved;


    if (kmode==KBD_MODE_NORMAL) {

        /* restore tty attributes */
        tcsetattr(0, TCSAFLUSH, &tty_attr_saved);
        /* restore keyboard mode */
        ioctl(0, KDSKBMODE, keyboard_mode_saved);

    } else if (kmode==KBD_MODE_RAW) {

        /* save keyboard mode */
        ioctl(0, KDGKBMODE, &keyboard_mode_saved);

        /* save tty attributes */
        tcgetattr(0, &tty_attr_saved);

        /* make stdin non-blocking */
        int flags=fcntl(0, F_GETFL);
        flags|=O_NONBLOCK;
        fcntl(0, F_SETFL, flags);

        /* reset tty attributes */
        struct termios tty_attr;
        memcpy(&tty_attr, &tty_attr_saved, sizeof(tty_attr));
        tty_attr.c_lflag&=~(ICANON|ECHO|ISIG);
        tty_attr.c_iflag&=~(ISTRIP|INLCR|ICRNL|IGNCR|IXON|IXOFF);
        tcsetattr(0, TCSANOW, &tty_attr);
        /* set keyboard to raw mode */
        ioctl(0, KDSKBMODE, K_RAW);
    }
}

void send_event(int ufile, int type, int code, int value)
{
    /* build structure and populate */
    struct input_event event;

    gettimeofday(&event.time, NULL);
    event.type  = type;
    event.code  = code;
    event.value = value;

    /* send the event */
    ssize_t result=write(ufile, &event, sizeof(event));
    if (result!=sizeof(event)) {
        error(1, errno, "Error during event write");
    }
    // sync, reuse previous event structure
    event.type  = EV_SYN;
    event.code  = SYN_REPORT;
    event.value = 0;
    result=write(ufile, &event, sizeof(event));
    if (result!=sizeof(event)) {
        error(1, errno, "Error during event sync");
    }
}

void sendchar(int ufile, int val1)
{
#define USHIFT 0x1000
#define UCTRL  0x2000
    static short keycode[]=
    {
        /*00 @ABCDEFG */ KEY_2|USHIFT|UCTRL, KEY_A|UCTRL, KEY_B|UCTRL, KEY_C|UCTRL, KEY_D|UCTRL, KEY_E|UCTRL, KEY_F|UCTRL, KEY_G|UCTRL,
        /*08 HIJKLMNO */ KEY_H|UCTRL, KEY_I|UCTRL, KEY_J|UCTRL, KEY_K|UCTRL, KEY_L|UCTRL, KEY_M|UCTRL, KEY_N|UCTRL, KEY_O|UCTRL,
        /*10 PQRSTUVW */ KEY_P|UCTRL, KEY_Q|UCTRL, KEY_R|UCTRL, KEY_S|UCTRL, KEY_T|UCTRL, KEY_U|UCTRL, KEY_V|UCTRL, KEY_W|UCTRL,
        /*18 XYZ..... */ KEY_X|UCTRL, KEY_Y|UCTRL, KEY_Z|UCTRL, KEY_ESC, 0, 0, 0, 0,
        /*20  !"#$%&' */ KEY_SPACE, KEY_1|USHIFT, KEY_APOSTROPHE|USHIFT, KEY_3|USHIFT, KEY_4|USHIFT, KEY_5|USHIFT, KEY_7|USHIFT, KEY_APOSTROPHE,
        /*28 ()*+,-./ */ KEY_9|USHIFT, KEY_0|USHIFT, KEY_8|USHIFT, KEY_EQUAL|USHIFT, KEY_COMMA, KEY_MINUS, KEY_DOT, KEY_SLASH,
        /*30 01234567 */ KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
        /*38 89:;<=>? */ KEY_8, KEY_9, KEY_SEMICOLON|USHIFT, KEY_SEMICOLON, KEY_COMMA|USHIFT, KEY_EQUAL, KEY_DOT|USHIFT, KEY_SLASH|USHIFT,
        /*40 @ABCDEFG */ KEY_2|USHIFT, KEY_A|USHIFT, KEY_B|USHIFT, KEY_C|USHIFT, KEY_D|USHIFT, KEY_E|USHIFT, KEY_F|USHIFT, KEY_G|USHIFT,
        /*48 HIJKLMNO */ KEY_H|USHIFT, KEY_I|USHIFT, KEY_J|USHIFT, KEY_K|USHIFT, KEY_L|USHIFT, KEY_M|USHIFT, KEY_N|USHIFT, KEY_O|USHIFT,
        /*50 PQRSTUVW */ KEY_P|USHIFT, KEY_Q|USHIFT, KEY_R|USHIFT, KEY_S|USHIFT, KEY_T|USHIFT, KEY_U|USHIFT, KEY_V|USHIFT, KEY_W|USHIFT,
        /*58 XYZ[\]^_ */ KEY_X|USHIFT, KEY_Y|USHIFT, KEY_Z|USHIFT, KEY_LEFTBRACE, KEY_BACKSLASH, KEY_RIGHTBRACE, KEY_6|USHIFT, KEY_MINUS|USHIFT,
        /*60 `abcdefg */ KEY_GRAVE, KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
        /*68 hijklmno */ KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
        /*70 pqrstuvw */ KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W,
        /*78 xyz{|}~  */ KEY_X, KEY_Y, KEY_Z, KEY_LEFTBRACE|USHIFT, KEY_BACKSLASH|USHIFT, KEY_RIGHTBRACE|USHIFT, KEY_GRAVE|USHIFT, KEY_BACKSPACE
    };

    /* verify alignment of array */
    assert(keycode[' ']==(KEY_SPACE));
    assert(keycode['A']==(KEY_A|USHIFT));
    assert(keycode['a']==(KEY_A));
    assert(keycode['~']==(KEY_GRAVE|USHIFT));
    assert(sizeof(keycode)==256);

    int need_shift=keycode[val1]&USHIFT;
    int need_ctrl=keycode[val1]&UCTRL;
    int key=keycode[val1]&(0xfff);

    // if modifier needed, hold it down
    if (need_ctrl) {
        send_event(ufile, EV_KEY, KEY_LEFTCTRL, 1);
    }
    if (need_shift) {
        send_event(ufile, EV_KEY, KEY_LEFTSHIFT, 1);
    }
    // press key
    send_event(ufile, EV_KEY, key, 1);
    // release key
    send_event(ufile, EV_KEY, key, 0);
    // now release the modifiers
    if (need_shift) {
        send_event(ufile, EV_KEY, KEY_LEFTSHIFT, 0);
    }
    if (need_ctrl) {
        send_event(ufile, EV_KEY, KEY_LEFTCTRL, 0);
    }
}

int create_uinput()
{
    /* Attempt to open uinput to create new device */
    int ufile = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufile<0) {
        error(1, errno, "Could not open uinput device");
    }

    /* structure with name and other info */
    struct uinput_user_dev uinp;
    memset(&uinp, 0, sizeof(uinp));
    strncpy(uinp.name, "Faux Keyboard [TODO: & Mouse]", UINPUT_MAX_NAME_SIZE-1);
    uinp.id.bustype = BUS_USB;
    /* made up values, but didn't find anything using these values */
    uinp.id.vendor  = 0x9642;
    uinp.id.product = 0x000d;
    uinp.id.version = 13;

    /* we handle EV_SVN, EV_KEY & EV_REP events */
    ioctl(ufile, UI_SET_EVBIT, EV_SYN);
    ioctl(ufile, UI_SET_EVBIT, EV_KEY);
    ioctl(ufile, UI_SET_EVBIT, EV_REP);

    /* enable handling of all keys */
    for (int i=0; i<256; i++) {
        ioctl(ufile, UI_SET_KEYBIT, i);
    }

    /* write data out to prepare for the magic */
    ssize_t res=write(ufile, &uinp, sizeof(uinp));
    if (res!=sizeof(uinp)) {
        close(ufile);
        error(2, errno, "Write error: %d != %d", (signed int)res, (signed int)sizeof(uinp));
    }
    /* magic happens here, honest */
    int retcode = ioctl(ufile, UI_DEV_CREATE);
    if (retcode!=0) {
        close(ufile);
        error(2, errno, "Ioctl error: %d", retcode);
    }
    /* ufile is our link to uinput device */
    return ufile;
}

void destroy_uinput(int ufile)
{
    /* skip checking retval, not concerned */
    ioctl(ufile, UI_DEV_DESTROY);
    close(ufile);
}

int main(void)
{
    /* set up uinput device */
    int ufile=create_uinput();

    /* set input to nonblocking/raw mode */
    set_keyboard(KBD_MODE_RAW);

    /* specify valid escape string <CR>%. */
    const char* escape_string="\015%.";

    /* hold last 3 previous keystrokes
     * (plus trailing zero to cmp with escape_string)
     */
    char prevchr[4]={0, 0, 0, 0};
    int chr;

    while (1) {
        chr=getchar();
        if (chr==EOF) {
            continue;
        }
        sendchar(ufile, chr);

        prevchr[0]=prevchr[1];
        prevchr[1]=prevchr[2];
        prevchr[2]=chr;

        if (memcmp(escape_string, prevchr, 3)==0) {
            break;
        }
        int pchr=(chr<32)?32:chr;
        printf("chr=%3d '%c'\n",chr,pchr);

    }

    /* set input to 'normal' mode */
    set_keyboard(KBD_MODE_NORMAL);

    /* remove everything */
    destroy_uinput(ufile);

    return 0;
}

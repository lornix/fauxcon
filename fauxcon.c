/*
 * fauxcon - virtual console connection for keyboard and mouse
 *
 * A simple utility to allow connecting to the CONSOLE keyboard and
 * mouse (TODO: add mouse passthrough)
 *
 * In its simplest mode, it takes raw ASCII from keyboard and converts this
 * to the appropriate keyboard scancodes, stuffed into keyboard queue of
 * current system.  When you're logged in via SSH, this feeds keys into the
 * input queue.  Very handy.
 *
 * ------IDEAS-------
 * TODO: (-m) mouse support + option to enable (not enabled by default)
 *            This likely only possible with (-r) remote mode. Involves
 *            grabbing events for mouse/kb to pass along.
 * TODO: (-r) remote mode. Like how rsync does it, connect to remote system,
 *            talk to itself on that machine, connect and begin passing
 *            kb/mouse events.
 * TODO: (-f/-s) Allow multiple file/string send options, send in order
 *
 * <lornix@lornix.com> 2014-06-15
 *
 */

#ifndef VERSION
#define VERSION "--dev--"
#endif

#define AUTHOR "L Nix <lornix@lornix.com>"

#define _BSD_SOURCE
#define _GNU_SOURCE

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
#include <sys/select.h>
#include <getopt.h>

/* #include <linux/input.h>                               */
/* not needed, since <linux/uinput.h> includes it already */
/* but makes it easy to 'gf' it to view in Vim            */

/* GLOBALS!                                                               */
/* Argh! Dislike global variables, but there are a few values we need to  */
/* pass around. Could put in a structure, but we'd still need to pass it. */
/* No win either way.  Globals makes things cleaner at least.             */

/* what is the default escape character? */
#define ESCAPE_CHAR_DEFAULT '%'

/* create stringified version of macro*/
#define Q(x) QQ(x)
#define QQ(x) #x

/* REQ - denotes a non-optional argument in option list */
#define REQ 0x100

/* version string shown in multiple places. (consistency!) */
#define VERSION_STRING "%s " VERSION " - " AUTHOR "\n",arg0

/* arbitrary line length limit, prevents wrap on typical display */
static const int MAX_LINE_LENGTH=70;

/* Maximum rdelay/cdelay value, in milliseconds */
static const int MAX_DELAY=2000;

/* escape_char - what character is the escape char? Can't leave without it! */
static char escape_char=ESCAPE_CHAR_DEFAULT;
static int verbose_mode=0;
static int rdelay=0;
static int cdelay=0;

/* file descriptor to write to uinput device */
static int ufile=0;

/* a nice enum to document what mode we want KB to end up */
typedef enum { KBD_MODE_RAW, KBD_MODE_NORMAL } kbd_mode;

/* perhaps a better way to build getopts/getopts_long structure ONCE */
typedef struct {
    const int shortchar;
    const char* longname;
    int has_arg;
    const char* description;
} progoptions;

/* need to press SHIFT for this key */
#define USHIFT 0x1000
/* need to press CTRL for this key */
#define UCTRL  0x2000
/* 1:1 lookup table.  128 entries, ASC('A')=65=KEY_A|USHIFT */
static const short keycode[]=
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

/* Do processing to set KB to raw or cooked mode. */
/* Saves old state to restore later.              */
static void set_keyboard(kbd_mode kmode)
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

/* send an event to uinput */
static void send_event(int type, int code, int value)
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

    /* send sync event, reusing previous event structure */
    gettimeofday(&event.time, NULL);
    event.type  = EV_SYN;
    event.code  = SYN_REPORT;
    event.value = 0;

    result=write(ufile, &event, sizeof(event));
    if (result!=sizeof(event)) {
        error(1, errno, "Error during event sync");
    }
}

/* convert an ASCII character given into a useful scancode for uinput */
static void sendchar(int any_key)
{
    /* parse key, grabbing SHIFT & CTRL requirements */
    int need_shift=keycode[any_key]&USHIFT;
    int need_ctrl=keycode[any_key]&UCTRL;
    int key=keycode[any_key]&(0xfff);

    /* if modifier needed, hold it down */
    if (need_ctrl) {
        send_event(EV_KEY, KEY_LEFTCTRL, 1);
    }
    if (need_shift) {
        send_event(EV_KEY, KEY_LEFTSHIFT, 1);
    }

    /* press key */
    send_event(EV_KEY, key, 1);

    /* release key */
    send_event(EV_KEY, key, 0);

    /* now release the modifiers */
    if (need_shift) {
        send_event(EV_KEY, KEY_LEFTSHIFT, 0);
    }
    if (need_ctrl) {
        send_event(EV_KEY, KEY_LEFTCTRL, 0);
    }
    /* did we send a carriage return? (or linefeed?) */
    if ((any_key==10)||(any_key==13)) {
        /* rdelay overrides cdelay if present */
        if (rdelay>0) {
            usleep(rdelay*1000);
        } else if (cdelay>0) {
            usleep(cdelay*1000);
        }
    } else {
        /* any other character, delay if specified */
        if (cdelay>0) {
            usleep(cdelay*1000);
        }
    }
}

/* perform initial setup to create uinput device */
static void create_uinput()
{
    /* Attempt to open uinput to create new device */
    ufile = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
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
    uinp.id.product = 0x0d0d;
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
        error(2, errno, "Write error: %d (actual) != %d (expected)", (signed int)res, (signed int)sizeof(uinp));
        /* does not return */
    }

    /* magic happens here, honest */
    int retcode = ioctl(ufile, UI_DEV_CREATE);
    if (retcode!=0) {
        close(ufile);
        error(2, errno, "Ioctl error: %d", retcode);
        /* does not return */
    }
}

/* tear down uinput device and close file descriptor */
static void destroy_uinput(void)
{
    /* skip checking retval, not concerned */
    ioctl(ufile, UI_DEV_DESTROY);
    close(ufile);
    /* mark it invalid */
    ufile=-1;
}

static void connect_user(void)
{
    /* set up uinput device */
    create_uinput();

    /* set input to nonblocking/raw mode */
    set_keyboard(KBD_MODE_RAW);

    /* state machine to find escape sequence */
    int escape_sequence_state=0;

    /* build fd_set for select */
    fd_set readfds;
    FD_ZERO(&readfds);
    /* listen for stdin */
    FD_SET(0,&readfds);

    while (1) {
        int sel=select(1,&readfds,NULL,NULL,NULL);
        if (sel<0) {
            /* something bad happened */
            perror("Error during select");
            break;
        }
        if (sel==0) {
            /* nobody available, try again */
            /* listen for stdin again, since it was reset */
            FD_SET(0,&readfds);
            continue;
        }
        /* supposed to be a character ready */
        int chr=getchar();
        /* shouldn't happen... but... */
        if (chr==EOF) {
            continue;
        }
        /* send typed character to uinput device */
        sendchar(chr);

        /* state machine to handle escape code */
        switch (escape_sequence_state) {
            case 2: /* 2 = looking for period */
                escape_sequence_state=(chr=='.')?3:0;
                break;
            case 1: /* 1 = looking for escape_char */
                escape_sequence_state=(chr==escape_char)?2:0;
                break;
            default: /* 0 = looking for CR */
                escape_sequence_state=(chr==13)?1:0;
                break;
        }
        if (escape_sequence_state==3) {
            break;
        }
    }

    /* set input to 'normal' mode */
    set_keyboard(KBD_MODE_NORMAL);

    /* remove everything */
    destroy_uinput();
}

static const char* showopt(int shortchar, const char* longname)
{
    /* sigh.  Of course, someone will create a longname option over 80 chars
     * long... someday...
     */
#define SHOWOPTSTRLEN 80

    static char str[SHOWOPTSTRLEN+1];
    /* always start with empty string */
    str[0]=0;

    if (shortchar!=0) {
        /* fake it */
        strncat(str,"-x",SHOWOPTSTRLEN);
        /* and poke in proper value */
        str[strlen(str)-1]=shortchar&(~REQ);
    }
    if ((shortchar!=0)&&(longname!=0)) {
        strncat(str,"|",SHOWOPTSTRLEN);
    }
    if (longname!=0) {
        strncat(str,"--",SHOWOPTSTRLEN);
        strncat(str,longname,SHOWOPTSTRLEN);
    }
    /* statically allocated, will be overwritten each call */
    return str;
}

static const char* showarg(int has_arg)
{
#define SHOWARGSTRLEN 6

    static char str[SHOWARGSTRLEN+1]={0};
    /* always start with empty string */
    str[0]=0;

    if (has_arg>0) {
        strncat(str," ",SHOWARGSTRLEN);

        /* optional argument? has_arg==2 */
        if (has_arg>1) {
            strncat(str,"[",SHOWARGSTRLEN);
        }

        strncat(str,"arg",SHOWARGSTRLEN);

        if (has_arg>1) {
            strncat(str,"]",SHOWARGSTRLEN);
        }
    }
    /* statically allocated, will be overwritten each call */
    return str;
}

static void usage(char* arg0)
{
    printf("\n");

    progoptions poptions[]=
    {
        /* short,   long,      has_arg, description */
        {  'h',     "help",    0,       "Show Help" },
        {  'v',     "verbose", 0,       "Verbose operation" },
        {  'V',     "version", 0,       "Show version information" },
        {  'r',     "rdelay",  1,       "Delay arg (ms) after every <RETURN> character" },
        {  'c',     "cdelay",  1,       "Delay arg (ms) after every character" },
        {  'f',     "file",    1,       "Send contents of file 'arg' (and exit)" },
        {  's',     "string",  1,       "Send string 'arg' (and exit)" },
        {  'S',     "stay",    0,       "Stay connected after sending file or string" },
        {  'e',     "escape",  1,       "Specify Escape Character - Default (" Q(ESCAPE_CHAR_DEFAULT) ")" },
        {  'C'|REQ, "connect", 0,       "Connect to CONSOLE keyboard & mouse (REQUIRED)" },
        {   0,0,0, /* compiler will concatenate these all together */
            "Connect your keyboard to system's CONSOLE KB & Mouse.\n\n"
                "The required '-C/--connect' option is to prevent users from getting locked in\n"
                "without knowing how to exit.  You must always include this option to connect.\n\n"
                "To exit once running, you'll need to type the escape sequence (much like ssh(1)),\n"
                "by entering '<RETURN> % .', that is, the RETURN key, whatever your escape\n"
                "character is (default is " Q(ESCAPE_CHAR_DEFAULT) "), and then a period ('.').\n"
        },
    };

    int line_len=printf("%s",arg0);

    int max_opt_len=0;

    /* show initial line/layout for options with command name */
    int index=0;
    while ((poptions[index].shortchar!=0)||(poptions[index].longname!=0)) {
        int opt_len=0;
        int len=0;

        line_len+=printf(" ");

        /* optional option? -or- not required... */
        if ((poptions[index].shortchar&REQ)==0) {
            line_len+=printf("[");
        }

        /* show the formatted option pair -x/--xxxx */
        len=printf("%s",showopt(poptions[index].shortchar&(~REQ),poptions[index].longname));
        line_len+=len;
        opt_len+=len;

        /* does this option have an argument? */
        if (poptions[index].has_arg>0) {
            /* show argument, optionally marked as optional */
            len=printf("%s",showarg(poptions[index].has_arg));
            line_len+=len;
            opt_len+=len;
            line_len+=printf(" ");
        }

        if ((poptions[index].shortchar&REQ)==0) {
            line_len+=printf("]");
        }

        /* arbitrary line length limit, prevents wrap on typical display */
        if (line_len>=MAX_LINE_LENGTH) {
            /* retrieve length of arg0 */
            line_len=strlen(arg0);
            /* pad out to that length */
            printf("\n%*s",line_len,"");
        }

        if (opt_len>max_opt_len) {
            max_opt_len=opt_len;
        }

        index++;
    }
    printf("\n\n");

    /* display the long description stored in the tail of the struct */
    printf("%s\n",poptions[index].description);

    /* now show options with descriptions */
    index=0;
    while ((poptions[index].shortchar!=0)||(poptions[index].longname!=0)) {
        /* initial padding on line */
        printf("  ");

        int len=printf("%s",showopt(poptions[index].shortchar&(~REQ),poptions[index].longname));

        if (poptions[index].has_arg>0) {
            len+=printf("%s",showarg(poptions[index].has_arg));
        }

        /* figure out current width */
        len=2+max_opt_len-len;

        /* and pad to make columns line up */
        printf("%*s",len,"");
        printf("%s\n",poptions[index].description);

        index++;
    }
    printf("\n");
    printf(VERSION_STRING);
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    /* verify alignment of keycode array                           */
    /* just a spot check to make sure everything lines up properly */
    assert(keycode[' ']==(KEY_SPACE));
    assert(keycode['A']==(KEY_A|USHIFT));
    assert(keycode['a']==(KEY_A));
    assert(keycode['~']==(KEY_GRAVE|USHIFT));
    /* should be 128 entries in array */
    assert((sizeof(keycode)/sizeof(keycode[0]))==128);

    /* short options */
    char* optstring="hvVr:c:f:s:Se:C";

    /* long options */
    struct option longopt[]={
        { "help",    0, 0, 'h' },
        { "verbose", 0, 0, 'v' },
        { "version", 0, 0, 'V' },
        { "rdelay",  1, 0, 'r' },
        { "cdelay",  1, 0, 'c' },
        { "file",    1, 0, 'f' },
        { "string",  1, 0, 's' },
        { "stay",    0, 0, 'S' },
        { "escape",  1, 0, 'e' },
        { "connect", 0, 0, 'C' },
        { 0,         0, 0, 0   },
    };

    /* fetch basename of argv[0] */
    char* arg0=basename(argv[0]);

    /* preset the defaults for the various flags & settings */
    /* globals */
    verbose_mode=0;
    rdelay=0;
    cdelay=0;
    /* locals */
    char* filename=NULL;
    char* sendstr=NULL;
    int stay_connected=0;
    int connect=0;

    /* prevent getopt_long from printing error messages */
    opterr=0;

    /* start from beginning */
    optind=1;

    while (1) {
        int opt=getopt_long(argc, argv, optstring, longopt, NULL);
        /* no more options? */
        if (opt<0) {
            /* exit while loop */
            break;
        }
        switch (opt) {
            case 'V': /* version */
                printf(VERSION_STRING);
                exit(EXIT_SUCCESS);
                /* doesn't return */
                break;
            case 'v': /* verbose - multiple means more */
                verbose_mode+=1;
                break;
            case 'r': /* delay for RETURN's */
                rdelay=atoi(optarg);
                /* check value, zero, negative or > MAX_DELAY ms is not allowed */
                if ((rdelay<1)||(rdelay>MAX_DELAY)) {
                    /* do we want to fail early? or do something unexpected
                     * by the user? Let's fail for now */
                    error(EXIT_FAILURE,0,"<RETURN> delay (-r|--rdelay) out of bounds (1->%dms) at %d\n",MAX_DELAY,rdelay);
                    /* no return */
                }
                break;
            case 'c': /* delay for every character */
                cdelay=atoi(optarg);
                /* check value, zero, negative or > MAX_DELAY ms is not allowed */
                if ((cdelay<1)||(cdelay>MAX_DELAY)) {
                    /* Same as above, fail early, don't confuse user */
                    error(EXIT_FAILURE,0,"Char delay (-c|--cdelay) out of bounds (1->%dms) at %d\n",MAX_DELAY,cdelay);
                    /* no return */
                }
                break;
            case 'f': /* send file */
                /* save pointer to filename in argv[] array */
                filename=optarg;
                break;
            case 's': /* send string */
                sendstr=optarg;
                break;
            case 'S': /* stay connected after file/string send */
                stay_connected=1;
                break;
            case 'e': /* specify escape char, disallow '~' */
                escape_char=optarg[0];
                if ((escape_char<' ')||(escape_char>='~')) {
                    fprintf(stderr,"Escape character invalid\n");
                    if (escape_char=='~') {
                        fprintf(stderr,"I can't allow you to use '~',\n"
                                "\tyou'll hurt yourself if you're ssh'd into a system\n");
                    }
                    exit(EXIT_FAILURE);
                    /* no return */
                }
                break;
            case 'C': /* connect... really... connect this time! */
                connect=1;
                break;
            case 'h': /* help */
            default:  /* or anything weird */
                usage(arg0);
                /* doesn't return */
                break;
        }
    }

    /* hmmm, optind now points to rest of argv options... if we need them */

    /* satisfy request for verboseness */
    if (verbose_mode!=0) {
        printf(VERSION_STRING);
        if (escape_char!=ESCAPE_CHAR_DEFAULT) {
            printf("Setting escape character to '%c'\n",escape_char);
        }
        if (rdelay>0) {
            printf("Setting <RETURN> delay to %d ms\n",rdelay);
        }
        if (cdelay>0) {
            printf("Setting Character delay to %d ms\n",cdelay);
        }
        if (stay_connected!=0) {
            if ((filename==NULL)&&(sendstr==NULL)) {
                /* nothing to be sent, ignore stay_connected */
                stay_connected=0;
            } else {
                printf("Will stay connected after sending ");
                if (filename!=NULL) {
                    printf("file");
                }
                if ((filename!=NULL)&&(sendstr!=NULL)) {
                    printf(" and ");
                }
                if (sendstr!=NULL) {
                    printf("string");
                }
                printf(".\n");
            }
        }
        if (filename!=NULL) {
            printf("Sending file: %s\n",filename);
        }
        if (sendstr!=NULL) {
            printf("Sending string: '%s'\n",sendstr);
        }
    }

    if (connect==0) {
        fprintf(stderr,"\nConnect option not specified, preventing accidental invocation and\n"
                "subsequent freaking out because your keyboard is dead and you didn't\n"
                "read the man page or help.\n\n");
        exit(EXIT_FAILURE);
    }

    printf("Reminder: Escape sequence is '<CR> %c .'\n",escape_char);
    connect_user();

    return EXIT_SUCCESS;
}


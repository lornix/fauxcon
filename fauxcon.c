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
 *
 * <lornix@lornix.com> 2014-06-15
 *
 */

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

/* arbitrary line length limit, prevents wrap on typical display */
static const int MAX_LINE_LENGTH=70;

/* escape_char - what character is the escape char? Can't leave without it! */
static char escape_char=ESCAPE_CHAR_DEFAULT;

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
    event.type  = EV_SYN;
    event.code  = SYN_REPORT;
    event.value = 0;

    result=write(ufile, &event, sizeof(event));
    if (result!=sizeof(event)) {
        error(1, errno, "Error during event sync");
    }
}

/* convert an ASCII character given into a useful scancode for uinput */
static void sendchar(int val1)
{
    /* need to press SHIFT for this key */
#define USHIFT 0x1000
    /* need to press CTRL for this key */
#define UCTRL  0x2000

    /* 1:1 lookup table.  128 entries, ASC('A')=65=KEY_A|USHIFT */
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

    /* verify alignment of array                                   */
    /* just a spot check to make sure everything lines up properly */
    assert(keycode[' ']==(KEY_SPACE));
    assert(keycode['A']==(KEY_A|USHIFT));
    assert(keycode['a']==(KEY_A));
    assert(keycode['~']==(KEY_GRAVE|USHIFT));
    /* should be 128 entries in array */
    assert((sizeof(keycode)/sizeof(keycode[0]))==128);

    /* parse key, grabbing SHIFT & CTRL requirements */
    int need_shift=keycode[val1]&USHIFT;
    int need_ctrl=keycode[val1]&UCTRL;
    int key=keycode[val1]&(0xfff);

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

static void run(void)
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

/* create proper getopt_long parameters from poptions struct */
/* handles only short-name, only long-name or both type entries */
static void build_opts_objects(
        progoptions* poptions,
        char** optstring,
        struct option*** longoptarray)
{
    /* which poptions value are we parsing? */
    int index=0;
    /* which longoption index are we filling? */
    int longindex=0;
    /* string we're building */
    char* ostr=NULL;
    /* and length */
    int len=0;
    /* longoption array */
    struct option** lopts=NULL;

    /* while we aren't at the all-zeros last entry */
    while ((poptions[index].shortchar!=0)||(poptions[index].longname!=0)) {

        /* if entry has a short name value */
        if (poptions[index].shortchar!=0) {

            /* resize the string, add up to 3 chars (option+2:'s max)*/
            ostr=realloc(ostr,len+poptions[index].has_arg+2);

            /* grab character in shortchar (mask off the REQ flag) */
            ostr[len]=poptions[index].shortchar&(~REQ);
            len++;

            /* arguments? Type 1 = required (1 colon) */
            if (poptions[index].has_arg>0) {
                ostr[len]=':';
                len++;
            }

            /* arguments? Type 2 == optional (2 colons) */
            if (poptions[index].has_arg>1) {
                ostr[len]=':';
                len++;
            }

            /* fill end-of-string zero */
            ostr[len]=0;
        }

        /* if entry has a long name value */
        if (poptions[index].longname!=0) {

            /* resize the option array and fill in new entry */
            lopts=realloc(lopts,sizeof(struct option*)*(longindex+1));
            lopts[longindex]=calloc(1,sizeof(struct option));

            (*lopts[longindex]).name=poptions[index].longname;
            (*lopts[longindex]).has_arg=poptions[index].has_arg;
            (*lopts[longindex]).val=poptions[index].shortchar&(~REQ);
            (*lopts[longindex]).flag=0;

            longindex++;
        }

        index++;
    }

    /* add the zero filled entry at end of longoption array */
    lopts=realloc(lopts,sizeof(struct option*)*(longindex+1));
    lopts[longindex]=calloc(1,sizeof(struct option));

    /* give values back to caller */
    *optstring=ostr;
    *longoptarray=lopts;
}

static void free_mem(char* optstring, struct option** longoptarray)
{
    /* for each longoption entry, run free! */
    int longindex=0;
    while (longoptarray[longindex]->name!=0) {
        free(longoptarray[longindex]);
        longindex++;
    }

    /* drop the last all-zeros entry */
    free(longoptarray[longindex]);
    /* and then the whole array thingy */
    free(longoptarray);
    /* and the option string */
    free(optstring);
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

static void usage(char* arg0, progoptions poptions[])
{
    printf("\n");

    char* bname=basename(arg0);

    int line_len=printf("%s",bname);

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
        }
        line_len+=printf(" ");

        if ((poptions[index].shortchar&REQ)==0) {
            line_len+=printf("]");
        }

        /* arbitrary line length limit, prevents wrap on typical display */
        if (line_len>=MAX_LINE_LENGTH) {
            /* retrieve length of arg0 (basename) */
            line_len=strlen(bname);
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
        len=max_opt_len-len+2;

        /* and pad to make columns line up */
        printf("%*s",len,"");
        printf("%s\n",poptions[index].description);

        index++;
    }
    printf("\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    progoptions poptions[]=
    {
        /* short,   long,      has_arg, description */
        {  'h',     "help",    0,       "Show Help" },
        {  'v',     "verbose", 0,       "Verbose operation" },
        {  'V',     "version", 0,       "Show version information" },
        {  'd',     "delaycr", 1,       "Delay arg (ms) after every CR/LR character" },
        {  'D',     "delay",   1,       "Delay arg (ms) after every character" },
        {  'f',     "file",    1,       "Send contents of file 'arg' (and exit)" },
        {  's',     "string",  1,       "Send string 'arg' (and exit)" },
        {  'S',     "stay",    0,       "Stay connected after sending file or string" },
        {  'e',     "escape",  1,       "Specify Escape Character - Default (" Q(ESCAPE_CHAR_DEFAULT) ")" },
        {  'c'|REQ, "connect", 0,       "Connect to CONSOLE keyboard & mouse (REQUIRED)" },
        {   0,0,0, /* compiler will concatenate these all together */
            "Connect your keyboard to system's CONSOLE KB & Mouse.\n\n"
                "The required '-c/--connect' option is to prevent users from getting locked in\n"
                "without knowing how to exit.  You must always include this option to connect.\n\n"
                "To exit once running, you'll need to type the escape sequence (much like ssh(1)),\n"
                "by entering '<RETURN> % .', that is, the RETURN key, whatever your escape\n"
                "character is (default is " Q(ESCAPE_CHAR_DEFAULT) "), and then a period ('.').\n"
        },
    };

    /* to be filled in */
    char* optstring=NULL;
    struct option** longoptarray=NULL;

    /* create proper optstring & longoptarray from single poptions array */
    build_opts_objects(poptions,&optstring,&longoptarray);

    getopt_long(argc, argv, optstring, *longoptarray, NULL);

    free_mem(optstring,longoptarray);

    usage(argv[0],poptions);

    printf("Reminder: Escape sequence is '<CR> %c .'\n",escape_char);

    run();

    return EXIT_SUCCESS;
}


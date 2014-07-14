fauxcon
=======

A utility to provide a direct connection between your keyboard and the `console` of a system.
Very handy if you're ssh'd into a system, but need to fake keystrokes on the physical console.

I use this (so far!) to operate my Raspberryy Pi's as if I had a keyboard hooked up, while logged
in via ssh.  Honest, you don't know how useful this is until you try it.

It uses `uinput`, so you'll have to have the module loaded:

    sudo modprobe uinput

Should load it if necessary.  My laptops (Jessie/Sid) and RPi Raspian (Jessie/Sid) systems all
seem to have it loaded automatically.  You may force loading of the `uinput` module by placing
it's name in `/etc/modules`, or better, by placing it's name in a file within the
`/etc/modules-load.d` subdirectory.  Thusly:

    /etc/modules-load.d/load-uinput.conf
        # load uinput module automatically at startup
        #
        uinput

Of course, the uinput system will have permissions set to require root access to create a fake
keyboard/mouse device.  Generally, you'll have to start `fauxcon` with sudo to use it:

    sudo fauxcon

Another possibility, set the `setuid` bit on `fauxcon`:

    sudo chown root:root fauxcon
    sudo chmod 4755 fauxcon         # -or- u=rwxs,g=rx,o=rx

You _could_ edit the `udev` rules to allow someone in a particular group to have access, then add
your user to that group.  (__TODO:__ udev instructions. Perhaps to be added later)

By default, to __EXIT__ `fauxcon`, you'll need to type a specific 3 character sequence, since
we're emulating a full keyboard, you need to be able to type anything and everything.  I've chosen
a sequence similar to `SSH`'s _escape_ character, defaulting to '%'.  (Can't be the same, as
I envision using this over `ssh` connections too.)  You'll need to type \<ENTER\>, then the
_ESCAPE CHAR_, then a period ('.').  Hopefully not too many '\<CR\>%.' sequences occur in the
wild.

__TODO:__ Mouse passthrough. Quirky, since I'd really have to grab and constrain the mouse locally,
while transmitting all the motions and clicks. ('remote mode'? see below)  Probably best to make
mouse passthrough __NOT__ enabled by default, since anyone trying out `fauxcon` without reading
the man page would suddenly have no working mouse, making it considerably more difficult to
frantically kill `fauxcon` to restore their keyboard. (Nah, that wouldn't happen, would it?)

__TODO:__ Remote mode. Viability of a 'remote' connection method? Run `fauxcon` locally, it
connects to another machine and passes keystrokes through.  Perhaps like `rsync` does, essentially
connecting to itself to do work. (would make constraining mouse easier, see 'mouse passthrough'
TODO above)


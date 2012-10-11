Minimal UI for YAFFS encryption on Android devices
--------

I'm using this with [this whisperyaffs branch of the CyanogenMod kernel](https://github.com/garyp/cm-kernel/commits/whisper_yaffs) and [Guardian Project's LUKS toolkit](https://github.com/guardianproject/LUKS).

The following provides full encryption of the /data partition and the sdcard.

I've inserted these lines into /init.rc where /data was being mounted:

> exec /system/bin/logwrapper /sbin/yaffsunlock /system/xbin/mount /dev/block/mtdblock5 /data
> exec /sbin/cryptsetup luksOpen /dev/block/mmcblk0p1 sdcard --key-file /data/local/sdcard.key

And this in /system/etc/vold.fstab:

> dev_mount sdcard /mnt/sdcard auto /devices/virtual/block/dm-0 /devices/platform/msm_sdcc.2/mmc_host/mmc1

(Note this assumes the sdcard device being the zero'th device mapper device.)

Original code from [sigkill1337 at xda-developers](http://forum.xda-developers.com/showthread.php?t=866131) -- [tarball](http://forum.xda-developers.com/attachment.php?attachmentid=459501&d=1291847781) and [init.rc](http://forum.xda-developers.com/attachment.php?attachmentid=459500&d=1291847781)

Todo
--------

-make the interface less horrible
-"destroy" ability (key wipe)
-allow it to boot up with a prepared unencrypted ramdisk image, for ease of use in emergencies etc, or "plausible deniability"

Building
--------

You'll need some .so files from either an Android emulator or your phone; just
start the emulator or plug in your phone to USB in dev mode, so 'adb pull' can
get the files.

Here's the current general idea for building:

    cd luksunlock
    git submodule init
    git submodule update
    make -C external
    make -C external libpng-build
    make -C external libpng-install
    make

Building Firmware
=================

Most of the time when you’re distributing firmware you have permission from the
OEM or ODM to redistribute the non-free parts of the system firmware, e.g.
Dell can re-distribute the proprietary Intel Management Engine as part as the
firmware capsule that gets flashed onto the hardware.

In some cases that’s not possible, for example for smaller vendors or people
selling OpenHardware. For reasons (IFD, FMAP and CBFS…) you need to actually
build the target firmware on the system you’re deploying onto, where build
means executing random low-level tools to push random blobs of specific sizes
into specific unnecessarily complex partition formats rather than actually
compiling .c into executable code.

The solution of a manually updated interactive bash script isn’t awesome from a
user-experience or security point of view.

The other things that might be required is a way to `dd` a few bytes of
randomness into the target image at a specific offset and also to copy the old
network MAC address into the new firmware.

The firmware-builder functionality allows you to ship an archive (typically
in `.tar` format, as the `.cab` file will be compressed already) within the
`.cab` file as the main “release”.

Within the `.tar` archive will be a startup.sh file and all the utilities or
scripts needed to run the build operation, statically linked if required.
At firmware deploy time fwupd will explode the tar file into a newly-created
temp directory, create a bubblewrap container which has no network and
limited file-system access and then run the startup.sh script. Once complete,
fwupd will copy out just the `firmware.bin` file and then destroy the bubblewrap
container and the temporary directory.

This is the directory that is available to the bubble-wrap confined script.
If, for instance, a plugin needs the old system firmware blob (for a bsdiff)
then the plugin can write to this directory and the startup.sh script will be
able to access it as the chroot-ed `/boot`.

Firmware `.cab` files using this functionality should list the `.tar` file:

    <release>
      <checksum filename="firmware.tar" target="content"/>
    </release>

and also should include the name of the script to run as additional metadata:

    <custom>
      <value key="fwupd::BuilderScript">startup.sh</value>
      <value key="fwupd::BuilderOutput">firmware.bin</value>
    </custom>

# Using Visual Studio Code to debug

This directory contains a collection of scripts and assets to make debugging using Visual Studio Code easier.

## Preparing
First install the following applications locally:
* GDB Server
* GDB
* Visual Studio Code

In Visual Studio code, visit the extension store and install *C/C++* which is an extension provided by Microsoft.
Configure Visual Studio code to open the folder representing the root of the fwupd checkout.

## Building
Run `./contrib/debugging/build.sh` to build fwupd with all default options and create helper scripts pre-configured for debugger use.
The application will be placed into `./dist` and helper scripts will be created for `fwupdtool`, `fwupdmgr`, and `fwupd`.

## Running
To run any of the applications, execute the appropriate helper script in `./dist`.

## Debugging
To debug any of the applications, launch the helper script with the environment variable `DEBUG` set.
For example to debug `fwupdtool get-devices` the command to launch would be:

```
sudo DEBUG=1 ./dist/fwupdtool.sh get-devices
```

This will configure `gdbserver` to listen on a local port waiting for a debugger to connect.

## Using Visual Studio code
During build time a set of launch targets will have been created for use with Visual Studio Code.

Press the debugging button on the left and 3 targets will be listed at the top.
* gdbserver (fwupdtool)
* gdbserver (fwupd)
* gdbserver (fwupdmgr)

Select the appropriate target and press the green arrow to connect to `gdbserver` and start debugging.

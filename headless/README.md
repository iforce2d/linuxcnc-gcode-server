# Scripts for headless LinuxCNC startup and shutdown

These scripts can be used to start and stop a LinuxCNC system without having a graphical user interface. This can be useful on low power hardware like a Raspberry Pi ZeroW, or where a hardware-only HALUI interface is to be used.

These scripts were made by slighly modifying the default 'linuxcnc' script, and were tested with LinuxCNC 2.8.4

## Usage

Starting up without a GUI requires that the [DISPLAY]DISPLAY property in your .ini file be set to 'dummy'.

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1351.png)

To start LinuxCNC, run linuxcnc-headless and give your machine .ini file:

`linuxcnc-headless inifile.ini`

To stop a running LinuxCNC system, run stop-linuxcnc with no parameters. This will also stop a GUI instance of LinuxCNC.

`stop-linuxcnc`

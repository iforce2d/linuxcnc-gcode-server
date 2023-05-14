# linuxcnc-gcode-server
Allows connecting to a LinuxCNC installation and executing commands, similar to linuxcncrsh.

The motivation was to allow easier control by OpenPNP. To this end some non-standard (for LinuxCNC) commands will be intercepted and handled either within this server, or translated into something LinuxCNC can understand:
- M115 - firmware version
- M114 - current position
- M105 - read analog sensor
- M400 - wait for completion
- BEGINSUB - start batch
- ENDSUB - send batch

All other g-code commands will be passed through unchanged, and are relayed to LinuxCNC as MDI commands.

<br>

## Non-standard commands

#### M115
No interaction with LinuxCNC. Returns a string like:  

`ok FIRMWARE_NAME:linuxcnc-gcode, FIRMWARE_VERSION:0.1`

#### M114
Returns the current position of the machine, eg.  

`ok X:1.200000 Y:3.400000 Z:5.600000 A:7.800000`

#### M105
Returns the values of the first four analog inputs (motion.analog-in-00 etc)  

`ok T0:0.147000 T1:0.7890000 T2:0.000000 T3:0.000000`

#### M400

This command will cause all subsequent commands to be deferred until the machine is idle.

#### BEGINSUB, ENDSUB
See the section below about blending.

<br>

## Building

Requires the LinuxCNC headers and libs available. On my system I built LinuxCNC from source which produced the package "linuxcnc-uspace-dev", which I then installed. Not sure how you would get this by other methods...

With the requirements in place, you should be able to just run `make` to build.

This server uses NML to interface with LinuxCNC. It expects to find the NML definition file at /usr/share/linuxcnc/linuxcnc.nml which is probably where it will be unless you have really been messing around with things.

<br>

## Basic usage

First startup LinuxCNC, then run this server. By default it will listen on port 5007, or you can change this with the -p option, eg.

    ./linuxcnc-gcode-server -p 5050

To check connection to the server, you can use a telnet connection like:

    telnet 192.168.1.140 5007

If the machine is enabled and homed, you should be able to move it around with g-code commands.

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1180.png)

You can use the -e option to instruct the machine to be enabled and homed when the server starts, eg.

    ./linuxcnc-gcode-server -e

This is the equivalent of clicking the e-stop button off and the machine power button on, and then homing each axis that is not already homed. This is probably not advisable on a real machine, but it's quite convenient during development when using a dummy machine, to save some repetitive clicking. It also allows you to run a headless LinuxCNC without any traditional user interface, and still enable the machine and run g-code.

You can optionally specify the .ini file of your machine with the -i parameter, eg.:

    ./linuxcnc-gcode-server -i /path/to/your/machine.ini

This is only necessary if you want to use the subroutines explained in the section below about blending.


<br>

## Usage with OpenPNP

Set up a GCodeDriver like this:

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1177.png)

In the `Driver Settings` tab, clicking on `Detect Firmware` should show some output like this:

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1179.png)

If the gcode server is stopped and restarted, OpenPNP will lose communication with it. You can let it re-connect by clicking the power button off, then on again.

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1181.png)

Note that OpenPNP does not read the current position of the machine when connecting, so if the machine was moved by commands outside OpenPNP, they will not be in sync until OpenPNP issues the next move command.

<br>

Some commonly used settings are listed below (see OpenPNP's [GcodeDriver Command Reference](https://github.com/openpnp/openpnp/wiki/GcodeDriver_Command-Reference) for more details).


### COMMAND_CONFIRM_REGEX

The standard rule as suggested by OpenPNP is fine:

    ^ok.*

<br>

### POSITION_REPORT_REGEX

The standard rule as suggested by OpenPNP is fine:

    ^ok X:(?<x>-?\d+\.\d+) Y:(?<y>-?\d+\.\d+) Z:(?<z>-?\d+\.\d+) A:(?<rotation>-?\d+\.\d+)

<br>

### COMMAND_ERROR_REGEX

The standard rule as suggested by OpenPNP is fine:

    ^error:.*

<br>

### ACTUATE_BOOLEAN_COMMAND

You can use LinuxCNC's standard M64 and M65 to switch digital outputs on and off. The value P0, P1 etc maps to motion.digital-out-00, motion.digital-out-01 etc. There are various formats that will work for defining this in OpenPNP, I have found this style works ok:

    M{True:64}{False:65} P0

<br>

### MOVE_TO_COMMAND

OpenPNP will probably suggest something like this which will work fine:

    G1 {X:X%.3f} {Y:Y%.3f} {Z:Z%.3f} {A:A%.4f} {FeedRate:F%.0f}

<br>

### MOVE_TO_COMPLETE_COMMAND

This should be set to M400:

    M400

<br>

### ACTUATOR_READ_COMMAND
I have not tested this, but I think it would be just:

    M105

This will always return four values, for motion.analog-in-00, motion.analog-in-01 etc.

<br>

### ACTUATOR_READ_REGEX
I have not tested this, but I think it would be like:

    ^ok T0:(?<Value>-?\d+\.\d+)

That would be ok if you wanted to read motion.analog-in-00. But because M105 always returns multiple values, to read T1, T2 etc. you would need a a little extra .* in the regex to skip any preceding values:

    ^ok.* T2:(?<Value>-?\d+\.\d+)

<br>

## Setting acceleration

To have OpenPNP specify acceleration for moves, the `Motion Control Type` option in the `Driver Settings` tab must be set to a type that controls acceleration, eg. EuclideanAxisLimits, ConstantAcceleration. Then in the MOVE_TO_COMMAND definition you can prepend a rule to output an acceleration setting, for example:

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1182.png)

This will produce a g-code command like `M171 P125`. The M171 is not a standard g-code, it is a [user defined command](https://linuxcnc.org/docs/devel/html/gcode/m-code.html#mcode:m100-m199) that you must create on the LinuxCNC machine. The exact number 171 is not really important, it just needs to be from 100 to 199.

To create the user defined command, you need to make a bash script with the same name, no extension, upper-case M. For this example the file name would be "M171". This script must be placed in the directory specified by PROGRAM_PREFIX in your LinuxCNC .ini file:

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1183.png)

The contents of this bash script should be:

    #!/bin/bash
    acceleration=$1
    halcmd setp ini.x.max_acceleration $acceleration
    halcmd setp ini.y.max_acceleration $acceleration
    halcmd setp ini.z.max_acceleration $acceleration
    exit 0

<br>

## Blending
LinuxCNC is capable of blending consecutive segments together when G64 is in effect, but unfortunately sending commands via the MDI interface does not always allow this to happen. In my experiments, blending typically occurs in only 70-80% of cases where it would normally be expected. This is due to the lack of synchronization between the timing of commands entering the queue, and when those commands are allowed to start execution. For example if you issue two MDI commands and the machine starts executing the first one before the second has been received, blending cannot occur.

To work around this problem, multiple commands can be grouped into a batch for processing as a cohesive set by enclosing them inside `beginsub` and `endsub` keywords. This will cause the commands to be stored in a buffer and only sent to LinuxCNC when all commands of the group are known, and full blending can be achieved reliably. For example with this input:

    beginsub
    g1 x10 y20
    g1 x25 y25
    g1 x40 y20
    endsub

... nothing would happen until the `endsub`, at which point all the commands will be sent.

To ensure that the grouped commands are all processed together, a temporary [o-code subroutine](https://linuxcnc.org/docs/html/gcode/o-code.html) file is created and executed. This file will be created in the location specified by the RS247NGC:SUBROUTINE_PATH property of your .ini file:

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1184.png)

Since the subroutine file is only temporary and will be written and read potentially thousands of times during a pick and place job, it might be preferable to place it in RAM instead of on hard disk. You can find some info about which paths to use in [this Stack Overflow discussion](https://stackoverflow.com/questions/10982911/creating-temporary-files-in-bash). In the screenshot above, the /run/user/1000 path is actually a RAM location, and as such all the 'files' it contains will be lost when the computer is shut down. So if you already have your own subroutine files, you might actually want to use a normal hard disk location, or maybe copy them into the RAM location each time you start LinuxCNC.

Because SUBROUTINE_PATH is defined in your .ini file, to use this feature you must provide the .ini file when starting the server, eg.:

    ./linuxcnc-gcode-server -i /path/to/your/machine.ini

You can check if these paths are correct when the server starts up:

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1186.png)

Finally, to let OpenPNP manage these batches of commands, you can set up your `MOVE_TO_COMMAND` and `MOVE_TO_COMPLETE_COMMAND` so that a batch will be started before moves are issued, and finalized before the M400 'wait' command :

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1189.png)

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1190.png)

A `beginsub` while a batch is already in progress has no effect.

When using the OpenPNP user interface to jog the machine, the MOVE_TO_COMPLETE_COMMAND is not used, so there will be no `endsub` to complete the batch. To workaround this, a timeout is used to automatically send the batch to LinuxCNC if no further commands are given within a certain time. The default timeout is 250ms, you can change this with the -t parameter:

    ./linuxcnc-gcode-server -t 750

<b>Note:</b> even when using batches to process commands as a group, there are still other factors that can interrupt blending, like the M64/M65
commands or setting the acceleration via bash script as mentioned above.

<b>Note:</b> the commands within a `beginsub`/`endsub` block are passed to LinuxCNC without any special handling, so you cannot use the 'non-standard' commands (M115, M114, M105, M400). These will be ignored inside a batch.

<br>

## Multiple clients

Although this server is capable of communicating with multiple clients at the same time, that's not really the intended use case. The main issue is that the `beginsub` / `endsub` status is tracked globally on the server, not per client. So one client can start the batch and a different client could end it. If you want to switch between using different clients, just make sure there is no ongoing subroutine batch.


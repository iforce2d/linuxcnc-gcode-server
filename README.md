# linuxcnc-gcode-server
Allows connecting to a LinuxCNC installation and executing commands, similar to linuxcncrsh.

The motivation was to allow easier control by OpenPNP, to this end some non-standard (for LinuxCNC) commands will be intercepted and handled either within this server, or translated into something LinuxCNC can understand:
- M115 - firmware version
- M114 - current position
- M105 - read analog sensor
- M400 - wait for completion

All other g-code commands will be passed through unchanged, and are relayed to LinuxCNC as MDI commands.

<br>

## Non-standard commands

#### M115
Returns a string like:
`ok FIRMWARE_NAME:linuxcnc-gcode, FIRMWARE_VERSION:0.1`

#### M114
Returns the current position of the machine, eg.
`ok X:1.200000 Y:3.400000 Z:5.600000 A:7.800000`

#### M105
Returns the values of the first four analog inputs (motion.analog-in-00 etc)
`ok T0:0.147000 T1:0.7890000 T2:0.000000 T3:0.000000`

#### M400

This command will cause all further commands to be deferred until the machine is idle.

<br>

## Building

Requires the LinuxCNC headers and libs available on your system. On my system I built LinuxCNC from source which produced the package "linuxcnc-uspace-dev", which I then installed. Not sure how you would get this by other methods...

With the requirements in place, you should be able to just run `make` to build.

This server uses NML to interface with LinuxCNC. It expects to find the NML definition file at /usr/share/linuxcnc/linuxcnc.nml which is probably where it will be unless you have really been messing around with things.

<br>

## Basic usage

First startup LinuxCNC, then run this server. By default it will listen on port 5007, you can change this with the -p option, eg.

    ./linuxcnc-gcode-server -p 5050

You can use the -e option to instruct the machine to be enabled and homed when the server starts, eg.

    ./linuxcnc-gcode-server -e

This is the equivalent of clicking the e-stop button off and the machine power button on, and then homing each axis that is not already homed. This is probably not advisable on a real machine, but it's quite convenient during development when using a dummy machine, to save some repetitive clicking. It also allows you to run a headless LinuxCNC without any traditional user interface, and still enable the machine and run g-code.

To check connection to the server, you can use a telnet connection like:

    telnet 192.168.1.140 5007

If the machine is enabled, you should be able to move it around with g-code commands.

![alt text](https://www.iforce2d.net/tmp/openpnp/Selection_1180.png)

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
LinuxCNC is capable of blending consecutive segments together when G64 is in effect, but unfortunately sending commands via the MDI interface does not always allow this to happen. In my experiments, blending typically occurs in only 70-80% of cases where it would normally be expected. I think this is due to the lack of synchronization between the timing of commands entering the queue, and when those commands are allowed to start execution. For example if you issue two MDI commands and the machine starts executing the first one before the second has been received, blending cannot occur.

There are also other factors that interrupt blending, like the M64/M65 command which will always be executed immediately instead of being queued and performed sequentially with movements. Setting the acceleration via bash script as mentioned above also tends to interrupt blending, I found this can be somewhat improved by adding M400 after every acceleration setting, but it does not fully solve the problem.

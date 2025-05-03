# stpvis

A simple program, created for a group project.

Provides a simple visualization of Spanning Tree Protocol.

## controls
To add a switch, endpoint, or connection, left click.
To switch between placing switches or endpoints, use keys 1 and 2, respectively.
Middle click on a port to remove a connection.
Hold right click and drag to move devices.
Press SPACE to play/pause the simulation

Clicking on the empty space on an endpoint will activate "evil mode"
In evil mode, the device will spam BPDUs, proclaiming itself the ultimate root of the STP topology.

You cannot remove devices. Instead either shove them to the side or restart the program.
There are a maximum of 256 devices. Any more and the program will forcefully close.

## legend

- Blue dots are broadcast "pings" (really empty frames)
- Light purple dots are Config BPDUs
- Tan dots are TCNs (topology change notification)
- Purple ports are root ports.

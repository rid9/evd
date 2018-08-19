# evd - event daemon

evd is a program for globally controlling events such as ACPI events or key
presses on Linux. It is configured at compile time via the **config.h** file.

# Installation

Edit **config.h** to configure the application, then build and install it as
root:

    make install

# Running

To run the application, either start it by typing `evd -b` at the command
prompt.

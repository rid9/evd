# ksd - keyboard shortcuts daemon

ksd is a program for globally controlling keyboard shortcuts on Linux. It is
configured at compile time via the **config.h** file.

# Installation

Edit **config.h** to configure the application, then build and install it as
root:

    make install

# Running

To run the application, either start it by typing `ksd -b` at the command
prompt, or enable the `ksd` service via **systemd**.

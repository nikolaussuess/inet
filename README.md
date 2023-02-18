<!-- [![badge 1][badge-1]][1] [![badge 2][badge-2]][2] -->

INET Framework for OMNEST/OMNeT++
=================================

The [INET framework](https://inet.omnetpp.org) is an open-source communication networks
simulation package, written for the OMNEST/OMNeT++ simulation system. The INET
framework contains models for numerous wired and wireless protocols, a detailed
physical layer model, application models and more. See the CREDITS file for the
names of people who have contributed to the INET Framework.

IMPORTANT: The INET Framework is continuously being improved: new parts
are added, bugs are corrected, and so on. We cannot assert that any protocol
implemented here will work fully according to the specifications. YOU ARE
RESPONSIBLE YOURSELF FOR MAKING SURE THAT THE MODELS YOU USE IN YOUR SIMULATIONS
WORK CORRECTLY, AND YOU'RE GETTING VALID RESULTS.

Contributions are highly welcome. You can make a difference!

> **Note**<br>
> This repository is a fork of the [INET repository](https://github.com/inet-framework/inet). We have adapted a few things, mainly with respect to MPLS. The goal is to provide an implementation that allows the simulation of MPLS traffic with a pre-existing data plane. To achieve this, we have modified a couple of classes and files.<br>
> Features:<br>
> - Priority stack for MPLS forwarding rules<br>
> - ECMP<br>
> - Some fixes ...<br>
> See file [CHANGES.MD](CHANGES.MD) for details.



GETTING STARTED
---------------
You may start by downloading and installing the INET framework. Read the INSTALL
file for further information.

Then you can gather initial experience by starting some examples or following a
tutorial or showcase (see the /examples, /showcases or /tutorials folder).
After that, you can learn the NED language from the OMNeT++ manual & sample
simulations.

After that, you may write your own topologies using the NED language. You may
assign some of the submodule parameters in NED files. You may leave some of
them unassigned.

Then, you may assign unassigned module parameters in omnetpp.ini of your
simulation. (You can refer to sample simulations & manual for the content of
omnetpp.ini)

Finally, you will be ready to run your simulation. As you see, you may use
the INET framework without writing any C++ code, as long as you use the
available modules.

To implement new protocols or modify existing ones, you'll need to add your
code somewhere under the src directory. If you add new files under the 'src'
directory you will need to regenerate the makefiles (using the 'make makefiles'
command).

If you want to use external interfaces in INET, enable the "Emulation" feature
either in the IDE or using the inet_featuretool then regenerate the INET makefile
using 'make makefiles'.


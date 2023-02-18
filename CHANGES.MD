# Changes
This repository contains a fork of the repository of the [INET framework](https://github.com/inet-framework/inet) for [OMNeT++](https://omnetpp.org). We have adapted a few things, mainly with respect to MPLS. The goal is to provide an implementation that allows the simulation of MPLS traffic with a pre-existing data plane. To achieve this, we have modified a couple of classes and files.

Although the simulations work as intended and we have achieved our goal, we cannot guarantee that these modifications do not break other classes that we currently are not using. Use our code at your own risk!

**Features:**

* Priority stack for MPLS forwarding rules
* ECMP
* Some fixes ...

## Details
* **Implement priority tag for the `LibTable` module**  
We allow the MPLS `LibTable` to contain multiple entries for the same label with different priorities. This enables to install backup paths. The entry with the *lowest* integer value in the `priority` field for that the outgoing interface is `UP` is chosen for forwarding.  
Please note, that the `LibTable::installLibEntry` has an additional (optional) parameter `int priority` with default value `0`. Also note, that the internal data structure of the `LibTable` class has changed.

* **ECMP for MPLS forwarding rules** in the `LibTable` module  
The traffic is equally distributed over all paths (ECMP) if there are multiple entries in the `LibTable` that have an equal `priority` value. Again, we only use those rules where the outging interfaces are `UP`.

* **Multiple loopback interfaces**  
We support multiple loopback interfaces at routers. The first loopback interface (usually `lo0`) has IP address `127.0.0.1`, the second one `127.0.0.2` and so on. (Modified files: `Ipv4NodeConfigurator.h` and `Ipv4NodeConfigurator.cc`)

* **New router module `MplsRouter`**  
The new router class is a copy of `RsvpMplsRouter.ned`, but with an additional loopback interface called `mlo0` that is connected to the `ml` message dispatcher. This allows the MPLS module to reroute traffic via a loopback interface. We need this to also allow those `pop` operations where the next label must be processed at the same router.

* **Handling of ICMPv4 packets**  
When using the `ScenarioManager` to simulate link fails, the IP module sends ICMPv4 packets to the (local) **RsvpTe** and **LinkStateRouting** module when it has no remaining (valid) route to the destination. However, these classes could not handle such packets, so that the simulations crashed.  
We currently added code to just ignore such `ICMPv4` packets. A more advanced handling should, however, be added in the future!

* `Mpls.cc` replaces a loopback interface ID before forwarding the packet to the IP module by the first non-loopback interface ID. This was required to get the last POP operation working.

* **`RsvpClassifier.cc` pushes the label**  
The `RsvpClassifier` now actually pushes the label onto the stack. (Method: `RsvpClassifier::lookupLabel`)

Additionally, some logging messages were added for debugging purposes.

## Usage
1. Create an `omnetpp.ini` file:
```
[General]
network = test_export
sim-time-limit = 6s
**.R1.libTable.config = xmldoc("R1_lib.xml")
**.R2.libTable.config = xmldoc("R2_lib.xml")
**.R3.libTable.config = xmldoc("R3_lib.xml")
**.R4.libTable.config = xmldoc("R4_lib.xml")
**.R5.libTable.config = xmldoc("R5_lib.xml")
**.rsvp.helloInterval = 0.2s
**.rsvp.helloTimeout = 0.5s
**.ppp[*].queue.typename = "DropTailQueue"
**.ppp[*].queue.packetCapacity = 10

# The classification files that determine which label to add to packets
**.R1.classifier.config = xmldoc("R1_classification.xml")
**.R2.classifier.config = xmldoc("R2_classification.xml")
**.R3.classifier.config = xmldoc("R3_classification.xml")
**.R4.classifier.config = xmldoc("R4_classification.xml")
**.R5.classifier.config = xmldoc("R5_classification.xml")

# If you want to fail links, for example ...
*.scenarioManager.script = xmldoc("scenario.xml")

[Config UDP]
**.host1.numApps = 1
**.host1.app[0].typename = "UdpBasicApp"
**.host1.app[0].localPort = 1000
**.host1.app[0].destPort = 1000
**.host1.app[0].messageLength = 64 bytes
**.host1.app[0].sendInterval = 0.01s
**.host1.app[0].destAddresses = "target1"

**.target1.numApps = 1
**.target1.app[0].typename = "UdpSinkApp"
**.target1.app[0].io.localPort = 1000
# ...
```

2. Add a topology in a `package.ned` file:
```
package inet.examples.mpls.frrtest;
import inet.common.scenario.ScenarioManager;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.node.inet.StandardHost;
import inet.node.mpls.MplsRouter;

network test_export
{
    submodules:
        configurator: Ipv4NetworkConfigurator;
        scenarioManager: ScenarioManager;
        R1: MplsRouter {
            parameters:
                peers = "ppp0 ppp1";
            gates:
                pppg[5];
        }
        # other routers, ...
        host1: StandardHost {
            gates:
                pppg[1];
        }
        # other hosts ...
	connections:
        R1.pppg[0] <--> { delay = 10ms; datarate = 1048576kbps; } <--> R2.pppg[0];
        # additional connections ...
}
```

3. The lib XML files (e.g. `R1_lib.xml`) contain the MPLS rules for the `LibTable`:  
```
<libtable>
  <!-- POP via loopback, then process next label -->
  <libentry>
    <priority>0</priority>
    <inLabel>3</inLabel>
    <inInterface>any</inInterface>
    <outInterface>mlo0</outInterface>
    <outLabel>
      <op code="pop" />
    </outLabel>
  </libentry>
  <!-- POP and forward packet -->
  <libentry>
    <priority>0</priority>
    <inLabel>17</inLabel>
    <inInterface>any</inInterface>
    <outInterface>ppp0</outInterface>
    <outLabel>
      <op code="pop" />
    </outLabel>
  </libentry>
  <!-- use the same priority to use ECMP -->
  <!-- use a higher value for priority to define a backup rule -->
  <libentry>
    <priority>1</priority>
    <inLabel>17</inLabel>
    <inInterface>any</inInterface>
    <outInterface>ppp1</outInterface>
    <outLabel>
      <op code="pop" />
      <op code="push" value="16" />
    </outLabel>
  </libentry>
  <!-- other rules -->
<libtable>
```

4. The classification files look like this (`R1_classification.xml`). All packets sent from `host1` with destination `target1` will get assigned the label `17` in this example:  
```
<fectable>
  <fecentry>
    <id>1</id>
    <label>17</label>
    <destination>target1</destination>
    <source>host1</source>
  </fecentry>
</fectable>
```


## Contact
Feel free to get in contact with me.  
Nikolaus Suess &lt;nikolaus.suess@univie.ac.at&gt;
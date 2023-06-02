# Changes
This repository contains a fork of the repository of the [INET framework](https://github.com/inet-framework/inet) for [OMNeT++](https://omnetpp.org). We have made a few adaptions, mainly regarding MPLS. The goal is to provide an implementation that allows the simulation of MPLS traffic with a pre-existing data plane. To achieve this, we have modified a couple of classes and files.

Although the simulations work as intended and we have achieved our goal, we cannot guarantee that these modifications do not break other classes that we currently are not using. Use our code at your own risk!

**Features:**

* Priority stack for MPLS forwarding rules
* ECMP (including a preference attribute)
* Some fixes ...

## Details
* **Implement priority tag for the `LibTable` module**  
We have implemented the ability for the MPLS `LibTable` to contain multiple entries for the same label with different priorities. This enables to install backup paths. The entry with the *lowest* integer value in the `priority` field for that the outgoing interface is `UP` is chosen for forwarding.  
Please note, that the `LibTable::installLibEntry` has an additional (optional) parameter `int priority` with default value `0`. Also note, that the internal data structure of the `LibTable` class has changed.

* **ECMP for MPLS forwarding rules** in the `LibTable` module  
The traffic is equally distributed over all paths (ECMP) if there are multiple entries in the `LibTable` that have an equal `priority` value. Again, we only use those rules where the outging interfaces are `UP`.  
Furthermore, it is possible to set *ECMP weights* using the `preference` attribute. This allows to non-uniformly split the traffic based on the ECMP weights.  
A `preference` value of `0` indicates that the corresponding forwarding entry is disabled and will never be used for forwarding. For values $p_i>0$, a fraction $f_i=\frac{ p_i }{\sum_j p_j}$ of traffic will be forwarded using the respective rule $i$.

* **Multiple loopback interfaces**  
We support multiple loopback interfaces at routers. The first loopback interface (usually `lo0`) has IP address `127.0.0.1`, the second one `127.0.0.2` and so on. (Modified files: `Ipv4NodeConfigurator.h` and `Ipv4NodeConfigurator.cc`)

* **New router module `MplsRouter`**  
The new router class is a copy of `RsvpMplsRouter.ned`, but with an additional loopback interface called `mlo0` that is connected to the `ml` message dispatcher. This allows the MPLS module to reroute traffic via a loopback interface. We need this to also allow those `pop` operations where the next label must be processed at the same router.

* **Handling of ICMPv4 packets**  
When using the `ScenarioManager` to simulate link fails, the IP module sends ICMPv4 packets to the (local) **RsvpTe** and **LinkStateRouting** modules when it has no remaining (valid) route to the destination. However, these classes were unable to handle such packets, resulting in simulation crashes.  
We added code to just ignore such `ICMPv4` packets. A more advanced handling should, however, be implemented in the future!

* `Mpls.cc` replaces a loopback interface ID by the first non-loopback interface ID before forwarding the packet to the IP module. This modification was necessary to get the last POP operation working.

* **`RsvpClassifier.cc` pushes the label**  
The `RsvpClassifier` now actually pushes the label onto the stack. (Method: `RsvpClassifier::lookupLabel`)

Additionally, some logging messages were added for debugging purposes.

## Usage
1. Create an `omnetpp.ini` file.  
Most of the values here can be chosen as needed. It is, however, important to note, that in this example flows are represented by a `StandardHost` pair, where one host sends UDP packets using the `UdpBasicApp` module, and the target node receives these packets using `UdpSinkApp`. In fact, it is sufficient to have only a single target but separate source nodes, because the `(source,target)` tuple is used to classify packets and give them the appropriate labels.  
Other configuration files, such as the lib table contents and the classification files are also loaded in the `omnetpp.ini` file for each router.
```
[General]
network = test_export
sim-time-limit = 6s
# Precalculated LIB tables
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

# If you want to fail links, for example, this can be done with the scenario manager
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
        # Routers are of the class MplsRouter
        R1: MplsRouter {
            parameters:
                peers = "ppp0 ppp1";
            gates:
                pppg[5];
        }
        # other routers, ...

        # Hosts
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
    <preference>1</preference>
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
    <preference>2</preference>
    <inLabel>17</inLabel>
    <inInterface>any</inInterface>
    <outInterface>ppp0</outInterface>
    <outLabel>
      <op code="pop" />
    </outLabel>
  </libentry>
  <!-- use the same priority to use ECMP -->
  <!-- use a higher value for priority to define a backup rule -->
  <!-- The preference is optional. In this example, the first rule will be chosen for 1/3 of the packets and the second one for 2/3. -->
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

## Modification of the LIB table during simulation time
In addition to the "usual" (time-based) actions that can be configured using the `ScenarioManager`, such as failing links, we have also added new options.

It is possible to

* add new FEC entries (based on the time).
* delete one or multiple existing FEC entries (based on time).
* update an existing FEC entry (based on time or on the traffic received by the router).

It is not possible to identify uniquely a single FEC entry even by a tuple `(label, outInterface, priority, preference)`. To enable the modification of a single forwarding entry, we had to assign new "IDs" to the entries. As a solution, we added an integer attribute `group` to rules. A logical group can consist of multiple forwarding entries, and a single forwarding entry may be part of multiple groups. This concept can be beneficial in scenarios, where it is necessary to remove multiple entries at the same time due to an event.

In the `*_lib.xml` files, that contain the precomputed rules, the syntax for adding multiple groups to a rule is as follows:
```
  <libentry>
    <priority>0</priority>
    <inLabel>20017</inLabel>
    <inInterface>any</inInterface>
    <outInterface>mlo0</outInterface>
    <groups>
      <group id="2" />
      <group id="8" />
      <group id="6" />
      <group id="5" />
      <group id="4" />
    </groups>
    <outLabel>
      <op code="pop" />
    </outLabel>
  </libentry>
```
The `groups` tag can be left empty or omitted if there are no groups associated with an entry.

### Actions based on time (using the `ScenarioManager`)
In order to modify e.g. the preference (and set it to 0) after 1 second, one can add the following code to the `scenario.xml` file:
```
<scenario>
    <at t="1">
        <update-entry group="4" module="R1.libTable">
            	<preference value="0" />
        </update-entry>
    </at>
</scenario>
```
If one wants to modify all rules with a specific label, interface, priority and (old) preference value, one can also filter using the following method. All filtering criteria are connected with "and". Either the attribute `group` or `label` is required, all other attributes are optional. It is also valid to use both `group` and `label` at the same time.
```
<scenario>
    <at t="1">
        <update-entry module="R1.libTable" 
                      label="20017" 
                      priority="0" 
                      preference="2" 
                      outInterface="mlo0" >
            	<preference value="0" />
        </update-entry>
    </at>
</scenario>
```
The `module` attribute is required here and determines which router (strictly speaking, module) is responsible for parsing the rule. If we want to execute the `<update-entry>` on a router called `R1`, then we have to set the `module` attribute to `R1.libTable`.

The following values can be updated, where the `value` attribute must be set to the new value:

* `<preference value="..." />`
* `<priority value="..." />`

In addition to the `<update entry>` tag, there are also `<delete-entry>` and `<add-entry>`. They have similar structure to `<update-entry>` tag, but have no subtags and do not (yet) support the `group` attribute.


### Actions based on the incoming traffic
It is also possible to update forwarding entries when the router receives a special packet. This can be useful to e.g. disable entries when a link fails. Currently, it is only possible to "listen" to the top-most MPLS label and the incoming interface.

A rule can look like this (e.g., file `R3_actions.xml`):
```
<actions>
  <action label="20024" inInterface="ppp0">
    <update-entry group="4" >
      <preference value="0" />
    </update-entry>
  </action>
  <!-- more actions ... -->
</actions>
```

In the `omnetpp.ini` file, you have to load the `*_action.xml` files like this:
```
*.R3.mpls.actions = xmldoc("R3_actions.xml")
```

(There is no `module` attribute in this case, and only the `<update-entry>` tag is supported here, yet.)


## Contact
Feel free to get in contact with me.  
Nikolaus Suess &lt;nikolaus.suess@univie.ac.at&gt;
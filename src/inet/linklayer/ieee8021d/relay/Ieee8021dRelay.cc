// Copyright (C) 2013 OpenSim Ltd.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//
// Author: Benjamin Martin Seregi

#include "inet/common/IProtocolRegistrationListener.h"
#include "inet/common/LayeredProtocolBase.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/configurator/Ieee8021dInterfaceData.h"
#include "inet/linklayer/ieee8021d/relay/Ieee8021dRelay.h"
#include "inet/linklayer/ieee8022/Ieee8022LlcHeader_m.h"
#include "inet/networklayer/common/InterfaceEntry.h"

namespace inet {

Define_Module(Ieee8021dRelay);

Ieee8021dRelay::Ieee8021dRelay()
{
}

void Ieee8021dRelay::initialize(int stage)
{
    if (stage == INITSTAGE_LOCAL) {
        // statistics
        numDispatchedBDPUFrames = numDispatchedNonBPDUFrames = numDeliveredBDPUsToSTP = 0;
        numReceivedBPDUsFromSTP = numReceivedNetworkFrames = numDroppedFrames = 0;
        fcsMode = parseFcsMode(par("fcsMode"));
        isStpAware = par("hasStp");
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        NodeStatus *nodeStatus = dynamic_cast<NodeStatus *>(findContainingNode(this)->getSubmodule("status"));
        isOperational = (!nodeStatus) || nodeStatus->getState() == NodeStatus::UP;
        registerService(Protocol::ethernetMac, nullptr, gate("ifIn"));
        registerProtocol(Protocol::ethernetMac, gate("ifOut"), nullptr);

        //TODO FIX Move it at least to STP module (like in ANSA's CDP/LLDP)
        if(isStpAware) {
            registerAddress(MacAddress::STP_MULTICAST_ADDRESS);
        }
    }
    else if (stage == INITSTAGE_LINK_LAYER_2) {

        macTable = getModuleFromPar<IMacAddressTable>(par("macTableModule"), this);
        ifTable = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);

        if (isOperational) {
            ie = chooseInterface();

            if (ie) {
                bridgeAddress = ie->getMacAddress(); // get the bridge's MAC address
                registerAddress(bridgeAddress); // register bridge's MAC address
            } else
                throw cRuntimeError("No non-loopback interface found!");
        }

//        isStpAware = gate("stpIn")->isConnected();    // if the stpIn is not connected then the switch is STP/RSTP unaware

        WATCH(bridgeAddress);
        WATCH(numReceivedNetworkFrames);
        WATCH(numDroppedFrames);
        WATCH(numReceivedBPDUsFromSTP);
        WATCH(numDeliveredBDPUsToSTP);
        WATCH(numDispatchedNonBPDUFrames);
    }
}


void Ieee8021dRelay::registerAddress(MacAddress mac)
{
    registerAddresses(mac, mac);

}


void Ieee8021dRelay::registerAddresses(MacAddress startMac, MacAddress endMac)
{
    registeredMacAddresses.insert(MacAddressPair(startMac, endMac));

}

void Ieee8021dRelay::handleMessage(cMessage *msg)
{
    if (!isOperational) {
        EV_ERROR << "Message '" << msg << "' arrived when module status is down, dropped it." << endl;
        delete msg;
        return;
    }
    else if (msg->isSelfMessage())
        throw cRuntimeError("This module doesn't handle self-messages!");

    else if (msg->arrivedOn("upperLayerIn"))
    {
        handleAndDispatchFrameFromHL(check_and_cast<Packet *>(msg));
    }
    // messages from network
    else if (msg->arrivedOn("ifIn")) {
        Packet *packet = check_and_cast<Packet *>(msg);
        numReceivedNetworkFrames++;
        EV_INFO << "Received " << msg << " from network." << endl;
        delete packet->removeTagIfPresent<DispatchProtocolReq>();
        emit(packetReceivedFromLowerSignal, packet);
        handleAndDispatchFrame(packet);
    }
//    // messages from STP process
//    else if (msg->arrivedOn("stpIn")) {
//        Packet *packet = check_and_cast<Packet *>(msg);
//        numReceivedBPDUsFromSTP++;
//        EV_INFO << "Received " << msg << " from STP/RSTP module." << endl;
//        dispatchBPDU(packet);
//    }
    // unknown gate
    else
        throw cRuntimeError("Message arrived on unknown gate");
}

void Ieee8021dRelay::handleAndDispatchFrameFromHL(Packet *packet)
{
    const auto& frame = packet->peekAtFront<EthernetMacHeader>();

    InterfaceReq* interfaceReq = packet->findTag<InterfaceReq>();
    int interfaceId =
            interfaceReq == nullptr ? -1 : interfaceReq->getInterfaceId();

    if (interfaceId != -1) {
        InterfaceEntry *ie = ifTable->getInterfaceById(interfaceId);
        dispatch(packet, ie);
    } else if (frame->getDest().isBroadcast()) {    // broadcast address
        broadcast(packet, -1);
    } else {
        int outInterfaceId = macTable->getPortForAddress(frame->getDest());
        // Not known -> broadcast
        if (outInterfaceId == -1) {
            EV_DETAIL << "Destination address = " << frame->getDest()
                             << " unknown, broadcasting frame " << frame
                             << endl;
            broadcast(packet, -1);
        } else {
            InterfaceEntry *ie = ifTable->getInterfaceById(interfaceId);
            dispatch(packet, ie);

        }
    }
}

bool Ieee8021dRelay::isForwardingInterface(InterfaceEntry *ie)
{
    if (isStpAware) {
        if (!ie->ieee8021dData())
            throw cRuntimeError("Ieee8021dInterfaceData not found for interface %s", ie->getFullName());
        return ie->ieee8021dData()->isForwarding();
    }
    return true;
}

void Ieee8021dRelay::broadcast(Packet *packet, int arrivalInterfaceId)
{
    EV_DETAIL << "Broadcast frame " << packet << endl;

    auto oldPacketProtocolTag = packet->removeTag<PacketProtocolTag>();
    packet->clearTags();
    auto newPacketProtocolTag = packet->addTag<PacketProtocolTag>();
    *newPacketProtocolTag = *oldPacketProtocolTag;
    delete oldPacketProtocolTag;
    packet->trim();

    int numPorts = ifTable->getNumInterfaces();
    for (int i = 0; i < numPorts; i++) {
        InterfaceEntry *ie = ifTable->getInterface(i);
        if (ie->isLoopback() || !ie->isBroadcast())
            continue;
        if (ie->getInterfaceId() != arrivalInterfaceId && isForwardingInterface(ie)) {
            dispatch(packet->dup(), ie);
        }
    }
    delete packet;
}

namespace {
    bool isBpdu(Packet *packet, const Ptr<const EthernetMacHeader>& hdr)
    {
        if (isIeee8023Header(*hdr)) {
            const auto& llc = packet->peekDataAt<Ieee8022LlcHeader>(hdr->getChunkLength());
            return (llc->getSsap() == 0x42 && llc->getDsap() == 0x42 && llc->getControl() == 3);
        }
        else
            return false;
    }
}

void Ieee8021dRelay::handleAndDispatchFrame(Packet *packet)
{
    const auto& frame = packet->peekAtFront<EthernetMacHeader>();
    int arrivalInterfaceId = packet->getTag<InterfaceInd>()->getInterfaceId();
    InterfaceEntry *arrivalInterface = ifTable->getInterfaceById(arrivalInterfaceId);
    Ieee8021dInterfaceData *arrivalPortData = arrivalInterface->ieee8021dData();
    if (isStpAware && arrivalPortData == nullptr)
        throw cRuntimeError("Ieee8021dInterfaceData not found for interface %s", arrivalInterface->getFullName());
    learn(frame->getSrc(), arrivalInterfaceId);

    // BPDU Handling
    if (isStpAware
            && (frame->getDest() == MacAddress::STP_MULTICAST_ADDRESS || frame->getDest() == bridgeAddress)
            && arrivalPortData->getRole() != Ieee8021dInterfaceData::DISABLED
            && isBpdu(packet, frame)) {
        EV_DETAIL << "Deliver BPDU to the STP/RSTP module" << endl;
        sendUp(packet);    // deliver to the STP/RSTP module
    }
    else if (isStpAware && !arrivalPortData->isForwarding()) {
        EV_INFO << "The arrival port is not forwarding! Discarding it!" << endl;
        numDroppedFrames++;
        delete packet;
    }
    else if (in_range(registeredMacAddresses, frame->getDest())) {
        // destination MAC address is registered, send it up
        sendUp(packet);
    }
    else if (frame->getDest().isBroadcast()) {    // broadcast address
        broadcast(packet, arrivalInterfaceId);
    }
    else {
        int outputInterfaceId = macTable->getPortForAddress(frame->getDest());
        // Not known -> broadcast
        if (outputInterfaceId == -1) {
            EV_DETAIL << "Destination address = " << frame->getDest() << " unknown, broadcasting frame " << frame << endl;
            broadcast(packet, arrivalInterfaceId);
        }
        else {
            InterfaceEntry *outputInterface = ifTable->getInterfaceById(outputInterfaceId);
            if (outputInterfaceId != arrivalInterfaceId) {
                if (isForwardingInterface(outputInterface))
                    dispatch(packet, outputInterface);
                else {
                    EV_INFO << "Output interface " << outputInterface->getFullName() << " is not forwarding. Discarding!" << endl;
                    numDroppedFrames++;
                    delete packet;
                }
            }
            else {
                EV_DETAIL << "Output port is same as input port, " << packet->getFullName() << " destination = " << frame->getDest() << ", discarding frame " << frame << endl;
                numDroppedFrames++;
                delete packet;
            }
        }
    }
}

void Ieee8021dRelay::dispatch(Packet *packet, InterfaceEntry *ie)
{
    const auto& frame = packet->peekAtFront<EthernetMacHeader>();
    EV_INFO << "Sending frame " << packet << " on output interface " << ie->getFullName() << " with destination = " << frame->getDest() << endl;

    numDispatchedNonBPDUFrames++;
    auto oldPacketProtocolTag = packet->removeTag<PacketProtocolTag>();
    packet->clearTags();
    auto newPacketProtocolTag = packet->addTag<PacketProtocolTag>();
    *newPacketProtocolTag = *oldPacketProtocolTag;
    delete oldPacketProtocolTag;
    packet->addTag<InterfaceReq>()->setInterfaceId(ie->getInterfaceId());
    packet->trim();
    emit(packetSentToLowerSignal, packet);
    send(packet, "ifOut");
}

void Ieee8021dRelay::learn(MacAddress srcAddr, int arrivalInterfaceId)
{
    Ieee8021dInterfaceData *port = getPortInterfaceData(arrivalInterfaceId);

    if (!isStpAware || port->isLearning())
        macTable->updateTableWithAddress(arrivalInterfaceId, srcAddr);
}

void Ieee8021dRelay::sendUp(Packet *packet)
{

    send(packet, "upperLayerOut");
}

Ieee8021dInterfaceData *Ieee8021dRelay::getPortInterfaceData(unsigned int interfaceId)
{
    if (isStpAware) {
        InterfaceEntry *gateIfEntry = ifTable->getInterfaceById(interfaceId);
        Ieee8021dInterfaceData *portData = gateIfEntry ? gateIfEntry->ieee8021dData() : nullptr;

        if (!portData)
            throw cRuntimeError("Ieee8021dInterfaceData not found for port = %d", interfaceId);

        return portData;
    }
    return nullptr;
}

void Ieee8021dRelay::start()
{
    isOperational = true;

    ie = chooseInterface();
    if (ie)
        bridgeAddress = ie->getMacAddress(); // get the bridge's MAC address
    else
        throw cRuntimeError("No non-loopback interface found!");

    macTable->clearTable();
}

void Ieee8021dRelay::stop()
{
    isOperational = false;

    macTable->clearTable();
    ie = nullptr;
}

InterfaceEntry *Ieee8021dRelay::chooseInterface()
{
    // TODO: Currently, we assume that the first non-loopback interface is an Ethernet interface
    //       since relays work on EtherSwitches.
    //       NOTE that, we don't check if the returning interface is an Ethernet interface!
    for (int i = 0; i < ifTable->getNumInterfaces(); i++) {
        InterfaceEntry *current = ifTable->getInterface(i);
        if (!current->isLoopback())
            return current;
    }

    return nullptr;
}

void Ieee8021dRelay::finish()
{
    recordScalar("number of received BPDUs from STP module", numReceivedBPDUsFromSTP);
    recordScalar("number of received frames from network (including BPDUs)", numReceivedNetworkFrames);
    recordScalar("number of dropped frames (including BPDUs)", numDroppedFrames);
    recordScalar("number of delivered BPDUs to the STP module", numDeliveredBDPUsToSTP);
    recordScalar("number of dispatched BPDU frames to the network", numDispatchedBDPUFrames);
    recordScalar("number of dispatched non-BDPU frames to the network", numDispatchedNonBPDUFrames);
}

bool Ieee8021dRelay::handleOperationStage(LifecycleOperation *operation, int stage, IDoneCallback *doneCallback)
{
    Enter_Method_Silent();

    if (dynamic_cast<NodeStartOperation *>(operation)) {
        if (static_cast<NodeStartOperation::Stage>(stage) == NodeStartOperation::STAGE_LINK_LAYER) {
            start();
        }
    }
    else if (dynamic_cast<NodeShutdownOperation *>(operation)) {
        if (static_cast<NodeShutdownOperation::Stage>(stage) == NodeShutdownOperation::STAGE_LINK_LAYER) {
            stop();
        }
    }
    else if (dynamic_cast<NodeCrashOperation *>(operation)) {
        if (static_cast<NodeCrashOperation::Stage>(stage) == NodeCrashOperation::STAGE_CRASH) {
            stop();
        }
    }
    else {
        throw cRuntimeError("Unsupported operation '%s'", operation->getClassName());
    }

    return true;
}

} // namespace inet


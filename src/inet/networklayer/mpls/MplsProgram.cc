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

#include "MplsProgram.h"
#include "inet/debugging.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/common/XMLUtils.h"
#include "LibTable.h"

namespace inet {

Define_Module(MplsProgram);

MplsProgram::MplsProgram()
{
    // TODO Auto-generated constructor stub
}

MplsProgram::~MplsProgram()
{
    // TODO Auto-generated destructor stub
}



void MplsProgram::initialize(int stage)
{
    Mpls::initialize(stage);
    if (stage == INITSTAGE_NETWORK_LAYER) {
        // read configuration
        readActionsFromXML(par("actions"));
    }
}

void MplsProgram::handleMessage(cMessage *msg)
{
    Packet *pk = check_and_cast<Packet *>(msg);
    if (msg->getArrivalGate()->isName("lowerLayerIn")) {
        // Outgoing
        //DEBUG(ROUTERSTR(pk) "Reveived on lowerLayerIn: " << pk);
        //print_packet_tags(pk);
        int protocolId = pk->getTag<PacketProtocolTag>()->getProtocol()->getId();
        if (protocolId == Protocol::mpls.getId()) {
            int incomingInterfaceId = pk->getTag<InterfaceInd>()->getInterfaceId();
            NetworkInterface *ie = ift->getInterfaceById(incomingInterfaceId);
            std::string incomingInterfaceName = ie->getInterfaceName();
            const auto& mplsHeader = pk->peekAtFront<MplsHeader>();
            this->handleIncomingPacket(mplsHeader->getLabel(), incomingInterfaceName);
            if( !strcmp(pk->getArrivalGate()->getOwner()->getOwner()->getName(), "S") ){
                DEBUG(ROUTER_STR(pk) << "Outermost label: " << mplsHeader->getLabel())
                DEBUG(ROUTER_STR(pk) << "Incoming interface: " << incomingInterfaceName)
            }
        }
        //std::cerr << "======" << endl;
    }
    /*
    else if (msg->getArrivalGate()->isName("upperLayerIn")) {
        // Incoming
        DEBUG(ROUTER_STR(pk) "Reveived on upperLayerIn: " << pk);
        print_packet_tags(pk);
        std::cerr << "======" << endl;
    }*/
    Mpls::handleMessage(msg);
}


void MplsProgram::handleIncomingPacket(int label, std::string inInterface){
    if( this->actions.count(label) == 0 )
        return;
    for( auto& action : this->actions[label] ){
        // Check for inInterface and skip elements not fitting
        if( !action.inInterface.empty() && inInterface != action.inInterface )
            continue;

        DEBUG("EXECUTING COMMAND");
        DEBUG("Label: "<<action.label<<", outInterface:"<<action.outInterface<<", priority="<<action.priority
                << ", group="<<action.group<<", preference="<<action.preference<<", new preference="<<action.new_preference
                << ", new priority="<<action.new_priority)

        this->lt->processCommand_updateEntry(action.label, action.outInterface, action.priority, action.group,
                action.preference, action.new_preference, action.new_priority);
    }
}



/**
 * Actions are of the form:
 * <actions>
 *  <action label="36" inInterface="ppp1"><!-- inInterface is optional! -->
 *      <!-- update-entry tag -->
 *  </action>
 * </actions>
 */
void MplsProgram::readActionsFromXML(const cXMLElement *actions){
    using namespace xmlutils;

    ASSERT(actions);
    ASSERT(!strcmp(actions->getTagName(), "actions"));
    checkTags(actions, "action");
    cXMLElementList list = actions->getChildrenByTagName("action");
    for (auto& elem : list) {
        // Label attribute
        if( !elem->getAttribute("label") || atoi(elem->getAttribute("label")) == 0 ){
            throw cRuntimeError("Label attribute is mandatory");
        }
        int label = atoi(elem->getAttribute("label"));

        // inInterface attribute
        std::string inInterface;
        if( elem->getAttribute("inInterface") && strlen(elem->getAttribute("label")) ){
            inInterface = std::string(elem->getAttribute("inInterface"));
        }

        // Check sub-tags
        checkTags(elem, "update-entry");
        cXMLElementList utaglist = elem->getChildrenByTagName("update-entry");

        for(auto& updateEntryTag : utaglist){
            Action action;
            action.inInterface = inInterface;

            this->readActionsFromXML_update_entry(*updateEntryTag, action);
            // Add action
            this->actions[label].push_back(action);
        }
    }
}

void MplsProgram::readActionsFromXML_update_entry(const cXMLElement& node, Action& a){
    // Either label or group attribute is mandatory
        if( ! node.getAttribute("label") && ! node.getAttribute("group") ){
            EV_ERROR << "<update-entry> tag invalid." << endl;
            assert(false);
            return;
        }
        int label = LibTable::INVALID_LABEL;
        if( node.getAttribute("label") )
            label = atoi(node.getAttribute("label"));
        a.label = label;

        // optional attributes for matching ...
        const char* s_priority    = node.getAttribute("priority");
        const char* s_preference  = node.getAttribute("preference");
        const char* s_group_id    = node.getAttribute("group");
        const char* out_interface = node.getAttribute("outInterface");
        bool priority_ok   = s_priority && *s_priority != '\0';
        bool preference_ok = s_preference && *s_preference != '\0';
        bool group_ok      = s_group_id && *s_group_id != '\0';

        a.group = group_ok ? atoi(s_group_id) : LibTable::INVALID_GROUP;
        a.outInterface = out_interface ? std::string{out_interface} : std::string{};
        a.preference = preference_ok ? atof(s_preference) : LibTable::INVALID_PREFERENCE;
        a.priority = priority_ok ? atoi(s_priority) : LibTable::INVALID_PRIORITY;

        const cXMLElement *preference_tag = xmlutils::getUniqueChildIfExists(&node, "preference");
        a.new_preference = LibTable::INVALID_PREFERENCE;
        if(preference_tag && preference_tag->getAttribute("value"))
            a.new_preference = atof(preference_tag->getAttribute("value"));


        const cXMLElement *priority_tag = xmlutils::getUniqueChildIfExists(&node, "priority");
        a.new_priority = LibTable::INVALID_PRIORITY;
        if(priority_tag && priority_tag->getAttribute("value"))
            a.new_priority = atoi(priority_tag->getAttribute("value"));
}

} /* namespace inet */

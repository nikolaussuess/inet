//
// Copyright (C) 2005 Vojtech Janota
// Copyright (C) 2003 Xuan Thang Nguyen
// Modifications by Nikolaus Suess in 2022-23.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

#include "inet/networklayer/mpls/LibTable.h"

#include <iostream>
#include <algorithm>

#include "inet/common/XMLUtils.h"

#include "inet/debugging.h"
#include "inet/networklayer/common/InterfaceTable.h"
#include "inet/networklayer/common/NetworkInterface.h"
#include <random>

namespace inet {

Define_Module(LibTable);

void LibTable::initialize(int stage)
{
    cSimpleModule::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        maxLabel = 0;
        WATCH_VECTOR(lib);
    }
    else if (stage == INITSTAGE_NETWORK_LAYER) {
        // read configuration
        readTableFromXML(par("config"));
    }
}

void LibTable::handleMessage(cMessage *)
{
    ASSERT(false);
}

/**
 * Checks if the interface of the forwarding entry is up (TRUE) or down (FALSE).
 */
bool LibTable::isInterfaceUp(const std::string& ifname){
    const cModule* router = this->getParentModule();
    const InterfaceTable* ift = (InterfaceTable*)CHK(router->getModuleByPath(".interfaceTable"));
    int nr_interfaces = ift->getNumInterfaces();
    for( int i = 0; i < nr_interfaces; ++i ){
        const NetworkInterface* interface = ift->getInterface(i);
        //std::cout << "Name: " << interface->getInterfaceName() << " ";
        //std::cout << interface->getInterfaceId() << " status = " << interface->isUp() << std::endl;
        if( !strcmp(interface->getInterfaceName(), ifname.c_str() ) ){
            return interface->isUp();
        }
    }

    return false;

}

/**
 * Get entry from LIB table.
 * Modified.
 */
bool LibTable::resolveLabel(std::string inInterface, int inLabel,
        LabelOpVector& outLabel, std::string& outInterface, int& color)
{
    bool any = (inInterface.length() == 0);
    any = true; // TODO: fix

    // Random number generator
    std::random_device rd;
    std::mt19937 gen(rd());

    for (auto& elem : lib) {
        if (!any && elem.inInterface != inInterface)
            continue;

        if (elem.inLabel != inLabel)
            continue;

        /*
        DEBUG("Resolving label from " << std::to_string(inLabel) << " to");
        for(const auto& fwe : elem.entries){
            DEBUG("- Entry with");
            DEBUG("  out_if:   " << fwe.outInterface)
            DEBUG("  priority: " << fwe.priority);
            for( const auto& e : fwe.outLabel )
                DEBUG("   * " << std::to_string(e.optcode) << " "<< std::to_string(e.label));
        }*/

        // Filter interfaces to get only those that are up.
        std::vector<ForwardingEntry> valid_entries;
        std::copy_if(elem.entries.begin(), elem.entries.end(), std::back_inserter(valid_entries), [this](const auto&e){
            return this->isInterfaceUp(e.outInterface);
        });

        // Find entry with lowest priority
        auto it = std::min_element(valid_entries.begin(), valid_entries.end(), [](const auto& e1, const auto& e2){
            return e1.priority < e2.priority;
        });

        if( it == valid_entries.end())
            return false;

        // Implementation of ECMP
        // We allow a weighted ECMP, i.e. the preference attribute can be used to load balance the
        // traffic among entries with the same priority.
        int min_priority = it->priority;
        std::vector<ForwardingEntry> minimum_entries;
        std::copy_if(valid_entries.begin(), valid_entries.end(), std::back_inserter(minimum_entries), [min_priority](const auto&e){
           return e.priority == min_priority;
        });

        std::vector<int> preferences (minimum_entries.size());
        std::transform(minimum_entries.begin(), minimum_entries.end(), preferences.begin(), [](const auto&e){
            return e.preference;
         });

        std::discrete_distribution<> d(preferences.begin(), preferences.end());

        it = minimum_entries.begin();
        std::advance( it, d(gen) );
        // END ECMP CODE

        outLabel = it->outLabel;
        outInterface = it->outInterface;
        EV_INFO << "Label resolved to ("<<outLabel <<","<<outInterface<<")" << endl;

        color = elem.color;

        return true;
    }
    return false;
}

// NOTE: Modified.
// Now, it does not overwrite an entry if it already exists but instead it adds an additional one.
int LibTable::installLibEntry(int inLabel, std::string inInterface, const LabelOpVector& outLabel,
        std::string outInterface, int color, int priority /* = 0 */, int preference /* = 1 */ )
{
    if (inLabel == -1) {
        LibEntry newItem;
        newItem.inLabel = ++maxLabel;
        newItem.inInterface = inInterface;

        ForwardingEntry fwe { outLabel, outInterface, priority };
        newItem.entries.push_back(fwe);
        newItem.color = color;
        lib.push_back(newItem);
        return newItem.inLabel;
    }
    else {
        for (auto& elem : lib) {
            //if (elem.inLabel != inLabel)
            if (elem.inLabel != inLabel /* || elem.inInterface != inInterface ???*/)
                continue;

            ForwardingEntry fwe { outLabel, outInterface, priority, preference };
            elem.entries.push_back(fwe);
            elem.color = color;
            return inLabel;
        }
        ASSERT(false);
        return 0; // prevent warning
    }
}

void LibTable::removeLibEntry(int inLabel)
{
    for (unsigned int i = 0; i < lib.size(); i++) {
        if (lib[i].inLabel != inLabel)
            continue;

        lib.erase(lib.begin() + i);
        return;
    }
    ASSERT(false);
}

// Modified: Allow priority tag
void LibTable::readTableFromXML(const cXMLElement *libtable)
{
    using namespace xmlutils;

    ASSERT(libtable);
    ASSERT(!strcmp(libtable->getTagName(), "libtable"));
    checkTags(libtable, "libentry");
    cXMLElementList list = libtable->getChildrenByTagName("libentry");
    for (auto& elem : list) {
        const cXMLElement& entry = *elem;

        checkTags(&entry, "inLabel inInterface outLabel outInterface color priority preference");

        LibEntry newItem;
        newItem.inLabel = getParameterIntValue(&entry, "inLabel");
        newItem.inInterface = getParameterStrValue(&entry, "inInterface");
        newItem.color = getParameterIntValue(&entry, "color", 0);

        ForwardingEntry fwe {
            {}, // LabelOpVector outLabel
            getParameterStrValue(&entry, "outInterface"),
            getParameterIntValue(&entry, "priority", 0),
            getParameterIntValue(&entry, "preference", 1),
        };

        cXMLElementList ops = getUniqueChild(&entry, "outLabel")->getChildrenByTagName("op");
        for (auto& ops_oit : ops) {
            const cXMLElement& op = *ops_oit;
            const char *val = op.getAttribute("value");
            const char *code = op.getAttribute("code");
            ASSERT(code);
            LabelOp l;

            if (!strcmp(code, "push")) {
                l.optcode = PUSH_OPER;
                ASSERT(val);
                l.label = atoi(val);
                ASSERT(l.label > 0);
            }
            else if (!strcmp(code, "pop")) {
                l.optcode = POP_OPER;
                ASSERT(!val);
            }
            else if (!strcmp(code, "swap")) {
                l.optcode = SWAP_OPER;
                ASSERT(val);
                l.label = atoi(val);
                ASSERT(l.label > 0);
            }
            else
                ASSERT(false);

            fwe.outLabel.push_back(l);
        }

        auto old_entry = std::find_if(lib.begin(), lib.end(), [&newItem](const LibEntry& entry){
            return entry == newItem;
        });

        if( old_entry == lib.end() ){
            // There is no entry, yet.
            newItem.entries.push_back(fwe);
            lib.push_back(newItem);
        }
        else {
            // LIB entry already exists, we just add a new forwarding rule.
            old_entry->entries.push_back(fwe);
        }

        ASSERT(newItem.inLabel > 0);

        if (newItem.inLabel > maxLabel)
            maxLabel = newItem.inLabel;
    }
}

LabelOpVector LibTable::pushLabel(int label)
{
    LabelOpVector vec;
    LabelOp lop;
    lop.optcode = PUSH_OPER;
    lop.label = label;
    vec.push_back(lop);
    return vec;
}

LabelOpVector LibTable::swapLabel(int label)
{
    LabelOpVector vec;
    LabelOp lop;
    lop.optcode = SWAP_OPER;
    lop.label = label;
    vec.push_back(lop);
    return vec;
}

LabelOpVector LibTable::popLabel()
{
    LabelOpVector vec;
    LabelOp lop;
    lop.optcode = POP_OPER;
    lop.label = 0;
    vec.push_back(lop);
    return vec;
}

/**
 * Compare by inLabel and inInterface.
 */
bool operator==(const LibTable::LibEntry& lhs, const LibTable::LibEntry& rhs){
    return lhs.inLabel == rhs.inLabel && lhs.inInterface == rhs.inInterface;
}
bool operator!=(const LibTable::LibEntry& lhs, const LibTable::LibEntry& rhs){
    return !(lhs == rhs);
}


std::ostream& operator<<(std::ostream& os, const LabelOpVector& label)
{
    os << "{";
    for (unsigned int i = 0; i < label.size(); i++) {
        switch (label[i].optcode) {
            case PUSH_OPER:
                os << "PUSH " << label[i].label;
                break;

            case SWAP_OPER:
                os << "SWAP " << label[i].label;
                break;

            case POP_OPER:
                os << "POP";
                break;

            default:
                ASSERT(false);
                break;
        }

        if (i < label.size() - 1)
            os << "; ";
        else
            os << "}";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const LibTable::LibEntry& lib)
{
    os << "inLabel:" << lib.inLabel;
    os << "    inInterface:" << lib.inInterface;
    //os << "    outLabel:" << lib.outLabel;
    //os << "    outInterface:" << lib.outInterface;
    os << "    color:" << lib.color;
    os << "    entries: [";
    for( const auto& e : lib.entries){
        os << "[";
        os << "    outLabel:" << e.outLabel;
        os << "    outInterface:" << e.outInterface;
        os << "    priority:" << e.priority;
        os << "],";
    }
    os << "    ]";
    return os;
}

} // namespace inet


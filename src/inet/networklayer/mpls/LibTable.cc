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
        // And filter out entries where its preferences = 0.0f
        std::vector<ForwardingEntry> valid_entries;
        std::copy_if(elem.entries.begin(), elem.entries.end(), std::back_inserter(valid_entries), [this](const auto&e){
            return this->isInterfaceUp(e.outInterface) && e.preference != 0.0;
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

        std::vector<float> preferences (minimum_entries.size());
        std::transform(minimum_entries.begin(), minimum_entries.end(), preferences.begin(), [](const auto&e){
            return e.preference;
         });

        std::discrete_distribution<int> d(preferences.begin(), preferences.end());

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
        std::string outInterface, int color, int priority /* = 0 */, float preference /* = 1.0f */ )
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
        processNewXmlEntry(*elem);
    }
}

void LibTable::processNewXmlEntry(const cXMLElement &entry){
    using namespace xmlutils;

    checkTags(&entry, "inLabel inInterface outLabel outInterface color priority preference groups");

    LibEntry newItem;
    newItem.inLabel = getParameterIntValue(&entry, "inLabel");
    newItem.inInterface = getParameterStrValue(&entry, "inInterface");
    newItem.color = getParameterIntValue(&entry, "color", 0);

    std::set<int> groups {};
    if( auto child = getUniqueChildIfExists(&entry, "groups") ){
        checkTags(child, "group");
        cXMLElementList ids = child->getChildrenByTagName("group");
        for( auto& id : ids ){
            if(! id->getAttribute("id") )
                continue;
            groups.insert( atoi(id->getAttribute("id")) );
        }
    }

    ForwardingEntry fwe {
        {}, // LabelOpVector outLabel
        getParameterStrValue(&entry, "outInterface"),
        getParameterIntValue(&entry, "priority", DEFAULT_PRIORITY),
        (float)getParameterDoubleValue(&entry, "preference", DEFAULT_PREFERENCE),
        groups,
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
bool operator==(const LibTable::LibEntry& lhs, const LibTable::LibEntry& rhs)
{
    return lhs.inLabel == rhs.inLabel && lhs.inInterface == rhs.inInterface;
}
bool operator!=(const LibTable::LibEntry& lhs, const LibTable::LibEntry& rhs)
{
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

std::ostream& operator<<(std::ostream& os, const LibTable::ForwardingEntry& e)
{
    os << "[";
    os << "    outLabel:" << e.outLabel;
    os << "    outInterface:" << e.outInterface;
    os << "    priority:" << e.priority;
    os << "    preference:" << e.preference;
    os << "],";
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
        os << "    preference:" << e.preference;
        os << "],";
    }
    os << "    ]";
    return os;
}

void LibTable::processCommand(const cXMLElement& node)
{
    if (!strcmp(node.getTagName(), "update-entry")) {
        // Update an existing LibEntry
        processCommand_updateEntry(node);
    }
    else if(!strcmp(node.getTagName(), "delete-entry")){
        // Delete all entries matching ...
        processCommand_deleteEntry(node);
    }
    else if(!strcmp(node.getTagName(), "add-entry")){
        // Add a new entry to the table.
        processNewXmlEntry(node);
        EV_INFO << "Added 1 new rule." << endl;
    }
    else {
        assert(false);
    }
    return;
}


void LibTable::processCommand_updateEntry(const cXMLElement& node)
{
    // Either label or group attribute is mandatory
    if( ! node.getAttribute("label") && ! node.getAttribute("group") ){
        EV_ERROR << "<update-entry> tag invalid." << endl;
        assert(false);
        return;
    }
    int label = INVALID_LABEL;
    if( node.getAttribute("label") )
        label = atoi(node.getAttribute("label"));

    // optional attributes for matching ...
    const char* s_priority    = node.getAttribute("priority");
    const char* s_preference  = node.getAttribute("preference");
    const char* s_group_id    = node.getAttribute("group");
    const char* out_interface = node.getAttribute("outInterface");
    bool priority_ok   = s_priority && *s_priority != '\0';
    bool preference_ok = s_preference && *s_preference != '\0';
    bool group_ok      = s_group_id && *s_group_id != '\0';

    const cXMLElement *preference_tag = xmlutils::getUniqueChildIfExists(&node, "preference");
    // If it is not given, assume 0, which means "do not choose".
    float new_preference = DEFAULT_PREFERENCE;
    if(preference_tag && preference_tag->getAttribute("value"))
        new_preference = atof(preference_tag->getAttribute("value"));

    const cXMLElement *priority_tag = xmlutils::getUniqueChildIfExists(&node, "priority");
    // If it is not given, assume 0
    int new_priority = DEFAULT_PRIORITY;
    if(priority_tag && priority_tag->getAttribute("value"))
        new_priority = atoi(priority_tag->getAttribute("value"));

    // TODO: Rewrite to call other processCommand_updateEntry() in order to avoid code duplication
    for( auto& libentry : this->lib ){
        // Skip entries where the label does not match the attribute iff it exists.
        if( label != 0 && label != libentry.inLabel )
            continue;

        for( auto& entry : libentry.entries ){
            if( out_interface && strcmp(out_interface, entry.outInterface.c_str()) )
                continue;
            if( priority_ok && atoi(s_priority) != entry.priority)
                continue;
            if( group_ok && entry.groups.count( atoi(s_group_id) ) == 0 )
                continue;
            if( preference_ok && fabs(atof(s_preference) - entry.preference) > FLOAT_EPS )
                continue;

            if( preference_tag )
                entry.preference = new_preference;

            if( priority_tag )
                entry.priority = new_priority;

            EV_INFO << "Rule with inLabel " << libentry.inLabel << " updated." << endl;
        }
    }
}

// TODO: Replace INVALID_... by std::optional or similar
void LibTable::processCommand_updateEntry(int label, const std::string& outInterface, int priority,
        int group, float preference, float new_preference, int new_priority){
    for( auto& libentry : this->lib ){
        // Skip entries where the label does not match the attribute iff it exists.
        if( label != 0 && label != libentry.inLabel )
            continue;

        for( auto& entry : libentry.entries ){
            if( !outInterface.empty() && outInterface != entry.outInterface )
                continue;
            if( priority != INVALID_PRIORITY && priority != entry.priority)
                continue;
            if( group != INVALID_GROUP && entry.groups.count( group ) == 0 )
                continue;
            if( preference != INVALID_PREFERENCE && fabs(preference - entry.preference) > FLOAT_EPS )
                continue;

            if( new_preference != INVALID_PREFERENCE )
                entry.preference = new_preference;

            if( new_priority != INVALID_PRIORITY )
                entry.priority = new_priority;

            EV_INFO << "Rule with inLabel " << libentry.inLabel << " updated." << endl;
            EV_INFO << entry << " -- " << new_preference << " ... " << INVALID_PREFERENCE << endl;
        }
    }
}


void LibTable::processCommand_deleteEntry(const cXMLElement& node)
{
    // Mandatory:
    if( !node.getAttribute("label") ){
        EV_ERROR << "<delete-entry> tag invalid." << endl;
        ASSERT(false);
        return;
    }

    int label = atoi(node.getAttribute("label"));
    if(label <= 0){
        EV_ERROR << "Invalid label in <delete-entry> tag." << endl;
        ASSERT(false);
        return;
    }

    // Optional matching
    const char* out_interface = node.getAttribute("outInterface");
    const char* s_priority    = node.getAttribute("priority");
    const char* s_preference  = node.getAttribute("preference");
    bool out_if_ok = out_interface && *out_interface != '\0';
    bool priority_ok = s_priority && *s_priority != '\0';
    bool preference_ok = s_preference && *s_preference != '\0';

    const auto& it = std::find_if(this->lib.begin(), this->lib.end(), [label](const LibEntry& e){ return e.inLabel == label;});
    if( it == this->lib.end()){
        EV_ERROR << "Label not found ("<<label << ") in <delete-entry> tag." << endl;
        ASSERT(false);
        return;
    }

    if( !out_if_ok && !priority_ok && !preference_ok ){
        // Delete full entry, because we only got the label ...
        this->lib.erase(it);
        EV_INFO << "Deleted entry with label " << label << endl;
        return;
    }

    LibEntry& entry = *it;

    entry.entries.erase(std::remove_if( entry.entries.begin(), entry.entries.end(), [&](const ForwardingEntry& e){
        if( out_if_ok && strcmp(out_interface, e.outInterface.c_str()) )
            return false;
        if( priority_ok && atoi(s_priority) != e.priority)
            return false;
        if( preference_ok && fabs(atof(s_preference) - e.preference) > 1E-8 )
            return false;
        EV_INFO << "Deleted 1 rule: [label="<<label
                << ", outIf=" << e.outInterface
                << ", outLabel=" << e.outLabel
                << ", prio=" << e.priority
                << ", pref=" << e.preference
                << "]" << endl;
        return true;
    } ), entry.entries.end());

}

} // namespace inet


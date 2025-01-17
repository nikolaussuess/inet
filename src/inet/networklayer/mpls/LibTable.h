//
// Copyright (C) 2005 Vojtech Janota
// Copyright (C) 2003 Xuan Thang Nguyen
// Modifications by Nikolaus Suess in 2022-23
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

#ifndef __INET_LIBTABLE_H
#define __INET_LIBTABLE_H

#include <string>
#include <vector>

#include "inet/networklayer/contract/ipv4/Ipv4Address.h"
#include "inet/networklayer/ipv4/Ipv4Header_m.h"
#include "inet/networklayer/mpls/ConstType.h"
#include "inet/common/scenario/IScriptable.h"

namespace inet {

enum LabelOpCode {
    PUSH_OPER,
    SWAP_OPER,
    POP_OPER
};

struct LabelOp
{
    int label;
    LabelOpCode optcode;
};

typedef std::vector<LabelOp> LabelOpVector;

/**
 * Represents the Label Information Base (LIB) for MPLS.
 */
class INET_API LibTable : public cSimpleModule, public IScriptable
{
  public:

    struct ForwardingEntry {
        LabelOpVector outLabel;
        std::string outInterface;

        int priority = 0;
        float preference = 1;
    };

    struct LibEntry {
        int inLabel;
        std::string inInterface;

        std::vector<ForwardingEntry> entries;

        // FIXME colors in nam, temporary solution
        int color;
    };

    static constexpr int   DEFAULT_PRIORITY   = 0;
    static constexpr float DEFAULT_PREFERENCE = 1.0f;

  protected:
    int maxLabel;
    std::vector<LibEntry> lib;

  protected:
    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void handleMessage(cMessage *msg) override;

    // static configuration
    virtual void readTableFromXML(const cXMLElement *libtable);
    virtual void processNewXmlEntry(const cXMLElement &entry);
    // Check if the interface called "ifname" is up (true) or down (false)
    bool isInterfaceUp(const std::string& ifname);
    // Process the <update-entry /> tag from scenario.xml to update a LibTable entry.
    virtual void processCommand_updateEntry(const cXMLElement& node);
    // Process the <delete-entry /> tag from scenario.xml to delete an entry from the LibTable.
    virtual void processCommand_deleteEntry(const cXMLElement& node);

  public:
    // label management
    virtual bool resolveLabel(std::string inInterface, int inLabel,
            LabelOpVector& outLabel, std::string& outInterface, int& color);

    virtual int installLibEntry(int inLabel, std::string inInterface, const LabelOpVector& outLabel,
            std::string outInterface, int color, int priority = 0, float preference = 1.0f);

    virtual void removeLibEntry(int inLabel);

    // process scenario.xml
    virtual void processCommand(const cXMLElement& node) override;

    // utility
    static LabelOpVector pushLabel(int label);
    static LabelOpVector swapLabel(int label);
    static LabelOpVector popLabel();
};

bool operator==(const LibTable::LibEntry& lhs, const LibTable::LibEntry& rhs);
bool operator!=(const LibTable::LibEntry& lhs, const LibTable::LibEntry& rhs);

std::ostream& operator<<(std::ostream& os, const LibTable::LibEntry& lib);
std::ostream& operator<<(std::ostream& os, const LabelOpVector& label);

} // namespace inet

#endif


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

#ifndef INET_NETWORKLAYER_MPLS_MPLSPROGRAM_H_
#define INET_NETWORKLAYER_MPLS_MPLSPROGRAM_H_

#include <omnetpp/csimplemodule.h>
#include "inet/networklayer/mpls/Mpls.h"
#include <map>

namespace inet {

class MplsProgram:
        public Mpls
{
protected:
    struct Action {
        std::string inInterface;

        int label;
        int group;
        std::string outInterface;
        int preference;
        int priority;

        // -1 means "do not update"
        float new_preference = -1.0f;
        int new_priority     = -1;
    };
private:
    std::map<int, std::vector<Action>> actions;
    void readActionsFromXML_update_entry(const cXMLElement& node, Action& action);
public:
    MplsProgram();
    virtual ~MplsProgram();
    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void readActionsFromXML(const cXMLElement *actions);
    virtual void handleIncomingPacket(int label, std::string inInterface);
};

} /* namespace inet */

#endif /* INET_NETWORKLAYER_MPLS_MPLSPROGRAM_H_ */

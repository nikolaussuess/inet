#ifndef DEBUGGING_H_INCLUDED
#define DEBUGGING_H_INCLUDED
//
// Copyright (C) 2023 Nikolaus Suess
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// For debugging purposes only.

#include <type_traits>

// Debugging message.
#define DEBUG(x) \
    std::cerr << __FILE__ << ":" << __LINE__ << ":" << __func__ << ": " << x << std::endl;

// Allow to print the router name in debugging messages.
#define IS_OF_TYPE(variable, type, todo) \
        (std::is_same<decltype(variable), type>::value ? variable->todo : "")

#define ROUTER_STR(packet) \
    "[ROUTER " << \
      IS_OF_TYPE(packet, Packet*, getArrivalGate()->getOwner()->getOwner()->getName()) << \
      "]: "

// Print the packet tags.
#include "inet/common/packet/Packet.h"
inline void print_packet_tags(inet::Packet *msg){
    auto tags = msg->getTags();
    auto num_tags = tags.getNumTags();
    for( int i = 0; i < num_tags; ++i){
        DEBUG(tags.getTag(i))
    }
}

#endif

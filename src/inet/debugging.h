#ifndef DEBUGGING_H_INCLUDED
#define DEBUGGING_H_INCLUDED
// File by Nikolaus Suess for debugging purposes

#include <type_traits>

#define DEBUG(x) \
    std::cerr << __FILE__ << ":" << __LINE__ << ":" << __func__ << ": " << x << std::endl;

#define IS_OF_TYPE(variable, type, todo) \
        (std::is_same<decltype(variable), type>::value ? variable->todo : "")

#define ROUTER_STR(packet) \
    "[ROUTER " << \
      IS_OF_TYPE(packet, Packet*, getArrivalGate()->getOwner()->getOwner()->getName()) << \
      "]: "

#endif

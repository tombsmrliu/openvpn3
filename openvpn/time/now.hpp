#ifndef OPENVPN_TIME_NOW_H
#define OPENVPN_TIME_NOW_H

#include <openvpn/time/time.hpp>

namespace openvpn {
  namespace time {

    volatile Time::base_type now = 0; /* GLOBAL */

  } // namespace time
} // namespace openvpn

#endif // OPENVPN_TIME_NOW_H
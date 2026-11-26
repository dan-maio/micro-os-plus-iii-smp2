/*
 * wifi-config.hpp -- what smp-wifi associates with.
 *
 * No credentials live in the repository.  They come from the cmake line, the
 * same cache entries as the MangoPi MQ-Pro's wifi-net:
 *
 *   cmake ... -DBOARD=visionfive2-lite \
 *             -DWIFI_TEST_SSID=myssid -DWIFI_TEST_PASS=mypassphrase \
 *             [-DWIFI_TEST_GATEWAY=192.168.1.1] [-DWIFI_TEST_PEER=192.168.1.20]
 *
 * A build without them still brings the radio up and scans, and fails at
 * the join step with that reason in the log.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#ifndef WIFI_TEST_SSID
#define WIFI_TEST_SSID ""
#endif

// Empty = an open network.
#ifndef WIFI_TEST_PASS
#define WIFI_TEST_PASS ""
#endif

// What to ping: "gw" is the gateway DHCP handed out; a dotted address
// overrides it.  A reply proves the round trip through the access point.
#ifndef WIFI_TEST_GATEWAY
#define WIFI_TEST_GATEWAY "gw"
#endif

// Optional second target (a PC on the LAN).  Empty string = skipped.
#ifndef WIFI_TEST_PEER
#define WIFI_TEST_PEER ""
#endif

namespace wifi_test
{
  inline constexpr const char* ssid = WIFI_TEST_SSID;
  inline constexpr const char* passphrase = WIFI_TEST_PASS;
  inline constexpr const char* gateway = WIFI_TEST_GATEWAY;
  inline constexpr const char* peer = WIFI_TEST_PEER;
  inline constexpr bool configured = (WIFI_TEST_SSID[0] != '\0');
} // namespace wifi_test

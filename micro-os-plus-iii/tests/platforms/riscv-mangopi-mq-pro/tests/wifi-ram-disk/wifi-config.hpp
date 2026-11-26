/*
 * wifi-config.hpp -- what the wifi-net and wifi-ram-disk tests associate with.
 *
 * The values come from the build (-DWIFI_TEST_SSID=..., -DWIFI_TEST_PASS=...)
 * so no network credentials live in the repository.  A build without them
 * still runs: it brings the radio up, scans, and reports what it sees, but
 * the join step is skipped and the run is reported as SKIP-to-FAIL with the
 * reason in the log.
 */
#pragma once

#ifndef WIFI_TEST_SSID
#define WIFI_TEST_SSID ""
#endif

#ifndef WIFI_TEST_PASS
#define WIFI_TEST_PASS ""
#endif

// The router to ping: DHCP hands out the address, and a reply from the
// gateway proves the frame made the full round trip through the AP.
#ifndef WIFI_TEST_GATEWAY
#define WIFI_TEST_GATEWAY "192.168.1.1"
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

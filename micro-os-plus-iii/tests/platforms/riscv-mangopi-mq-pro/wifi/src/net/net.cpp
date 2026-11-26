// net.cpp - lwIP glue: network interface on the Wi-Fi link, DHCP, ping and
// the TCP echo server.
#include "net.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <span>

#include "console.hpp"
#include "wifi/platform.hpp"
#include "wifi/wlan.hpp"

extern "C" {
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/init.h"
#include "lwip/ip4.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
}

extern "C" {
extern int rtw_debug;
}

// ---- lwIP system hooks (NO_SYS) -------------------------------------------
extern "C" u32_t sys_now(void) { return mqpro::platform::millis(); }

extern "C" unsigned int net_random(void) {
    // xorshift64*, seeded from the timer; used for DHCP xids, DNS ids and
    // TCP initial sequence numbers.
    static std::uint64_t state = 0;
    if (state == 0) {
        state = plat_time_us() * 0x9e3779b97f4a7c15ull + 0x2545f4914f6cdd1dull;
    }
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return static_cast<unsigned int>((state * 0x2545f4914f6cdd1dull) >> 32);
}

namespace mqpro::net {

namespace {

constinit bool initialised = false;
constinit netif wl0{};
constinit bool announced = false;

alignas(8) constinit std::array<std::uint8_t, 1600> tx_frame{};

void print_ip(const ip4_addr_t& address) noexcept {
    kprintf("%u.%u.%u.%u", ip4_addr1(&address), ip4_addr2(&address),
            ip4_addr3(&address), ip4_addr4(&address));
}

// ------------------------------------------------------------ interface
unsigned tx_frames = 0;
unsigned rx_frames = 0;

void trace_frame(const char* direction, const std::uint8_t* frame, std::size_t length) noexcept {
    if (rtw_debug == 0 || length < 14) {
        return;
    }
    const unsigned ethertype = static_cast<unsigned>((frame[12] << 8) | frame[13]);
    if (ethertype == 0x0806 && length >= 42) {          // ARP
        serial.print("[NET] {} ARP {}.{}.{}.{} ({:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}) "
                     "-> {}.{}.{}.{}\r\n",
                     direction, frame[28], frame[29], frame[30], frame[31],
                     frame[22], frame[23], frame[24], frame[25], frame[26], frame[27],
                     frame[38], frame[39], frame[40], frame[41]);
    } else if (ethertype == 0x0800 && length >= 34) {    // IPv4
        const unsigned proto = frame[23];
        const unsigned sport = static_cast<unsigned>((frame[34] << 8) | frame[35]);
        const unsigned dport = static_cast<unsigned>((frame[36] << 8) | frame[37]);
        if (proto == 17) {
            serial.print("[NET] {} UDP {}.{}.{}.{}:{} -> {}.{}.{}.{}:{}\r\n", direction,
                         frame[26], frame[27], frame[28], frame[29], sport,
                         frame[30], frame[31], frame[32], frame[33], dport);
        } else {
            serial.print("[NET] {} IP proto {} {}.{}.{}.{} -> {}.{}.{}.{}\r\n", direction,
                         proto, frame[26], frame[27], frame[28], frame[29],
                         frame[30], frame[31], frame[32], frame[33]);
        }
    } else if (ethertype == 0x888e) {
        serial.print("[NET] {} EAPOL len {}\r\n", direction, length);
    }
}

err_t link_output(netif*, pbuf* p) {
    if (p->tot_len > tx_frame.size()) {
        return ERR_BUF;
    }
    pbuf_copy_partial(p, tx_frame.data(), p->tot_len, 0);
    ++tx_frames;
    trace_frame("tx", tx_frame.data(), p->tot_len);
    return wlan::send_ethernet({tx_frame.data(), p->tot_len}) ? ERR_OK : ERR_IF;
}

err_t interface_init(netif* interface) {
    interface->name[0] = 'w';
    interface->name[1] = 'l';
    interface->hostname = "mangopi-mq-pro";
    interface->output = etharp_output;
    interface->linkoutput = link_output;
    interface->mtu = 1500;
    interface->hwaddr_len = ETH_HWADDR_LEN;
    std::memcpy(interface->hwaddr, wlan::address().data(), ETH_HWADDR_LEN);
    interface->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

void on_frame(std::span<const std::uint8_t> frame) noexcept {
    ++rx_frames;
    trace_frame("rx", frame.data(), frame.size());
    pbuf* p = pbuf_alloc(PBUF_RAW, static_cast<u16_t>(frame.size()), PBUF_POOL);
    if (p == nullptr) {
        return;
    }
    pbuf_take(p, frame.data(), static_cast<u16_t>(frame.size()));
    if (wl0.input(p, &wl0) != ERR_OK) {
        pbuf_free(p);
    }
}

// True when the address came from `wifi ip` instead of DHCP.
constinit bool static_config = false;

// Diagnostic switch: with DHCP off the join produces no broadcast traffic at
// all (no DHCP DISCOVER and no gratuitous ARP), so single group frames can be
// tested without tripping the AP's TKIP MIC countermeasures.
constinit bool dhcp_enabled = true;

void on_link(bool up) noexcept {
    if (up) {
        std::memcpy(wl0.hwaddr, wlan::address().data(), ETH_HWADDR_LEN);
        announced = false;
        if (!dhcp_enabled) {
            wl0.flags = static_cast<u8_t>(wl0.flags & ~NETIF_FLAG_ETHARP);
        }
        netif_set_link_up(&wl0);
        if (!dhcp_enabled) {
            wl0.flags = static_cast<u8_t>(wl0.flags | NETIF_FLAG_ETHARP);
            serial.write("[NET] link up, DHCP disabled\r\n");
            return;
        }
        serial.write("[NET] link up, starting DHCP\r\n");
        static_config = false;
        dhcp_start(&wl0);
    } else {
        dhcp_release_and_stop(&wl0);
        netif_set_link_down(&wl0);
        serial.write("[NET] link down\r\n");
    }
}

void on_status(netif* interface) {
    if (ip4_addr_isany_val(*netif_ip4_addr(interface)) || announced) {
        return;
    }
    announced = true;
    // Only call it a lease when DHCP actually handed it out; `wifi ip` sets
    // the address by hand and used to be reported as a lease too.
    kprintf(static_config ? "\n[NET] static address " : "\n[NET] DHCP lease: address ");
    print_ip(*netif_ip4_addr(interface));
    kprintf(" mask ");
    print_ip(*netif_ip4_netmask(interface));
    kprintf(" gateway ");
    print_ip(*netif_ip4_gw(interface));
    kprintf("\n");
}

// ----------------------------------------------------- TCP echo, port 7
// RFC 862: send back whatever arrives.  Data that does not fit the send
// buffer stays queued in `pending`, and the receive window is only reopened
// (tcp_recved) for bytes that were actually echoed, so a fast sender is
// throttled by TCP flow control instead of being dropped.
struct echo_connection {
    tcp_pcb* pcb{nullptr};
    pbuf* pending{nullptr};
    std::uint64_t bytes{0};
    bool closing{false};
    bool used{false};
};

constinit std::array<echo_connection, 4> echo_connections{};
constinit std::uint64_t echo_total_bytes = 0;
constinit std::uint32_t echo_total_connections = 0;

void echo_release(echo_connection& c) noexcept {
    if (c.pending != nullptr) {
        pbuf_free(c.pending);
    }
    c = {};
}

void echo_close(echo_connection& c) noexcept {
    tcp_pcb* pcb = c.pcb;
    if (pcb != nullptr) {
        tcp_arg(pcb, nullptr);
        tcp_recv(pcb, nullptr);
        tcp_sent(pcb, nullptr);
        tcp_err(pcb, nullptr);
        tcp_poll(pcb, nullptr, 0);
        if (tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);
        }
        kprintf("[NET] echo: connection closed after %lu bytes\n",
                static_cast<unsigned long>(c.bytes));
    }
    echo_release(c);
}

void echo_flush(echo_connection& c) noexcept {
    while (c.pending != nullptr) {
        pbuf* head = c.pending;
        const u16_t room = tcp_sndbuf(c.pcb);
        if (room == 0) {
            break;
        }
        const u16_t chunk = head->len < room ? head->len : room;
        if (tcp_write(c.pcb, head->payload, chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            break;
        }
        c.bytes += chunk;
        echo_total_bytes += chunk;
        tcp_recved(c.pcb, chunk);
        if (chunk == head->len) {
            c.pending = head->next;
            if (c.pending != nullptr) {
                pbuf_ref(c.pending);
            }
            pbuf_free(head);
        } else {
            pbuf_remove_header(head, chunk);
        }
    }
    tcp_output(c.pcb);
    if (c.closing && c.pending == nullptr) {
        echo_close(c);
    }
}

err_t echo_received(void* arg, tcp_pcb*, pbuf* p, err_t) {
    auto& c = *static_cast<echo_connection*>(arg);
    if (p == nullptr) {   // the peer closed its side: finish echoing, then close
        c.closing = true;
        echo_flush(c);
        return ERR_OK;
    }
    if (c.pending == nullptr) {
        c.pending = p;
    } else {
        pbuf_cat(c.pending, p);
    }
    echo_flush(c);
    return ERR_OK;
}

err_t echo_sent(void* arg, tcp_pcb*, u16_t) {
    echo_flush(*static_cast<echo_connection*>(arg));
    return ERR_OK;
}

err_t echo_polled(void* arg, tcp_pcb*) {
    echo_flush(*static_cast<echo_connection*>(arg));
    return ERR_OK;
}

void echo_error(void* arg, err_t) {
    // The pcb is already gone.
    auto& c = *static_cast<echo_connection*>(arg);
    kprintf("[NET] echo: connection reset after %lu bytes\n",
            static_cast<unsigned long>(c.bytes));
    echo_release(c);
}

err_t echo_accepted(void*, tcp_pcb* pcb, err_t error) {
    if (error != ERR_OK || pcb == nullptr) {
        return ERR_VAL;
    }
    for (auto& c : echo_connections) {
        if (!c.used) {
            c = {};
            c.used = true;
            c.pcb = pcb;
            ++echo_total_connections;
            tcp_arg(pcb, &c);
            tcp_recv(pcb, echo_received);
            tcp_sent(pcb, echo_sent);
            tcp_err(pcb, echo_error);
            tcp_poll(pcb, echo_polled, 1);
            tcp_nagle_disable(pcb);
            kprintf("[NET] echo: connection from ");
            print_ip(*ip_2_ip4(&pcb->remote_ip));
            kprintf(":%u\n", pcb->remote_port);
            return ERR_OK;
        }
    }
    tcp_abort(pcb);
    return ERR_ABRT;
}

void echo_listen() noexcept {
    tcp_pcb* pcb = tcp_new();
    if (pcb == nullptr || tcp_bind(pcb, IP_ANY_TYPE, echo_port) != ERR_OK) {
        serial.write("[NET] echo: cannot bind port 7\r\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, echo_accepted);
}

// ------------------------------------------------------------------ ping
constexpr std::uint16_t ping_id = 0x4d51;   // "MQ"

struct ping_state {
    std::uint16_t sequence{0};
    bool replied{false};
    std::uint64_t sent_us{0};
    std::uint64_t rtt_us{0};
    std::uint8_t ttl{0};
    std::uint16_t length{0};
};
constinit ping_state pinging{};

u8_t ping_received(void*, raw_pcb*, pbuf* p, const ip_addr_t*) {
    // p starts at the IP header.
    if (p->tot_len < IP_HLEN + sizeof(icmp_echo_hdr)) {
        return 0;
    }
    std::array<std::uint8_t, IP_HLEN + sizeof(icmp_echo_hdr)> head{};
    pbuf_copy_partial(p, head.data(), static_cast<u16_t>(head.size()), 0);
    const auto* ip = reinterpret_cast<const ip_hdr*>(head.data());
    const std::size_t ip_len = IPH_HL_BYTES(ip);
    if (ip_len != IP_HLEN) {
        return 0;
    }
    icmp_echo_hdr echo{};
    std::memcpy(&echo, head.data() + IP_HLEN, sizeof(echo));
    if (echo.type != ICMP_ER || echo.id != lwip_htons(ping_id) ||
        echo.seqno != lwip_htons(pinging.sequence)) {
        return 0;   // not ours: let lwIP handle it
    }
    pinging.rtt_us = plat_time_us() - pinging.sent_us;
    pinging.ttl = IPH_TTL(ip);
    pinging.length = static_cast<std::uint16_t>(p->tot_len - IP_HLEN);
    pinging.replied = true;
    pbuf_free(p);
    return 1;   // consumed
}

constinit bool resolved = false;
constinit ip_addr_t resolved_address{};

void dns_found(const char*, const ip_addr_t* address, void*) {
    if (address != nullptr) {
        resolved_address = *address;
    } else {
        ip_addr_set_zero(&resolved_address);
    }
    resolved = true;
}

void service() noexcept {
    wlan::poll();
    sys_check_timeouts();
}

template <typename Predicate>
bool wait_for(std::uint32_t timeout_ms, Predicate done) noexcept {
    const std::uint32_t start = platform::millis();
    for (;;) {
        service();
        if (done()) {
            return true;
        }
        if (platform::millis() - start >= timeout_ms) {
            return false;
        }
        plat_delay_us(1000);
    }
}

}  // namespace

void set_dhcp_enabled(bool on) noexcept { dhcp_enabled = on; }

void init() noexcept {
    if (initialised) {
        return;
    }
    lwip_init();
    netif_add(&wl0, nullptr, nullptr, nullptr, nullptr, interface_init, ethernet_input);
    netif_set_default(&wl0);
    netif_set_status_callback(&wl0, on_status);
    netif_set_up(&wl0);
    wlan::set_receive_handler(on_frame);
    wlan::set_link_handler(on_link);
    echo_listen();
    initialised = true;
    serial.write("[NET] lwIP " LWIP_VERSION_STRING " ready, TCP echo server on port 7\r\n");
}

void poll() noexcept {
    if (initialised) {
        sys_check_timeouts();
    }
}

// `wifi arp`: preload a neighbour entry so unicast traffic can be tested
// (and used) without any broadcast ARP on the air.
bool add_static_arp(std::string_view ip, std::string_view mac) noexcept {
    if (!initialised) {
        return false;
    }
    std::array<char, 32> ip_text{};
    if (ip.size() >= ip_text.size()) {
        return false;
    }
    std::memcpy(ip_text.data(), ip.data(), ip.size());
    ip4_addr_t addr{};
    if (ip4addr_aton(ip_text.data(), &addr) == 0) {
        return false;
    }
    std::array<std::uint8_t, 6> hw{};
    std::size_t at = 0;
    unsigned value = 0;
    bool have_digit = false;
    for (const char c : mac) {
        if (c == ':' || c == '-') {
            if (!have_digit || at >= hw.size()) {
                return false;
            }
            hw[at++] = static_cast<std::uint8_t>(value);
            value = 0;
            have_digit = false;
        } else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            value = value * 16 + static_cast<unsigned>(c >= 'a' ? c - 'a' + 10
                                                        : c >= 'A' ? c - 'A' + 10
                                                                   : c - '0');
            have_digit = true;
            if (value > 0xff) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (!have_digit || at != hw.size() - 1) {
        return false;
    }
    hw[at] = static_cast<std::uint8_t>(value);
    struct eth_addr hwaddr {};
    std::memcpy(hwaddr.addr, hw.data(), hw.size());
    return etharp_add_static_entry(&addr, &hwaddr) == ERR_OK;
}

bool set_static_address(std::string_view address, std::string_view gateway) noexcept {
    if (!initialised) {
        return false;
    }
    std::array<char, 32> address_text{};
    std::array<char, 32> gateway_text{};
    if (address.size() >= address_text.size() || gateway.size() >= gateway_text.size()) {
        return false;
    }
    std::memcpy(address_text.data(), address.data(), address.size());
    std::memcpy(gateway_text.data(), gateway.data(), gateway.size());

    std::uint32_t prefix = 24;
    if (auto* slash = std::strchr(address_text.data(), '/'); slash != nullptr) {
        prefix = 0;
        for (const char* p = slash + 1; *p >= '0' && *p <= '9'; ++p) {
            prefix = prefix * 10u + static_cast<std::uint32_t>(*p - '0');
        }
        *slash = '\0';
    }
    ip4_addr_t addr{};
    if (ip4addr_aton(address_text.data(), &addr) == 0) {
        return false;
    }
    ip4_addr_t mask{};
    mask.addr = lwip_htonl(prefix >= 32 ? 0xffffffffu : (0xffffffffu << (32 - prefix)));
    ip4_addr_t gw{};
    if (!gateway.empty() && ip4addr_aton(gateway_text.data(), &gw) == 0) {
        return false;
    }
    dhcp_stop(&wl0);
    static_config = true;
    netif_set_addr(&wl0, &addr, &mask, &gw);
    announced = true;
    return true;
}

bool has_address() noexcept {
    return initialised && netif_is_link_up(&wl0) &&
           !ip4_addr_isany_val(*netif_ip4_addr(&wl0));
}

bool wait_for_address(std::uint32_t timeout_ms) noexcept {
    return wait_for(timeout_ms, [] { return has_address(); });
}

void print_status() noexcept {
    if (!initialised) {
        serial.write("[NET] not initialised\r\n");
        return;
    }
    kprintf("[NET] wl0: link %s, address ", netif_is_link_up(&wl0) ? "up" : "down");
    print_ip(*netif_ip4_addr(&wl0));
    kprintf(" mask ");
    print_ip(*netif_ip4_netmask(&wl0));
    kprintf(" gateway ");
    print_ip(*netif_ip4_gw(&wl0));
    kprintf(" dns ");
    print_ip(*ip_2_ip4(dns_getserver(0)));
    kprintf("\n[NET] echo server: %u connection(s), %lu bytes echoed\n",
            echo_total_connections, static_cast<unsigned long>(echo_total_bytes));
    kprintf("[NET] frames to lwIP: %u, from lwIP: %u\n", rx_frames, tx_frames);
}

// ------------------------------------------------------------- UDP probe
// Diagnostics: send a broadcast/unicast datagram of a chosen size and log
// any datagram that comes back.  Used to characterise the group-addressed
// transmit path (DHCP rides on the same path).
constinit udp_pcb* probe_pcb = nullptr;

void probe_recv(void*, udp_pcb*, pbuf* p, const ip_addr_t* addr, u16_t port) noexcept {
    if (p == nullptr) {
        return;
    }
    serial.print("[NET] UDP rx {} bytes from ", static_cast<unsigned>(p->tot_len));
    print_ip(*ip_2_ip4(addr));
    serial.print(":{}\r\n", static_cast<unsigned>(port));
    pbuf_free(p);
}

bool udp_probe(std::string_view host, unsigned port, unsigned size) noexcept {
    if (!initialised) {
        return false;
    }
    if (size > 1400) {
        size = 1400;
    }
    if (probe_pcb == nullptr) {
        probe_pcb = udp_new();
        if (probe_pcb == nullptr) {
            return false;
        }
        udp_recv(probe_pcb, probe_recv, nullptr);
        (void)udp_bind(probe_pcb, IP_ANY_TYPE, 5000);
    }
    std::array<char, 32> text{};
    if (host.size() >= text.size()) {
        return false;
    }
    std::memcpy(text.data(), host.data(), host.size());
    ip_addr_t destination{};
    if (ipaddr_aton(text.data(), &destination) == 0) {
        return false;
    }
    pbuf* p = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(size), PBUF_POOL);
    if (p == nullptr) {
        return false;
    }
    std::array<std::uint8_t, 1400> filler{};
    filler.fill(static_cast<std::uint8_t>('A'));
    pbuf_take(p, filler.data(), static_cast<u16_t>(size));
    const err_t err = udp_sendto(probe_pcb, p, &destination, static_cast<u16_t>(port));
    pbuf_free(p);
    serial.print("[NET] UDP tx {} bytes to port {}: {}\r\n", size, port,
                 static_cast<unsigned>(err));
    return err == ERR_OK;
}

unsigned ping(std::string_view target, unsigned count, unsigned payload_size) noexcept {
    if (!has_address()) {
        serial.write("[NET] ping: no IP address yet\r\n");
        return 0;
    }
    if (payload_size > 1400) {
        payload_size = 1400;
    }

    // Resolve the target.
    ip_addr_t address{};
    std::array<char, 64> name{};
    if (target.size() >= name.size()) {
        return 0;
    }
    std::memcpy(name.data(), target.data(), target.size());
    if (target == "gw" || target.empty()) {
        ip_addr_copy_from_ip4(address, *netif_ip4_gw(&wl0));
    } else if (ip4addr_aton(name.data(), ip_2_ip4(&address)) == 0) {
        resolved = false;
        const err_t result = dns_gethostbyname(name.data(), &address, dns_found, nullptr);
        if (result == ERR_INPROGRESS) {
            if (!wait_for(5000, [] { return resolved; }) || ip_addr_isany(&resolved_address)) {
                serial.print("[NET] ping: cannot resolve \"{}\"\r\n", target);
                return 0;
            }
            address = resolved_address;
        } else if (result != ERR_OK) {
            serial.print("[NET] ping: cannot resolve \"{}\"\r\n", target);
            return 0;
        }
    }

    raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
    if (pcb == nullptr) {
        return 0;
    }
    raw_recv(pcb, ping_received, nullptr);
    raw_bind(pcb, IP_ADDR_ANY);

    kprintf("[NET] PING ");
    print_ip(*ip_2_ip4(&address));
    kprintf(": %u data bytes\n", payload_size);

    unsigned replies = 0;
    std::uint64_t rtt_min = ~0ull, rtt_max = 0, rtt_sum = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto size = static_cast<u16_t>(sizeof(icmp_echo_hdr) + payload_size);
        pbuf* p = pbuf_alloc(PBUF_IP, size, PBUF_RAM);
        if (p == nullptr) {
            break;
        }
        auto* echo = static_cast<icmp_echo_hdr*>(p->payload);
        ICMPH_TYPE_SET(echo, ICMP_ECHO);
        ICMPH_CODE_SET(echo, 0);
        echo->chksum = 0;
        echo->id = lwip_htons(ping_id);
        pinging.sequence = static_cast<std::uint16_t>(pinging.sequence + 1);
        echo->seqno = lwip_htons(pinging.sequence);
        auto* data = static_cast<std::uint8_t*>(p->payload) + sizeof(icmp_echo_hdr);
        for (unsigned j = 0; j < payload_size; ++j) {
            data[j] = static_cast<std::uint8_t>(j);
        }
        echo->chksum = inet_chksum(echo, size);

        pinging.replied = false;
        pinging.sent_us = plat_time_us();
        raw_sendto(pcb, p, &address);
        pbuf_free(p);

        const std::uint32_t started = platform::millis();
        if (wait_for(1000, [] { return pinging.replied; })) {
            ++replies;
            rtt_sum += pinging.rtt_us;
            if (pinging.rtt_us < rtt_min) rtt_min = pinging.rtt_us;
            if (pinging.rtt_us > rtt_max) rtt_max = pinging.rtt_us;
            kprintf("[NET] %u bytes from ", pinging.length);
            print_ip(*ip_2_ip4(&address));
            kprintf(": icmp_seq=%u ttl=%u time=%lu.%03lu ms\n", i + 1, pinging.ttl,
                    static_cast<unsigned long>(pinging.rtt_us / 1000u),
                    static_cast<unsigned long>(pinging.rtt_us % 1000u));
            // one request per second
            (void)wait_for(1000 - (platform::millis() - started) % 1000, [] { return false; });
        } else {
            kprintf("[NET] icmp_seq=%u: no reply within 1 s\n", i + 1);
        }
        if (!wlan::linked()) {
            break;
        }
    }
    raw_remove(pcb);

    kprintf("[NET] ping: %u sent, %u received, %u%% loss", count, replies,
            count != 0 ? (count - replies) * 100u / count : 0u);
    if (replies != 0) {
        const std::uint64_t average = rtt_sum / replies;
        kprintf(", rtt min/avg/max = %lu.%03lu/%lu.%03lu/%lu.%03lu ms",
                static_cast<unsigned long>(rtt_min / 1000u), static_cast<unsigned long>(rtt_min % 1000u),
                static_cast<unsigned long>(average / 1000u), static_cast<unsigned long>(average % 1000u),
                static_cast<unsigned long>(rtt_max / 1000u), static_cast<unsigned long>(rtt_max % 1000u));
    }
    kprintf("\n");
    return replies;
}

}  // namespace mqpro::net

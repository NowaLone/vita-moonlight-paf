/**
 * @file udp_sniffer_vita.h
 * @brief mDNS UDP sniffer for PSVita (VitaSDK) - API de integración
 *
 * This file defines the API to integrate the Moonlight/Sunshine mDNS sniffer in PSVita applications.
 * The sniffer detects GameStream services (Moonlight/Sunshine) on the local network and notifies detections via callback.
 *
 * Uso típico:
 *   1. Call udp_sniffer_vita_set_callback() with your function to receive detections.
 *   2. Llama periódicamente a udp_sniffer_vita_poll() en tu bucle principal.
 *
 * The callback function is invoked only when a complete Moonlight/Sunshine host is detected (host, PC name, IP and port).
 *
 * @author AorsiniYT
 * @copyright 2025
 */

#ifndef UDP_SNIFFER_VITA_H
#define UDP_SNIFFER_VITA_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Callback type for Moonlight/Sunshine host detection.
 * @param idx   Detection number (1, 2, ...)
 * @param host  mDNS host name (e.g. "DESKTOP-XXXXXX._nvstream._tcp.local")
 * @param pcname Machine/PC name (target SRV, e.g. "DESKTOP-XXXXXX.local")
 * @param ip    Detected IP address (e.g. "192.168.1.100")
 * @param port TCP port of the GameStream service
 */
typedef void (*moonlight_found_cb)(int idx, const char* host, const char* pcname, const char* ip, int port);

/**
 * Registers the callback function to be called when a Moonlight/Sunshine host is detected.
 * It is only invoked when all data (host, pcname, ip, port) is present.
 * @param cb User-defined callback function.
 */
void udp_sniffer_vita_set_callback(moonlight_found_cb cb);

/**
 * Processes an mDNS packet if available (non-blocking).
 * Call periodically from the main loop.
 */
void udp_sniffer_vita_poll(void);

/**
 * Closes the UDP sniffer and frees resources. Must be called before restarting a search.
 */
void udp_sniffer_vita_deinit(void);

/**
 * Resets the internal state of the sniffer (optional, for symmetry with deinit).
 */
void udp_sniffer_vita_init(void);

#ifdef __cplusplus
}
#endif

#endif // UDP_SNIFFER_VITA_H

/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>

#define NET_DEV_MAX_CNT 8

#ifndef RT_WLAN_SSID_MAX_LENGTH
#define RT_WLAN_SSID_MAX_LENGTH (32) /* SSID MAX LEN */
#endif

#ifndef RT_WLAN_PASSWORD_MAX_LENGTH
#define RT_WLAN_PASSWORD_MAX_LENGTH (64) /* PASSWORD MAX LEN*/
#endif

#ifndef RT_WLAN_BSSID_MAX_LENGTH
#define RT_WLAN_BSSID_MAX_LENGTH (6) /* BSSID MAX LEN (default is 6) */
#endif

#define RT_WLAN_STA_SCAN_MAX_AP (64)

#define WEP_ENABLED      0x0001
#define TKIP_ENABLED     0x0002
#define AES_ENABLED      0x0004
#define WSEC_SWFLAG      0x0008
#define AES_CMAC_ENABLED 0x0010

#define SHARED_ENABLED 0x00008000
#define WPA_SECURITY   0x00200000
#define WPA2_SECURITY  0x00400000
#define WPA3_SECURITY  0x00800000
#define WPS_ENABLED    0x10000000

#define IEEE_8021X_ENABLED 0x80000000

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum rt_wlan_security_t {
    SECURITY_OPEN                 = 0, /**< Open security                                 */
    SECURITY_WEP_PSK              = WEP_ENABLED, /**< WEP PSK Security with open authentication     */
    SECURITY_WEP_SHARED           = (WEP_ENABLED | SHARED_ENABLED), /**< WEP PSK Security with shared authentication   */
    SECURITY_WPA_TKIP_PSK         = (WPA_SECURITY | TKIP_ENABLED), /**< WPA PSK Security with TKIP                    */
    SECURITY_WPA_TKIP_8021X       = (IEEE_8021X_ENABLED | WPA_SECURITY | TKIP_ENABLED), /**< WPA 8021X Security with TKIP */
    SECURITY_WPA_AES_PSK          = (WPA_SECURITY | AES_ENABLED), /**< WPA PSK Security with AES                     */
    SECURITY_WPA_AES_8021X        = (IEEE_8021X_ENABLED | WPA_SECURITY | AES_ENABLED), /**< WPA 8021X Security with AES */
    SECURITY_WPA2_AES_PSK         = (WPA2_SECURITY | AES_ENABLED), /**< WPA2 PSK Security with AES                    */
    SECURITY_WPA2_AES_8021X       = (IEEE_8021X_ENABLED | WPA2_SECURITY | WEP_ENABLED), /**< WPA2 8021X Security with AES */
    SECURITY_WPA2_TKIP_PSK        = (WPA2_SECURITY | TKIP_ENABLED), /**< WPA2 PSK Security with TKIP                   */
    SECURITY_WPA2_TKIP_8021X      = (IEEE_8021X_ENABLED | WPA2_SECURITY | TKIP_ENABLED), /**< WPA2 8021X Security with TKIP */
    SECURITY_WPA2_MIXED_PSK       = (WPA2_SECURITY | AES_ENABLED | TKIP_ENABLED), /**< WPA2 PSK Security with AES & TKIP */
    SECURITY_WPA_WPA2_MIXED_PSK   = (WPA_SECURITY | WPA2_SECURITY), /**< WPA/WPA2 PSK Security                         */
    SECURITY_WPA_WPA2_MIXED_8021X = (IEEE_8021X_ENABLED | WPA_SECURITY | WPA2_SECURITY), /**< WPA/WPA2 8021X Security */
    SECURITY_WPA2_AES_CMAC = (WPA2_SECURITY | AES_CMAC_ENABLED), /**< WPA2 Security with AES and Management Frame Protection */

    SECURITY_WPS_OPEN   = WPS_ENABLED, /**< WPS with open security                  */
    SECURITY_WPS_SECURE = (WPS_ENABLED | AES_ENABLED), /**< WPS with AES security                   */

    SECURITY_WPA3_AES_PSK = (WPA3_SECURITY | AES_ENABLED), /**< WPA3-AES with AES security  */

    SECURITY_UNKNOWN
    = -1, /**< May be returned by scan function if security is unknown. Do not pass this to the join function! */
};

enum rt_802_11_band_t {
    RT_802_11_BAND_5GHZ    = 0, /* Denotes 5GHz radio band   */
    RT_802_11_BAND_2_4GHZ  = 1, /* Denotes 2.4GHz radio band */
    RT_802_11_BAND_UNKNOWN = 0x7fffffff, /* unknown */
};

enum rt_netif_t {
    RT_NET_DEV_WLAN_STA    = 0,
    RT_NET_DEV_WLAN_AP     = 1,
    RT_NET_DEV_USB_RTL8152 = 2,
    RT_NET_DEV_USB_ECM     = 3,
};

struct rt_wlan_ssid_t {
    uint8_t len;
    uint8_t val[RT_WLAN_SSID_MAX_LENGTH + 1];
};

struct rt_wlan_key_t {
    uint8_t len;
    uint8_t val[RT_WLAN_PASSWORD_MAX_LENGTH + 1];
};

struct rt_wlan_info_t {
    /* security type */
    enum rt_wlan_security_t security;
    /* 2.4G/5G */
    enum rt_802_11_band_t band;
    /* maximal data rate */
    uint32_t datarate;
    /* radio channel */
    int16_t channel;
    /* signal strength */
    int16_t rssi;
    /* ssid */
    struct rt_wlan_ssid_t ssid;
    /* hwaddr */
    uint8_t bssid[RT_WLAN_BSSID_MAX_LENGTH];
    uint8_t hidden;
};

struct ip_addr_t {
    uint32_t addr;
};

struct ifconfig_t {
    struct ip_addr_t ip; /* IP address */
    struct ip_addr_t gw; /* gateway */
    struct ip_addr_t netmask; /* subnet mask */
    struct ip_addr_t dns; /* DNS server */
};

/**
 * @brief Get whether STA auto-reconnect is enabled.
 * @param[out] enable Pointer to store the result (0 or 1).
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_get_auto_reconnect(int* enable);

/**
 * @brief Enable or disable STA auto-reconnect.
 * @param[in] enable 0 to disable, 1 to enable.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_set_auto_reconnect(int enable);

/**
 * @brief Connect to a Wi-Fi network using SSID and password.
 * @param[in] ssid SSID string.
 * @param[in] password Password string.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_connect_with_ssid(char* ssid, char* password);

/**
 * @brief Connect to a Wi-Fi network using scan result info.
 * @param[in] info Scan result info.
 * @param[in] password Password string.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_connect_with_scan_info(struct rt_wlan_info_t* info, char* password);

/**
 * @brief Disconnect from the current Wi-Fi AP.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_disconnect_ap(void);

/**
 * @brief Check if the STA is connected.
 * @param[out] status 1 if connected, 0 if not.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_isconnected(int* status);

/**
 * @brief Get current MAC address.
 * @param[out] mac Buffer to store MAC address.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_get_mac(uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH]);

/**
 * @brief Set MAC address for STA.
 * @param[in] mac MAC address to set.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_set_mac(uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH]);

/**
 * @brief Get information about the connected AP.
 * @param[out] info Pointer to store AP information.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_get_ap_info(struct rt_wlan_info_t* info);

/**
 * @brief Get current RSSI (signal strength).
 * @param[out] rssi Pointer to store RSSI value.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_get_rssi(int* rssi);

/**
 * @brief Scan for Wi-Fi APs.
 * @param[out] ap_num Number of APs found.
 * @param[out] ap_infos Array to store AP information.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_sta_scan(int* ap_num, struct rt_wlan_info_t ap_infos[RT_WLAN_STA_SCAN_MAX_AP]);

/**
 * @brief Scan for a specific SSID and get its info.
 * @param[in] ssid SSID to search.
 * @param[out] ap_info Pointer to store found AP info.
 * @return 0 if found, -1 otherwise.
 */
int netmgmt_wlan_sta_scan_with_ssid(char* ssid, struct rt_wlan_info_t* ap_info);

/**
 * @brief Start AP mode with SSID and password.
 * @param[in] ssid SSID string.
 * @param[in] password Password string.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_start_with_ssid(char* ssid, char* password);

/**
 * @brief Start AP mode using info and password.
 * @param[in] info AP configuration info.
 * @param[in] password Password.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_start_with_info(struct rt_wlan_info_t* info, char* password);

/**
 * @brief Stop the running AP.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_stop(void);

/**
 * @brief Check if AP mode is active.
 * @param[out] status 1 if active, 0 if not.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_isactived(int* status);

/**
 * @brief Get current AP configuration info.
 * @param[out] info Pointer to store AP info.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_get_info(struct rt_wlan_info_t* info);

/**
 * @brief Get list of connected STA clients.
 * @param[out] sta_num Number of connected STAs.
 * @param[out] sta_infos Array to store STA info.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_get_sta_info(int* sta_num, struct rt_wlan_info_t sta_infos[RT_WLAN_STA_SCAN_MAX_AP]);

/**
 * @brief Disconnect a STA client from AP using MAC.
 * @param[in] mac MAC address of client.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_disconnect_sta(uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH]);

/**
 * @brief Get current AP country code.
 * @param[out] country Pointer to store country code.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_get_country(int* country);

/**
 * @brief Set AP country code.
 * @param[in] country Country code to set.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_wlan_ap_set_country(int country);

/**
 * @brief Checks if a LAN interface is connected.
 * This is determined by both the physical link being up and a valid IP address being assigned.
 * @param[in] itf The network interface enum (e.g., `RT_NET_DEV_USB_RTL8152`).
 * @param[out] status Pointer to an integer to store the connection status: 1 if connected, 0 if not.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_lan_get_isconnected(enum rt_netif_t itf, int* status);

/**
 * @brief Gets the physical link status of a LAN interface.
 * @param[in] itf The network interface enum.
 * @param[out] status Pointer to an integer to store the link status: 1 if link is up, 2 if link is down, 0 if unable to get
 * status.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_lan_get_link_status(enum rt_netif_t itf, int* status);

/**
 * @brief Gets the current MAC address of a LAN interface.
 * @param[in] itf The network interface enum.
 * @param[out] mac A buffer to copy the 6-byte MAC address into. The buffer size should be at least `RT_WLAN_BSSID_MAX_LENGTH`.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_lan_get_mac(enum rt_netif_t itf, uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH]);

/**
 * @brief Sets the MAC address for a LAN interface.
 * @param[in] itf The network interface enum.
 * @param[in] mac The 6-byte MAC address to set. The buffer size should be at least `RT_WLAN_BSSID_MAX_LENGTH`.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_lan_set_mac(enum rt_netif_t itf, uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH]);

/**
 * @brief Get name of the default network device.
 * @param[out] name Buffer to store device name (32 bytes).
 * @return 0 on success, -1 on failure.
 */
int netmgmt_utils_get_defeault_dev(char name[32]);

/**
 * @brief Set default network device.
 * @param[in] name Name of the network device.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_utils_set_defeault_dev(char name[32]);

/**
 * @brief Get list of all network device names.
 * @param[out] dev_num Pointer to store number of devices.
 * @param[out] names Array to store device names.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_utils_get_dev_list(int* dev_num, char names[NET_DEV_MAX_CNT][32]);

/**
 * @brief Probe network interface availability.
 * @param[in] itf Interface type.
 * @param[out] status Pointer to receive result.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_utils_probe_device(enum rt_netif_t itf, int* status);

/**
 * @brief Get network interface IP configuration.
 * @param[in] itf Interface type.
 * @param[out] config Pointer to store config.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_utils_get_ifconfig(enum rt_netif_t itf, struct ifconfig_t* config);

/**
 * @brief Set static IP configuration for interface.
 * @param[in] itf Interface type.
 * @param[in] config IP configuration.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_utils_set_ifconfig_static(enum rt_netif_t itf, struct ifconfig_t* config);

/**
 * @brief Enable DHCP for network interface.
 * @param[in] itf Interface type.
 * @return 0 on success, -1 on failure.
 */
int netmgmt_utils_set_ifconfig_dhcp(enum rt_netif_t itf);

#ifdef __cplusplus
}
#endif /* __cplusplus */

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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "hal_netmgmt.h"

#define NETMGMT_DEV_NAME "/dev/netmgmt"

// wlan basic
#define IOCTRL_WM_GET_AUTO_RECONNECT _IOWR('N', 0x00, void*)
#define IOCTRL_WM_SET_AUTO_RECONNECT _IOWR('N', 0x01, void*)

// wlan sta
#define IOCTRL_WM_STA_CONNECT      _IOWR('N', 0x10, void*)
#define IOCTRL_WM_STA_DISCONNECT   _IOWR('N', 0x11, void*)
#define IOCTRL_WM_STA_IS_CONNECTED _IOWR('N', 0x12, void*)
#define IOCTRL_WM_STA_GET_MAC      _IOWR('N', 0x13, void*)
#define IOCTRL_WM_STA_SET_MAC      _IOWR('N', 0x14, void*)
#define IOCTRL_WM_STA_GET_AP_INFO  _IOWR('N', 0x15, void*)
#define IOCTRL_WM_STA_GET_RSSI     _IOWR('N', 0x16, void*)
#define IOCTRL_WM_STA_SCAN         _IOWR('N', 0x17, void*)

// wlan ap
#define IOCTRL_WM_AP_START        _IOWR('N', 0x20, void*)
#define IOCTRL_WM_AP_STOP         _IOWR('N', 0x21, void*)
#define IOCTRL_WM_AP_IS_ACTIVE    _IOWR('N', 0x22, void*)
#define IOCTRL_WM_AP_GET_INFO     _IOWR('N', 0x23, void*)
#define IOCTRL_WM_AP_GET_STA_INFO _IOWR('N', 0x24, void*)
#define IOCTRL_WM_AP_DEAUTH_STA   _IOWR('N', 0x25, void*)
#define IOCTRL_WM_AP_GET_COUNTRY  _IOWR('N', 0x26, void*)
#define IOCTRL_WM_AP_SET_COUNTRY  _IOWR('N', 0x27, void*)

// lan
#define IOCTRL_LAN_GET_ISCONNECTED _IOWR('N', 0x80, void*)
#define IOCTRL_LAN_GET_LINK_STATUS _IOWR('N', 0x81, void*)
#define IOCTRL_LAN_GET_MAC         _IOWR('N', 0x82, void*)
#define IOCTRL_LAN_SET_MAC         _IOWR('N', 0x83, void*)

// network util
#define IOCTRL_NET_IFCONFIG        _IOWR('N', 0x100, void*)
#define IOCTRL_NET_SET_DEV_DEFAULT _IOWR('N', 0x101, void*)
#define IOCTRL_NET_GET_DEV_DEFAULT _IOWR('N', 0x102, void*)
#define IOCTRL_NET_GET_DEV_LIST    _IOWR('N', 0x103, void*)
#define IOCTRL_NET_PROBE           _IOWR('N', 0x104, void*)

#define INVALID_INFO(_info)                                                                                                    \
    do {                                                                                                                       \
        memset((_info), 0, sizeof(struct rt_wlan_info_t));                                                                     \
        (_info)->band     = RT_802_11_BAND_UNKNOWN;                                                                            \
        (_info)->security = SECURITY_UNKNOWN;                                                                                  \
        (_info)->channel  = -1;                                                                                                \
    } while (0)

#define SSID_SET(_info, _ssid)                                                                                                 \
    do {                                                                                                                       \
        strncpy((char*)(_info)->ssid.val, (_ssid), RT_WLAN_SSID_MAX_LENGTH);                                                   \
        (_info)->ssid.len = strlen((char*)(_info)->ssid.val);                                                                  \
    } while (0)

struct rt_wlan_connect_config {
    int use_info;
    union {
        struct rt_wlan_ssid_t ssid;
        struct rt_wlan_info_t info;
    };
    struct rt_wlan_key_t key;
};

struct rt_wlan_scan_result {
    int32_t                num;
    struct rt_wlan_info_t* info;
};

struct ifconfig_cmd {
    enum rt_netif_t  net_if; /* 0: sta, 1: ap, 2:... */
    uint16_t         func; /* 0: get ip info, 1: disable dhcp, set static ip, 2: enable dhcp */
    struct ip_addr_t ip; /* IP address */
    struct ip_addr_t gw; /* gateway */
    struct ip_addr_t netmask; /* subnet mask */
    struct ip_addr_t dns; /* DNS server */
};

struct lan_status_cmd_wrap_t {
    enum rt_netif_t itf;

    int value;
};

struct lan_mac_cmd_wrap_t {
    enum rt_netif_t itf;

    uint8_t mac[6];
};

static int _netmgmt_ioctl(uint32_t cmd, void* arg)
{
    int result = -1;

    static int _netmgmt_fd = -1;

    if (0 > _netmgmt_fd) {
        if (0 > (_netmgmt_fd = open(NETMGMT_DEV_NAME, O_RDWR))) {
            printf("[hal_netmgmt]: open device failed\n");
            return -1;
        }
    }

    result = ioctl(_netmgmt_fd, cmd, arg);

    // if (0x00 != result) {
    //     printf("[hal_netmgmt]: ioctl failed, cmd: 0x%08x\n", cmd);
    // }

    return result;
}

static int _netmgmt_ioctl_with_type_rt_wlan_connect_config(uint32_t cmd, struct rt_wlan_ssid_t* ssid,
                                                           struct rt_wlan_info_t* info, char* password)
{
    size_t password_length = 0;

    struct rt_wlan_connect_config config;

    memset(&config, 0x00, sizeof(config));

    if ((!ssid && !info)) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    if (password) {
        password_length = strlen(password);
        if (RT_WLAN_PASSWORD_MAX_LENGTH < password_length) {
            printf("[hal_netmgmt]: %s invalid password length\n", __FUNCTION__);
            return -1;
        }

        config.key.len = (uint8_t)password_length & 0xFF;
        strncpy((char*)&config.key.val, password, RT_WLAN_PASSWORD_MAX_LENGTH);
    } else {
        config.key.len    = 0;
        config.key.val[0] = '\0';
    }

    if (ssid) {
        config.use_info = 0;
        memcpy(&config.ssid, ssid, sizeof(config.ssid));
    }

    if (info) {
        config.use_info = 1;
        memcpy(&config.info, info, sizeof(config.info));
    }

    if (0x00 != _netmgmt_ioctl(cmd, &config)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

static int _netmgmt_ioctl_with_type_rt_wlan_scan_result(uint32_t cmd, struct rt_wlan_scan_result** scan_result, char* ssid)
{
    struct rt_wlan_scan_result* result           = NULL;
    size_t                      result_info_size = 0;

    if (!scan_result) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    if (NULL != *scan_result) {
        printf("[hal_netmgmt]: %s result is not NULL, free it\n", __FUNCTION__);
        free(*scan_result);
    }
    *scan_result = NULL;

    result_info_size = sizeof(struct rt_wlan_info_t) * RT_WLAN_STA_SCAN_MAX_AP;
    result           = (struct rt_wlan_scan_result*)malloc(sizeof(struct rt_wlan_scan_result) + result_info_size);
    if (NULL == result) {
        printf("[hal_netmgmt]: %s EMEM\n", __FUNCTION__);
        return -1;
    }
    memset(result, 0, sizeof(struct rt_wlan_scan_result) + result_info_size);

    result->num  = RT_WLAN_STA_SCAN_MAX_AP;
    result->info = (struct rt_wlan_info_t*)(((uint8_t*)result) + sizeof(struct rt_wlan_scan_result));

    INVALID_INFO(&result->info[0]);

    if (ssid) {
        SSID_SET(&result->info[0], ssid);
    }

    if (0x00 != _netmgmt_ioctl(cmd, result)) {
        free(result);
        printf("[hal_netmgmt]: %s scan failed\n", __FUNCTION__);
        return -1;
    }

    *scan_result = result;

    return 0;
}

/* wlan station */
int netmgmt_wlan_sta_get_auto_reconnect(int* enable)
{
    int auto_reconnect = -1;

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_GET_AUTO_RECONNECT, &auto_reconnect)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (enable) {
        *enable = auto_reconnect;
    }

    return 0;
}

int netmgmt_wlan_sta_set_auto_reconnect(int enable)
{
    int auto_reconnect = enable;

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_SET_AUTO_RECONNECT, &auto_reconnect)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

int netmgmt_wlan_sta_connect_with_ssid(char* ssid, char* password)
{
    size_t                ssid_length = 0;
    struct rt_wlan_ssid_t _ssid;

    if (!ssid) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    ssid_length = strlen(ssid);

    if ((0x00 == ssid_length) || (RT_WLAN_SSID_MAX_LENGTH < ssid_length)) {
        printf("[hal_netmgmt]: %s invalid ssid length\n", __FUNCTION__);
        return -1;
    }

    _ssid.len = ssid_length;
    strncpy((char*)&_ssid.val, ssid, RT_WLAN_SSID_MAX_LENGTH);

    return _netmgmt_ioctl_with_type_rt_wlan_connect_config(IOCTRL_WM_STA_CONNECT, &_ssid, NULL, password);
}

int netmgmt_wlan_sta_connect_with_scan_info(struct rt_wlan_info_t* info, char* password)
{
    if (!info) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    return _netmgmt_ioctl_with_type_rt_wlan_connect_config(IOCTRL_WM_STA_CONNECT, NULL, info, password);
}

int netmgmt_wlan_sta_disconnect_ap(void)
{
    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_STA_DISCONNECT, NULL)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

int netmgmt_wlan_sta_isconnected(int* status)
{
    int _status = -1;

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_STA_IS_CONNECTED, &_status)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (status) {
        *status = _status;
    }

    return 0;
}

int netmgmt_wlan_sta_get_mac(uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH])
{
    uint8_t _mac[RT_WLAN_BSSID_MAX_LENGTH];

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_STA_GET_MAC, &_mac[0])) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (mac) {
        memcpy(mac, _mac, RT_WLAN_BSSID_MAX_LENGTH);
    }

    return 0;
}

int netmgmt_wlan_sta_set_mac(uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH])
{
    uint8_t _mac[RT_WLAN_BSSID_MAX_LENGTH];

    if (mac) {
        memcpy(_mac, mac, RT_WLAN_BSSID_MAX_LENGTH);
    }

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_STA_SET_MAC, &_mac[0])) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

int netmgmt_wlan_sta_get_ap_info(struct rt_wlan_info_t* info)
{
    struct rt_wlan_info_t _info;

    memset(&_info, 0, sizeof(_info));

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_STA_GET_AP_INFO, &_info)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (info) {
        memcpy(info, &_info, sizeof(*info));
    }

    return 0;
}

int netmgmt_wlan_sta_get_rssi(int* rssi)
{
    int _rssi = -100;

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_STA_GET_RSSI, &_rssi)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (rssi) {
        *rssi = _rssi;
    }

    return 0;
}

int netmgmt_wlan_sta_scan(int* ap_num, struct rt_wlan_info_t ap_infos[RT_WLAN_STA_SCAN_MAX_AP])
{
    struct rt_wlan_scan_result* result = NULL;

    int req_ap_num = RT_WLAN_STA_SCAN_MAX_AP;

    if (0x00 != _netmgmt_ioctl_with_type_rt_wlan_scan_result(IOCTRL_WM_STA_SCAN, &result, NULL)) {
        return -1;
    }

    req_ap_num = (result->num < RT_WLAN_STA_SCAN_MAX_AP) ? result->num : RT_WLAN_STA_SCAN_MAX_AP;

    for (int i = 0; i < req_ap_num; i++) {
        memcpy(&ap_infos[i], &result->info[i], sizeof(struct rt_wlan_info_t));
    }
    free(result);

    if (ap_num) {
        *ap_num = req_ap_num;
    }

    return 0;
}

int netmgmt_wlan_sta_scan_with_ssid(char* ssid, struct rt_wlan_info_t* ap_info)
{
    struct rt_wlan_info_t*      info   = NULL;
    struct rt_wlan_scan_result* result = NULL;

    int found      = 0;
    int req_ap_num = RT_WLAN_STA_SCAN_MAX_AP;

    if (0x00 != _netmgmt_ioctl_with_type_rt_wlan_scan_result(IOCTRL_WM_STA_SCAN, &result, ssid)) {
        return -1;
    }
    req_ap_num = (result->num < RT_WLAN_STA_SCAN_MAX_AP) ? result->num : RT_WLAN_STA_SCAN_MAX_AP;

    for (int i = 0; i < req_ap_num; i++) {
        info = &result->info[i];

        if (0x00 == strncmp((char*)&info->ssid.val[0], ssid, sizeof(info->ssid.val))) {
            if (ap_info) {
                memcpy(ap_info, info, sizeof(struct rt_wlan_info_t));
            }

            found = 1;
            break;
        }
    }

    free(result);

    return found ? 0 : -1;
}

// int netmgmt_wlan_get_hostname(char* hostname) { }

// int netmgmt_wlan_set_hostname(char* hostname) { }

/* wlan ap */
int netmgmt_wlan_ap_start_with_ssid(char* ssid, char* password)
{
    size_t                ssid_length = 0;
    struct rt_wlan_ssid_t _ssid;

    if (!ssid || !password) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    ssid_length = strlen(ssid);

    if ((0x00 == ssid_length) || (RT_WLAN_SSID_MAX_LENGTH < ssid_length)) {
        printf("[hal_netmgmt]: %s invalid ssid length\n", __FUNCTION__);
        return -1;
    }

    _ssid.len = ssid_length;
    strncpy((char*)&_ssid.val, ssid, RT_WLAN_SSID_MAX_LENGTH);

    return _netmgmt_ioctl_with_type_rt_wlan_connect_config(IOCTRL_WM_AP_START, &_ssid, NULL, password);
}

int netmgmt_wlan_ap_start_with_info(struct rt_wlan_info_t* info, char* password)
{
    if (!info || !password) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    return _netmgmt_ioctl_with_type_rt_wlan_connect_config(IOCTRL_WM_AP_START, NULL, info, password);
}

int netmgmt_wlan_ap_stop(void)
{
    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_AP_STOP, NULL)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

int netmgmt_wlan_ap_isactived(int* status)
{
    int _status = -1;

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_AP_IS_ACTIVE, &_status)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (status) {
        *status = _status;
    }

    return 0;
}

int netmgmt_wlan_ap_get_info(struct rt_wlan_info_t* info)
{
    struct rt_wlan_info_t _info;

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_AP_GET_INFO, &_info)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (info) {
        memcpy(info, &_info, sizeof(*info));
    }

    return 0;
}

int netmgmt_wlan_ap_get_sta_info(int* sta_num, struct rt_wlan_info_t sta_infos[RT_WLAN_STA_SCAN_MAX_AP])
{
    struct rt_wlan_scan_result* result = NULL;

    int req_sta_num = RT_WLAN_STA_SCAN_MAX_AP;

    if (0x00 != _netmgmt_ioctl_with_type_rt_wlan_scan_result(IOCTRL_WM_AP_GET_STA_INFO, &result, NULL)) {
        return -1;
    }

    req_sta_num = (result->num < RT_WLAN_STA_SCAN_MAX_AP) ? result->num : RT_WLAN_STA_SCAN_MAX_AP;

    for (int i = 0; i < req_sta_num; i++) {
        memcpy(&sta_infos[i], &result->info[i], sizeof(struct rt_wlan_info_t));
    }
    free(result);

    if (sta_num) {
        *sta_num = req_sta_num;
    }

    return 0;
}

int netmgmt_wlan_ap_disconnect_sta(uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH])
{
    uint8_t _mac[RT_WLAN_BSSID_MAX_LENGTH];

    if (!mac) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    memcpy(_mac, mac, sizeof(_mac));

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_AP_DEAUTH_STA, &_mac[0])) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

int netmgmt_wlan_ap_get_country(int* country)
{
    int _country = -1;

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_AP_GET_COUNTRY, &_country)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (country) {
        *country = _country;
    }

    return 0;
}

int netmgmt_wlan_ap_set_country(int country)
{
    int _country = country;

    if (0x00 != _netmgmt_ioctl(IOCTRL_WM_AP_SET_COUNTRY, &_country)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

/* lan */
int netmgmt_lan_get_isconnected(enum rt_netif_t itf, int* status)
{
    struct lan_status_cmd_wrap_t cfg = {
        .itf   = itf,
        .value = -1,
    };

    if (0x00 != _netmgmt_ioctl(IOCTRL_LAN_GET_ISCONNECTED, &cfg)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (status) {
        *status = cfg.value;
    }

    return 0;
}

int netmgmt_lan_get_link_status(enum rt_netif_t itf, int* status)
{
    struct lan_status_cmd_wrap_t cfg = {
        .itf   = itf,
        .value = -1,
    };

    if (0x00 != _netmgmt_ioctl(IOCTRL_LAN_GET_LINK_STATUS, &cfg)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (status) {
        *status = cfg.value;
    }

    return 0;
}

int netmgmt_lan_get_mac(enum rt_netif_t itf, uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH])
{
    struct lan_mac_cmd_wrap_t cfg = {
        .itf = itf,
    };

    if (0x00 != _netmgmt_ioctl(IOCTRL_LAN_GET_MAC, &cfg)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (mac) {
        memcpy(mac, cfg.mac, RT_WLAN_BSSID_MAX_LENGTH);
    }

    return 0;
}

int netmgmt_lan_set_mac(enum rt_netif_t itf, uint8_t mac[RT_WLAN_BSSID_MAX_LENGTH])
{
    struct lan_mac_cmd_wrap_t cfg = {
        .itf = itf,
    };

    if (mac) {
        memcpy(cfg.mac, mac, RT_WLAN_BSSID_MAX_LENGTH);
    }

    if (0x00 != _netmgmt_ioctl(IOCTRL_LAN_SET_MAC, &cfg)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

// int netmgmt_lan_get_hostname(char* hostname) { }

// int netmgmt_lan_set_hostname(char* hostname) { }

/* utils */
int netmgmt_utils_get_defeault_dev(char name[32])
{
    char _name[32];

    if (0x00 != _netmgmt_ioctl(IOCTRL_NET_GET_DEV_DEFAULT, &_name[0])) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (name) {
        strncpy(name, _name, sizeof(_name));
    }

    return 0;
}

int netmgmt_utils_set_defeault_dev(char name[32])
{
    char _name[32];

    if (!name) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    strncpy(_name, name, sizeof(_name));

    if (0x00 != _netmgmt_ioctl(IOCTRL_NET_SET_DEV_DEFAULT, &_name[0])) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

int netmgmt_utils_get_dev_list(int* dev_num, char names[NET_DEV_MAX_CNT][32])
{
    int _dev_num = 0, name_len;

    char _names[32 * NET_DEV_MAX_CNT], *pname = NULL;

    if (0x00 != _netmgmt_ioctl(IOCTRL_NET_GET_DEV_LIST, &_names[0])) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    for (int i = 0; i < NET_DEV_MAX_CNT; i++) {
        pname         = &_names[i * 32];
        pname[32 - 1] = '\0';
        name_len      = strlen(pname);

        if (name_len) {
            _dev_num++;
            strncpy(names[i], pname, 32);
        }
    }

    if (dev_num) {
        *dev_num = _dev_num;
    }

    return 0;
}

int netmgmt_utils_probe_device(enum rt_netif_t itf, int* status)
{
    int _itf = itf;

    if (0x00 != _netmgmt_ioctl(IOCTRL_NET_PROBE, &_itf)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (status) {
        *status = _itf;
    }

    return 0;
}

int netmgmt_utils_get_ifconfig(enum rt_netif_t itf, struct ifconfig_t* config)
{
    struct ifconfig_cmd cmd;

    memset(&cmd, 0, sizeof(cmd));

    cmd.net_if = itf;
    cmd.func   = 0;

    if (0x00 != _netmgmt_ioctl(IOCTRL_NET_IFCONFIG, &cmd)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    if (config) {
        memcpy(&config->ip, &cmd.ip, sizeof(struct ip_addr_t));
        memcpy(&config->gw, &cmd.gw, sizeof(struct ip_addr_t));
        memcpy(&config->netmask, &cmd.netmask, sizeof(struct ip_addr_t));
        memcpy(&config->dns, &cmd.dns, sizeof(struct ip_addr_t));
    }

    return 0;
}

int netmgmt_utils_set_ifconfig_static(enum rt_netif_t itf, struct ifconfig_t* config)
{
    struct ifconfig_cmd cmd;

    if (!config) {
        printf("[hal_netmgmt]: %s invalid args\n", __FUNCTION__);
        return -1;
    }

    memset(&cmd, 0, sizeof(cmd));

    cmd.net_if = itf;
    cmd.func   = 1;

    memcpy(&cmd.ip, &config->ip, sizeof(struct ip_addr_t));
    memcpy(&cmd.gw, &config->gw, sizeof(struct ip_addr_t));
    memcpy(&cmd.netmask, &config->netmask, sizeof(struct ip_addr_t));
    memcpy(&cmd.dns, &config->dns, sizeof(struct ip_addr_t));

    if (0x00 != _netmgmt_ioctl(IOCTRL_NET_IFCONFIG, &cmd)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

int netmgmt_utils_set_ifconfig_dhcp(enum rt_netif_t itf)
{
    struct ifconfig_cmd cmd;

    memset(&cmd, 0, sizeof(cmd));

    cmd.net_if = itf;
    cmd.func   = 2;

    if (0x00 != _netmgmt_ioctl(IOCTRL_NET_IFCONFIG, &cmd)) {
        printf("[hal_netmgmt]: %s failed\n", __FUNCTION__);
        return -1;
    }

    return 0;
}

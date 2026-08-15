/*
 * Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef WLAN_OFFLOAD_CLIENT_H
#define WLAN_OFFLOAD_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WLAN_OFFLOAD_SSID_MAX_LENGTH     32U
#define WLAN_OFFLOAD_SCAN_SSID_MAX       4U
#define WLAN_OFFLOAD_SCAN_CHANNEL_MAX    64U
#define WLAN_OFFLOAD_IE_MAX_LENGTH       1024U
#define WLAN_OFFLOAD_FRAME_MAX_LENGTH    2304U
#define WLAN_OFFLOAD_EAPOL_MAX_LENGTH    1600U
#define WLAN_OFFLOAD_KEY_MAX_LENGTH      64U
#define WLAN_OFFLOAD_SEQUENCE_MAX_LENGTH 16U
#define WLAN_OFFLOAD_DEVICE_NAME_MAX     24U

#define WLAN_OFFLOAD_RADIO_INDEX_NONE    255U

#define WLAN_OFFLOAD_CAP_STA                 (1U << 0)
#define WLAN_OFFLOAD_CAP_AP                  (1U << 1)
#define WLAN_OFFLOAD_CAP_STA_AP_CONCURRENT   (1U << 2)
#define WLAN_OFFLOAD_CAP_POWER_SAVE          (1U << 3)
#define WLAN_OFFLOAD_CAP_MONITOR             (1U << 4)
#define WLAN_OFFLOAD_CAP_EXTERNAL_SUPPLICANT (1U << 5)
#define WLAN_OFFLOAD_CAP_HOTPLUG             (1U << 6)
#define WLAN_OFFLOAD_CAP_SAE_OFFLOAD         (1U << 7)
#define WLAN_OFFLOAD_CAP_4WAY_OFFLOAD        (1U << 8)
#define WLAN_OFFLOAD_CAP_EXTERNAL_AUTH       (1U << 9)

#define WLAN_OFFLOAD_PHY_11B (1U << 0)
#define WLAN_OFFLOAD_PHY_11G (1U << 1)
#define WLAN_OFFLOAD_PHY_11A (1U << 2)
#define WLAN_OFFLOAD_PHY_HT  (1U << 3)
#define WLAN_OFFLOAD_PHY_VHT (1U << 4)
#define WLAN_OFFLOAD_PHY_HE  (1U << 5)
#define WLAN_OFFLOAD_PHY_EHT (1U << 6)

#define WLAN_OFFLOAD_SCAN_PASSIVE     (1U << 0)
#define WLAN_OFFLOAD_SCAN_RANDOM_MAC  (1U << 1)
#define WLAN_OFFLOAD_SCAN_FLUSH_CACHE (1U << 2)

/* Scan security values are a stable copy of the RT-Thread WLAN wire values.
 * Applications must not include the kernel wlan_dev.h header. */
typedef uint32_t wlan_offload_security_t;

#define WLAN_OFFLOAD_SECURITY_OPEN                       UINT32_C(0x00000000)
#define WLAN_OFFLOAD_SECURITY_WEP_PSK                    UINT32_C(0x00000001)
#define WLAN_OFFLOAD_SECURITY_WEP_SHARED                 UINT32_C(0x00008001)
#define WLAN_OFFLOAD_SECURITY_WPA_TKIP_PSK               UINT32_C(0x00200002)
#define WLAN_OFFLOAD_SECURITY_WPA_TKIP_8021X             UINT32_C(0x80200002)
#define WLAN_OFFLOAD_SECURITY_WPA_AES_PSK                UINT32_C(0x00200004)
#define WLAN_OFFLOAD_SECURITY_WPA_AES_8021X              UINT32_C(0x80200004)
#define WLAN_OFFLOAD_SECURITY_WPA2_AES_PSK               UINT32_C(0x00400004)
#define WLAN_OFFLOAD_SECURITY_WPA2_AES_8021X             UINT32_C(0x80400004)
#define WLAN_OFFLOAD_SECURITY_WPA2_TKIP_PSK              UINT32_C(0x00400002)
#define WLAN_OFFLOAD_SECURITY_WPA2_TKIP_8021X            UINT32_C(0x80400002)
#define WLAN_OFFLOAD_SECURITY_WPA2_MIXED_PSK             UINT32_C(0x00400006)
#define WLAN_OFFLOAD_SECURITY_WPA_WPA2_MIXED_PSK         UINT32_C(0x00600000)
#define WLAN_OFFLOAD_SECURITY_WPA_WPA2_MIXED_8021X       UINT32_C(0x80600000)
#define WLAN_OFFLOAD_SECURITY_WPA2_AES_CMAC              UINT32_C(0x00400010)
#define WLAN_OFFLOAD_SECURITY_WPS_OPEN                   UINT32_C(0x10000000)
#define WLAN_OFFLOAD_SECURITY_WPS_SECURE                 UINT32_C(0x10000004)
#define WLAN_OFFLOAD_SECURITY_WPA3_AES_PSK_LEGACY        UINT32_C(0x00800004)
#define WLAN_OFFLOAD_SECURITY_WPA3_AES_PSK \
    WLAN_OFFLOAD_SECURITY_WPA3_AES_PSK_LEGACY
#define WLAN_OFFLOAD_SECURITY_WPA3_SAE                   UINT32_C(0x00800024)
#define WLAN_OFFLOAD_SECURITY_WPA2_WPA3_MIXED_PSK       UINT32_C(0x00c00024)
#define WLAN_OFFLOAD_SECURITY_WPA3_AES_8021X             UINT32_C(0x80800004)
#define WLAN_OFFLOAD_SECURITY_WPA2_WPA3_MIXED_8021X     UINT32_C(0x80c00004)
#define WLAN_OFFLOAD_SECURITY_WPA3_192BIT_8021X          UINT32_C(0x80810004)
#define WLAN_OFFLOAD_SECURITY_OWE                        UINT32_C(0x00000044)
#define WLAN_OFFLOAD_SECURITY_OWE_TRANSITION             UINT32_C(0x00020044)
#define WLAN_OFFLOAD_SECURITY_WPA2_AES_PSK_SHA256        UINT32_C(0x00400104)
#define WLAN_OFFLOAD_SECURITY_WPA3_AES_PSK_SHA384        UINT32_C(0x00800204)
#define WLAN_OFFLOAD_SECURITY_WPA2_AES_8021X_SHA256      UINT32_C(0x80400104)
#define WLAN_OFFLOAD_SECURITY_FT_WPA2_AES_PSK            UINT32_C(0x00400084)
#define WLAN_OFFLOAD_SECURITY_FT_WPA3_AES_PSK_SHA384     UINT32_C(0x00800284)
#define WLAN_OFFLOAD_SECURITY_FT_WPA2_AES_8021X          UINT32_C(0x80400084)
#define WLAN_OFFLOAD_SECURITY_FT_WPA3_SAE                UINT32_C(0x008000a4)
#define WLAN_OFFLOAD_SECURITY_FT_WPA3_8021X_SHA384       UINT32_C(0x80800284)
#define WLAN_OFFLOAD_SECURITY_WPA3_SAE_EXT_KEY           UINT32_C(0x00840024)
#define WLAN_OFFLOAD_SECURITY_FT_WPA3_SAE_EXT_KEY        UINT32_C(0x008400a4)
#define WLAN_OFFLOAD_SECURITY_FILS_SHA256                UINT32_C(0x00000500)
#define WLAN_OFFLOAD_SECURITY_FILS_SHA384                UINT32_C(0x00000600)
#define WLAN_OFFLOAD_SECURITY_FT_FILS_SHA256             UINT32_C(0x00000580)
#define WLAN_OFFLOAD_SECURITY_FT_FILS_SHA384             UINT32_C(0x00000680)
#define WLAN_OFFLOAD_SECURITY_DPP                        UINT32_C(0x00000800)
#define WLAN_OFFLOAD_SECURITY_OSEN                       UINT32_C(0x00001000)
#define WLAN_OFFLOAD_SECURITY_WAPI_PSK                   UINT32_C(0x00002000)
#define WLAN_OFFLOAD_SECURITY_WAPI_CERT                  UINT32_C(0x80002000)
#define WLAN_OFFLOAD_SECURITY_CCKM                       UINT32_C(0x80004000)
#define WLAN_OFFLOAD_SECURITY_UNKNOWN                    UINT32_MAX

enum wlan_offload_interface_type
{
    WLAN_OFFLOAD_INTERFACE_STATION = 0,
    WLAN_OFFLOAD_INTERFACE_AP = 1,
    WLAN_OFFLOAD_INTERFACE_NONE = 255,
};

enum wlan_offload_band
{
    WLAN_OFFLOAD_BAND_2GHZ = 0,
    WLAN_OFFLOAD_BAND_5GHZ,
    WLAN_OFFLOAD_BAND_6GHZ,
    WLAN_OFFLOAD_BAND_UNSPECIFIED = 255,
};

enum wlan_offload_channel_width
{
    WLAN_OFFLOAD_CHANNEL_WIDTH_20_NOHT = 0,
    WLAN_OFFLOAD_CHANNEL_WIDTH_20,
    WLAN_OFFLOAD_CHANNEL_WIDTH_40,
    WLAN_OFFLOAD_CHANNEL_WIDTH_80,
    WLAN_OFFLOAD_CHANNEL_WIDTH_80P80,
    WLAN_OFFLOAD_CHANNEL_WIDTH_160,
    WLAN_OFFLOAD_CHANNEL_WIDTH_320,
};

enum wlan_offload_auth_type
{
    WLAN_OFFLOAD_AUTH_OPEN = 0,
    WLAN_OFFLOAD_AUTH_SHARED,
    WLAN_OFFLOAD_AUTH_FT,
    WLAN_OFFLOAD_AUTH_SAE,
    WLAN_OFFLOAD_AUTH_AUTOMATIC,
};

enum wlan_offload_cipher
{
    WLAN_OFFLOAD_CIPHER_NONE = 0,
    WLAN_OFFLOAD_CIPHER_WEP40,
    WLAN_OFFLOAD_CIPHER_WEP104,
    WLAN_OFFLOAD_CIPHER_TKIP,
    WLAN_OFFLOAD_CIPHER_CCMP,
    WLAN_OFFLOAD_CIPHER_CCMP_256,
    WLAN_OFFLOAD_CIPHER_GCMP,
    WLAN_OFFLOAD_CIPHER_GCMP_256,
    WLAN_OFFLOAD_CIPHER_AES_CMAC,
};

enum wlan_offload_event_type
{
    WLAN_OFFLOAD_EVENT_RADIO_ONLINE = 0,
    WLAN_OFFLOAD_EVENT_RADIO_OFFLINE,
    WLAN_OFFLOAD_EVENT_SCAN_RESULT,
    WLAN_OFFLOAD_EVENT_SCAN_DONE,
    WLAN_OFFLOAD_EVENT_CONNECT_RESULT,
    WLAN_OFFLOAD_EVENT_DISCONNECTED,
    WLAN_OFFLOAD_EVENT_AUTH_RX,
    WLAN_OFFLOAD_EVENT_ASSOC_RX,
    WLAN_OFFLOAD_EVENT_MGMT_RX,
    WLAN_OFFLOAD_EVENT_MGMT_TX_STATUS,
    WLAN_OFFLOAD_EVENT_EAPOL_RX,
    WLAN_OFFLOAD_EVENT_REGULATORY_CHANGED,
    WLAN_OFFLOAD_EVENT_FIRMWARE_ERROR,
    WLAN_OFFLOAD_EVENT_EXTERNAL_AUTH_REQUIRED,
};

struct wlan_offload_handle;

struct wlan_offload_ssid
{
    uint8_t length;
    uint8_t value[WLAN_OFFLOAD_SSID_MAX_LENGTH];
};

struct wlan_offload_channel
{
    enum wlan_offload_band band;
    enum wlan_offload_channel_width width;
    uint16_t primary_channel;
    uint16_t primary_frequency_mhz;
    uint16_t center_frequency1_mhz;
    uint16_t center_frequency2_mhz;
};

struct wlan_offload_info
{
    uint32_t capabilities;
    uint32_t phy_capabilities;
    uint32_t cipher_mask;
    uint32_t interface_type_mask;
    uint32_t max_frame_size;
    uint32_t framework_api_version;
    uint32_t firmware_protocol_version;
    uint32_t firmware_version;
    uint32_t firmware_features;
    uint32_t firmware_generation;
    uint16_t max_scan_ie_length;
    uint16_t max_stations;
    uint8_t max_scan_ssids;
    uint8_t band_mask;
    uint8_t max_vifs;
    uint8_t max_channel_contexts;
    uint8_t address[6];
};

/*
 * Device names the kernel assigned to this radio. An empty string means the
 * interface does not exist, and radio_index is WLAN_OFFLOAD_RADIO_INDEX_NONE
 * when no interface is registered.
 */
struct wlan_offload_names
{
    char control[WLAN_OFFLOAD_DEVICE_NAME_MAX];
    char station[WLAN_OFFLOAD_DEVICE_NAME_MAX];
    char ap[WLAN_OFFLOAD_DEVICE_NAME_MAX];
    uint8_t radio_index;
};

struct wlan_offload_scan_params
{
    uint32_t flags;
    uint16_t duration_ms;
    const struct wlan_offload_ssid *ssids;
    size_t ssid_count;
    const struct wlan_offload_channel *channels;
    size_t channel_count;
    uint8_t bssid[6];
    const uint8_t *ies;
    size_t ies_length;
};

struct wlan_offload_auth_params
{
    struct wlan_offload_ssid ssid;
    uint8_t bssid[6];
    struct wlan_offload_channel channel;
    enum wlan_offload_auth_type auth_type;
    const uint8_t *data;
    size_t data_length;
};

struct wlan_offload_assoc_params
{
    uint8_t bssid[6];
    const uint8_t *ies;
    size_t ies_length;
};

struct wlan_offload_key
{
    enum wlan_offload_cipher cipher;
    uint8_t index;
    uint8_t pairwise;
    uint8_t set_transmit;
    uint8_t peer[6];
    const uint8_t *data;
    size_t data_length;
    const uint8_t *sequence;
    size_t sequence_length;
};

struct wlan_offload_mgmt_frame
{
    struct wlan_offload_channel channel;
    uint8_t off_channel;
    uint32_t wait_ms;
    uint64_t cookie;
    const uint8_t *data;
    size_t data_length;
};

struct wlan_offload_network
{
    struct wlan_offload_ssid ssid;
    uint8_t bssid[6];
    struct wlan_offload_channel channel;
    int16_t rssi;
    wlan_offload_security_t security;
    uint16_t beacon_interval;
    uint16_t capability;
    uint16_t ies_length;
    uint8_t ies[WLAN_OFFLOAD_IE_MAX_LENGTH];
};

struct wlan_offload_event
{
    enum wlan_offload_event_type type;
    enum wlan_offload_interface_type interface_type;
    uint32_t request_id;
    int status;
    uint8_t truncated;
    uint8_t overflow;
    union
    {
        struct wlan_offload_network network;
        struct
        {
            uint8_t bssid[6];
            uint16_t reason;
            uint8_t locally_generated;
        } disconnected;
        struct
        {
            struct wlan_offload_channel channel;
            int16_t rssi;
            uint16_t length;
            uint8_t data[WLAN_OFFLOAD_FRAME_MAX_LENGTH];
        } frame;
        struct
        {
            uint64_t cookie;
            uint8_t acknowledged;
            uint16_t length;
            uint8_t data[WLAN_OFFLOAD_FRAME_MAX_LENGTH];
        } tx_status;
        struct
        {
            uint8_t source[6];
            uint8_t destination[6];
            uint16_t length;
            uint8_t data[WLAN_OFFLOAD_EAPOL_MAX_LENGTH];
        } eapol;
        struct
        {
            uint32_t reason;
            uint16_t dump_length;
            uint8_t dump[WLAN_OFFLOAD_IE_MAX_LENGTH];
        } firmware;
        struct
        {
            struct wlan_offload_ssid ssid;
            uint8_t bssid[6];
            uint32_t akm_suite;
        } external_auth;
    } data;
};

int wlan_offload_open(struct wlan_offload_handle **handle, const char *device_path);
void wlan_offload_close(struct wlan_offload_handle *handle);
int wlan_offload_get_fd(const struct wlan_offload_handle *handle);

int wlan_offload_get_info(struct wlan_offload_handle *handle,
                     enum wlan_offload_interface_type interface_type,
                     struct wlan_offload_info *info);
/* Returns -ENOSYS against a kernel that predates the names command. */
int wlan_offload_get_names(struct wlan_offload_handle *handle,
                      enum wlan_offload_interface_type interface_type,
                      struct wlan_offload_names *names);
int wlan_offload_set_interface(struct wlan_offload_handle *handle,
                          enum wlan_offload_interface_type interface_type,
                          int enabled, uint32_t *request_id);
int wlan_offload_scan(struct wlan_offload_handle *handle,
                 enum wlan_offload_interface_type interface_type,
                 const struct wlan_offload_scan_params *params,
                 uint32_t *request_id);
int wlan_offload_abort_scan(struct wlan_offload_handle *handle,
                       enum wlan_offload_interface_type interface_type,
                       uint32_t request_id);
int wlan_offload_authenticate(struct wlan_offload_handle *handle,
                         enum wlan_offload_interface_type interface_type,
                         const struct wlan_offload_auth_params *params,
                         uint32_t *request_id);
int wlan_offload_associate(struct wlan_offload_handle *handle,
                      enum wlan_offload_interface_type interface_type,
                      const struct wlan_offload_assoc_params *params,
                      uint32_t *request_id);
int wlan_offload_disconnect(struct wlan_offload_handle *handle,
                       enum wlan_offload_interface_type interface_type,
                       uint16_t reason, uint32_t *request_id);
int wlan_offload_set_key(struct wlan_offload_handle *handle,
                    enum wlan_offload_interface_type interface_type,
                    const struct wlan_offload_key *key, uint32_t *request_id);
int wlan_offload_delete_key(struct wlan_offload_handle *handle,
                       enum wlan_offload_interface_type interface_type,
                       uint8_t index, int pairwise, const uint8_t *peer,
                       uint32_t *request_id);
int wlan_offload_set_default_key(struct wlan_offload_handle *handle,
                            enum wlan_offload_interface_type interface_type,
                            uint8_t index, int unicast, int multicast,
                            uint32_t *request_id);
int wlan_offload_send_mgmt(struct wlan_offload_handle *handle,
                      enum wlan_offload_interface_type interface_type,
                      const struct wlan_offload_mgmt_frame *frame,
                      uint32_t *request_id);
int wlan_offload_send_eapol(struct wlan_offload_handle *handle,
                       enum wlan_offload_interface_type interface_type,
                       const uint8_t destination[6], const void *data,
                       size_t data_length, uint32_t *request_id);
int wlan_offload_external_auth_response(struct wlan_offload_handle *handle,
                                   enum wlan_offload_interface_type interface_type,
                                   uint32_t request_id, uint16_t status);

/* timeout_ms is -1 for infinite wait, 0 for nonblocking, or a timeout. */
int wlan_offload_receive_event(struct wlan_offload_handle *handle,
                          struct wlan_offload_event *event, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* WLAN_OFFLOAD_CLIENT_H */

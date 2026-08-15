/*
 * Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <wlan_offload_client.h>

#include "wlan_offload_wire.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct wlan_offload_handle
{
    int fd;
    uint32_t next_request_id;
};

_Static_assert(RTWO_CTRL_VERSION == 2 && RTWO_CTRL_CMD_GET_INFO == 1 &&
               RTWO_CTRL_CMD_EAPOL_TX == 12 &&
               RTWO_CTRL_CMD_EXTERNAL_AUTH_RESPONSE == 13 &&
               RTWO_CTRL_CMD_GET_NAMES == 14 &&
               RTWO_CTRL_EVENT_INFO == 0x8000 &&
               RTWO_CTRL_EVENT_FIRMWARE_ERROR == 0x800d &&
               RTWO_CTRL_EVENT_EXTERNAL_AUTH_REQUIRED == 0x800e &&
               RTWO_CTRL_EVENT_NAMES == 0x800f,
               "wlan_offload wire identifiers changed");
_Static_assert(sizeof(struct rtwo_ctrl_header) == 20,
               "wlan_offload wire header ABI changed");
_Static_assert(sizeof(struct rtwo_ctrl_info) == 56,
               "wlan_offload info ABI changed");
_Static_assert(sizeof(struct rtwo_ctrl_names) == 76 &&
               RTWO_CTRL_MAX_DEVICE_NAME == WLAN_OFFLOAD_DEVICE_NAME_MAX,
               "wlan_offload names ABI changed");
_Static_assert(sizeof(struct rtwo_ctrl_scan_request) == 1940,
               "wlan_offload scan ABI changed");
_Static_assert(sizeof(struct rtwo_ctrl_auth_request) == 1082,
               "wlan_offload auth ABI changed");
_Static_assert(sizeof(struct rtwo_ctrl_mgmt_frame) == 2344,
               "wlan_offload management frame ABI changed");
_Static_assert(sizeof(struct rtwo_ctrl_external_auth) == 44 &&
               sizeof(struct rtwo_ctrl_external_auth_response) == 4,
               "wlan_offload external-auth ABI changed");
_Static_assert(WLAN_OFFLOAD_SECURITY_WPA3_SAE == UINT32_C(0x00800024) &&
               WLAN_OFFLOAD_SECURITY_WPA2_WPA3_MIXED_PSK ==
                   UINT32_C(0x00c00024) &&
               WLAN_OFFLOAD_SECURITY_WPA3_AES_PSK_SHA384 ==
                   UINT32_C(0x00800204) &&
               WLAN_OFFLOAD_SECURITY_FT_WPA3_AES_PSK_SHA384 ==
                   UINT32_C(0x00800284) &&
               WLAN_OFFLOAD_SECURITY_WAPI_CERT == UINT32_C(0x80002000) &&
               WLAN_OFFLOAD_SECURITY_UNKNOWN == UINT32_MAX,
               "wlan_offload security ABI changed");

static int wlan_offload_validate_handle(const struct wlan_offload_handle *handle)
{
    return handle && handle->fd >= 0 ? 0 : -EINVAL;
}

static int wlan_offload_validate_interface(enum wlan_offload_interface_type interface_type)
{
    return interface_type == WLAN_OFFLOAD_INTERFACE_STATION ||
           interface_type == WLAN_OFFLOAD_INTERFACE_AP ? 0 : -EINVAL;
}

static int wlan_offload_channel_geometry_valid(const struct wlan_offload_channel *channel)
{
    uint16_t offset = channel->center_frequency1_mhz >
                      channel->primary_frequency_mhz ?
                      channel->center_frequency1_mhz -
                      channel->primary_frequency_mhz :
                      channel->primary_frequency_mhz -
                      channel->center_frequency1_mhz;

    switch (channel->width)
    {
    case WLAN_OFFLOAD_CHANNEL_WIDTH_20_NOHT:
    case WLAN_OFFLOAD_CHANNEL_WIDTH_20:
        return !offset && !channel->center_frequency2_mhz;
    case WLAN_OFFLOAD_CHANNEL_WIDTH_40:
        return offset == 10 && !channel->center_frequency2_mhz;
    case WLAN_OFFLOAD_CHANNEL_WIDTH_80:
        return (offset == 10 || offset == 30) &&
               !channel->center_frequency2_mhz;
    case WLAN_OFFLOAD_CHANNEL_WIDTH_80P80:
        return (offset == 10 || offset == 30) &&
               channel->center_frequency2_mhz &&
               channel->center_frequency2_mhz !=
                   channel->center_frequency1_mhz;
    case WLAN_OFFLOAD_CHANNEL_WIDTH_160:
        return (offset == 10 || offset == 30 || offset == 50 ||
                offset == 70) && !channel->center_frequency2_mhz;
    case WLAN_OFFLOAD_CHANNEL_WIDTH_320:
        return offset >= 10 && offset <= 150 && offset % 20 == 10 &&
               !channel->center_frequency2_mhz;
    default:
        return 0;
    }
}

static int wlan_offload_channel_to_wire(struct rtwo_ctrl_channel *wire,
                                   const struct wlan_offload_channel *channel,
                                   int allow_unspecified)
{
    if (!wire || !channel || channel->width < WLAN_OFFLOAD_CHANNEL_WIDTH_20_NOHT ||
        channel->width > WLAN_OFFLOAD_CHANNEL_WIDTH_320)
    {
        return -EINVAL;
    }
    if (channel->band == WLAN_OFFLOAD_BAND_UNSPECIFIED)
    {
        if (!allow_unspecified || channel->primary_channel ||
            channel->primary_frequency_mhz ||
            channel->center_frequency1_mhz ||
            channel->center_frequency2_mhz)
        {
            return -EINVAL;
        }
    }
    else if (channel->band < WLAN_OFFLOAD_BAND_2GHZ ||
             channel->band > WLAN_OFFLOAD_BAND_6GHZ ||
             !channel->primary_channel || !channel->primary_frequency_mhz ||
             !channel->center_frequency1_mhz ||
             (channel->width == WLAN_OFFLOAD_CHANNEL_WIDTH_80P80 &&
              !channel->center_frequency2_mhz) ||
             !wlan_offload_channel_geometry_valid(channel))
    {
        return -EINVAL;
    }

    memset(wire, 0, sizeof(*wire));
    wire->band = channel->band == WLAN_OFFLOAD_BAND_UNSPECIFIED ?
                 RTWO_CTRL_BAND_UNSPECIFIED : (uint8_t)channel->band;
    wire->width = (uint8_t)channel->width;
    wire->primary_channel = channel->primary_channel;
    wire->primary_frequency_mhz = channel->primary_frequency_mhz;
    wire->center_frequency1_mhz = channel->center_frequency1_mhz;
    wire->center_frequency2_mhz = channel->center_frequency2_mhz;
    return 0;
}

static int wlan_offload_channel_from_wire(struct wlan_offload_channel *channel,
                                     const struct rtwo_ctrl_channel *wire,
                                     int allow_unspecified)
{
    struct wlan_offload_channel decoded;
    struct rtwo_ctrl_channel checked;

    if (!channel || !wire || wire->reserved[0] || wire->reserved[1] ||
        wire->width > RTWO_CTRL_CHANNEL_WIDTH_320)
    {
        return -EPROTO;
    }
    memset(&decoded, 0, sizeof(decoded));
    decoded.band = wire->band == RTWO_CTRL_BAND_UNSPECIFIED ?
                   WLAN_OFFLOAD_BAND_UNSPECIFIED :
                   (enum wlan_offload_band)wire->band;
    decoded.width = (enum wlan_offload_channel_width)wire->width;
    decoded.primary_channel = wire->primary_channel;
    decoded.primary_frequency_mhz = wire->primary_frequency_mhz;
    decoded.center_frequency1_mhz = wire->center_frequency1_mhz;
    decoded.center_frequency2_mhz = wire->center_frequency2_mhz;
    if (wlan_offload_channel_to_wire(&checked, &decoded, allow_unspecified))
    {
        return -EPROTO;
    }
    *channel = decoded;
    return 0;
}

static uint32_t wlan_offload_next_request_id(struct wlan_offload_handle *handle)
{
    uint32_t request_id = __atomic_add_fetch(&handle->next_request_id, 1,
                                             __ATOMIC_RELAXED);

    if (!request_id)
    {
        request_id = __atomic_add_fetch(&handle->next_request_id, 1,
                                        __ATOMIC_RELAXED);
    }
    return request_id;
}

static int wlan_offload_send_command_id(struct wlan_offload_handle *handle,
                                   uint16_t type,
                                   enum wlan_offload_interface_type interface_type,
                                   uint32_t request_id, const void *payload,
                                   size_t payload_length)
{
    struct rtwo_ctrl_header *header;
    uint8_t *message;
    size_t length;
    ssize_t written;
    int result;

    result = wlan_offload_validate_handle(handle);
    if (result || wlan_offload_validate_interface(interface_type) || !request_id ||
        (payload_length && !payload))
    {
        return -EINVAL;
    }
    length = sizeof(*header) + payload_length;
    if (length > RTWO_CTRL_MAX_MESSAGE_SIZE)
    {
        return -EMSGSIZE;
    }
    message = calloc(1, length);
    if (!message)
    {
        return -ENOMEM;
    }
    header = (struct rtwo_ctrl_header *)message;
    header->version = RTWO_CTRL_VERSION;
    header->type = type;
    header->length = (uint32_t)length;
    header->request_id = request_id;
    header->iftype = (uint8_t)interface_type;
    if (payload_length)
    {
        memcpy(message + sizeof(*header), payload, payload_length);
    }
    do
    {
        written = write(handle->fd, message, length);
    } while (written < 0 && errno == EINTR);
    if (written < 0)
    {
        result = -errno;
    }
    else if ((size_t)written != length)
    {
        result = -EIO;
    }
    else
    {
        result = 0;
    }
    free(message);
    return result;
}

static int wlan_offload_send_command(struct wlan_offload_handle *handle, uint16_t type,
                                enum wlan_offload_interface_type interface_type,
                                const void *payload, size_t payload_length,
                                uint32_t *request_id)
{
    uint32_t id;
    int result;

    if (wlan_offload_validate_handle(handle))
    {
        return -EINVAL;
    }
    id = wlan_offload_next_request_id(handle);
    result = wlan_offload_send_command_id(handle, type, interface_type, id,
                                     payload, payload_length);
    if (!result && request_id)
    {
        *request_id = id;
    }
    return result;
}

static int wlan_offload_read_message(struct wlan_offload_handle *handle, uint8_t *message,
                                size_t capacity, int timeout_ms,
                                size_t *message_length)
{
    struct pollfd descriptor;
    struct rtwo_ctrl_header *header;
    ssize_t length;
    int result;

    if (wlan_offload_validate_handle(handle) || !message ||
        capacity < sizeof(*header) || !message_length || timeout_ms < -1)
    {
        return -EINVAL;
    }
    if (timeout_ms >= 0)
    {
        descriptor.fd = handle->fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        do
        {
            result = poll(&descriptor, 1, timeout_ms);
        } while (result < 0 && errno == EINTR);
        if (result < 0)
        {
            return -errno;
        }
        if (!result)
        {
            return -ETIMEDOUT;
        }
        if (!(descriptor.revents & POLLIN) &&
            (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            return -ENODEV;
        }
    }
    do
    {
        length = read(handle->fd, message, capacity);
    } while (length < 0 && errno == EINTR);
    if (length < 0)
    {
        return -errno;
    }
    if ((size_t)length < sizeof(*header))
    {
        return -EPROTO;
    }
    header = (struct rtwo_ctrl_header *)message;
    if (header->version != RTWO_CTRL_VERSION ||
        header->length != (uint32_t)length || header->length > capacity ||
        header->reserved ||
        (header->iftype > WLAN_OFFLOAD_INTERFACE_AP &&
         header->iftype != WLAN_OFFLOAD_INTERFACE_NONE) ||
        (header->flags & ~(RTWO_CTRL_FLAG_TRUNCATED |
                           RTWO_CTRL_FLAG_OVERFLOW)))
    {
        return -EPROTO;
    }
    *message_length = length;
    return 0;
}

int wlan_offload_open(struct wlan_offload_handle **handle, const char *device_path)
{
    struct wlan_offload_handle *new_handle;
    int descriptor;

    if (!handle || !device_path || !device_path[0])
    {
        return -EINVAL;
    }
    descriptor = open(device_path, O_RDWR | O_CLOEXEC);
    if (descriptor < 0)
    {
        return -errno;
    }
    new_handle = calloc(1, sizeof(*new_handle));
    if (!new_handle)
    {
        close(descriptor);
        return -ENOMEM;
    }
    new_handle->fd = descriptor;
    *handle = new_handle;
    return 0;
}

void wlan_offload_close(struct wlan_offload_handle *handle)
{
    if (!handle)
    {
        return;
    }
    if (handle->fd >= 0)
    {
        close(handle->fd);
    }
    handle->fd = -1;
    free(handle);
}

int wlan_offload_get_fd(const struct wlan_offload_handle *handle)
{
    return wlan_offload_validate_handle(handle) ? -EINVAL : handle->fd;
}

int wlan_offload_get_info(struct wlan_offload_handle *handle,
                     enum wlan_offload_interface_type interface_type,
                     struct wlan_offload_info *info)
{
    uint8_t message[RTWO_CTRL_MAX_MESSAGE_SIZE];
    const struct rtwo_ctrl_header *header;
    const struct rtwo_ctrl_info *wire;
    uint32_t request_id;
    size_t length;
    int result;

    if (!info)
    {
        return -EINVAL;
    }
    result = wlan_offload_send_command(handle, RTWO_CTRL_CMD_GET_INFO,
                                  interface_type, NULL, 0, &request_id);
    if (result)
    {
        return result;
    }
    result = wlan_offload_read_message(handle, message, sizeof(message), 1000,
                                  &length);
    if (result)
    {
        return result;
    }
    header = (const struct rtwo_ctrl_header *)message;
    if (header->type != RTWO_CTRL_EVENT_INFO ||
        header->request_id != request_id || header->status ||
        length != sizeof(*header) + sizeof(*wire))
    {
        return -EPROTO;
    }
    wire = (const struct rtwo_ctrl_info *)(message + sizeof(*header));
    memset(info, 0, sizeof(*info));
    info->capabilities = wire->capabilities;
    info->phy_capabilities = wire->phy_capabilities;
    info->cipher_mask = wire->cipher_mask;
    info->interface_type_mask = wire->iftype_mask;
    info->max_frame_size = wire->max_frame_size;
    info->framework_api_version = wire->framework_api_version;
    info->firmware_protocol_version = wire->firmware_protocol_version;
    info->firmware_version = wire->firmware_version;
    info->firmware_features = wire->firmware_features;
    info->firmware_generation = wire->firmware_generation;
    info->max_scan_ie_length = wire->max_scan_ie_length;
    info->max_stations = wire->max_stations;
    info->max_scan_ssids = wire->max_scan_ssids;
    info->band_mask = wire->band_mask;
    info->max_vifs = wire->max_vifs;
    info->max_channel_contexts = wire->max_channel_contexts;
    memcpy(info->address, wire->address, sizeof(info->address));
    return 0;
}

int wlan_offload_get_names(struct wlan_offload_handle *handle,
                      enum wlan_offload_interface_type interface_type,
                      struct wlan_offload_names *names)
{
    uint8_t message[RTWO_CTRL_MAX_MESSAGE_SIZE];
    const struct rtwo_ctrl_header *header;
    const struct rtwo_ctrl_names *wire;
    uint32_t request_id;
    size_t length;
    int result;

    if (!names)
    {
        return -EINVAL;
    }
    result = wlan_offload_send_command(handle, RTWO_CTRL_CMD_GET_NAMES,
                                  interface_type, NULL, 0, &request_id);
    if (result)
    {
        return result;
    }
    result = wlan_offload_read_message(handle, message, sizeof(message), 1000,
                                  &length);
    if (result)
    {
        return result;
    }
    header = (const struct rtwo_ctrl_header *)message;
    if (header->type != RTWO_CTRL_EVENT_NAMES ||
        header->request_id != request_id || header->status ||
        length != sizeof(*header) + sizeof(*wire))
    {
        return -EPROTO;
    }
    wire = (const struct rtwo_ctrl_names *)(message + sizeof(*header));
    memset(names, 0, sizeof(*names));
    memcpy(names->control, wire->control, sizeof(names->control));
    memcpy(names->station, wire->station, sizeof(names->station));
    memcpy(names->ap, wire->ap, sizeof(names->ap));
    names->control[sizeof(names->control) - 1] = '\0';
    names->station[sizeof(names->station) - 1] = '\0';
    names->ap[sizeof(names->ap) - 1] = '\0';
    names->radio_index = wire->radio_index;
    return 0;
}

int wlan_offload_set_interface(struct wlan_offload_handle *handle,
                          enum wlan_offload_interface_type interface_type,
                          int enabled, uint32_t *request_id)
{
    struct rtwo_ctrl_set_interface request;

    if (enabled != 0 && enabled != 1)
    {
        return -EINVAL;
    }
    memset(&request, 0, sizeof(request));
    request.enabled = enabled;
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_SET_INTERFACE,
                                interface_type, &request, sizeof(request),
                                request_id);
}

int wlan_offload_scan(struct wlan_offload_handle *handle,
                 enum wlan_offload_interface_type interface_type,
                 const struct wlan_offload_scan_params *params,
                 uint32_t *request_id)
{
    struct rtwo_ctrl_scan_request request;
    size_t index;

    if (!params || params->ssid_count > RTWO_CTRL_MAX_SCAN_SSIDS ||
        params->channel_count > RTWO_CTRL_MAX_SCAN_CHANNELS ||
        params->ies_length > RTWO_CTRL_MAX_IE_LENGTH ||
        (params->flags & ~(WLAN_OFFLOAD_SCAN_PASSIVE |
                           WLAN_OFFLOAD_SCAN_RANDOM_MAC |
                           WLAN_OFFLOAD_SCAN_FLUSH_CACHE)) ||
        (params->ssid_count && !params->ssids) ||
        (params->channel_count && !params->channels) ||
        (params->ies_length && !params->ies))
    {
        return -EINVAL;
    }
    memset(&request, 0, sizeof(request));
    request.flags = params->flags;
    request.duration_ms = params->duration_ms;
    request.ssid_count = params->ssid_count;
    request.channel_count = params->channel_count;
    request.ies_length = params->ies_length;
    memcpy(request.bssid, params->bssid, sizeof(request.bssid));
    for (index = 0; index < params->ssid_count; index++)
    {
        if (params->ssids[index].length > RTWO_CTRL_MAX_SSID_LENGTH)
        {
            return -EINVAL;
        }
        request.ssids[index].length = params->ssids[index].length;
        memcpy(request.ssids[index].value, params->ssids[index].value,
               request.ssids[index].length);
    }
    for (index = 0; index < params->channel_count; index++)
    {
        if (wlan_offload_channel_to_wire(&request.channels[index],
                                    &params->channels[index], 0))
        {
            return -EINVAL;
        }
    }
    if (params->ies_length)
    {
        memcpy(request.ies, params->ies, params->ies_length);
    }
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_SCAN, interface_type,
                                &request, sizeof(request), request_id);
}

int wlan_offload_abort_scan(struct wlan_offload_handle *handle,
                       enum wlan_offload_interface_type interface_type,
                       uint32_t request_id)
{
    return wlan_offload_send_command_id(handle, RTWO_CTRL_CMD_ABORT_SCAN,
                                   interface_type, request_id, NULL, 0);
}

int wlan_offload_authenticate(struct wlan_offload_handle *handle,
                         enum wlan_offload_interface_type interface_type,
                         const struct wlan_offload_auth_params *params,
                         uint32_t *request_id)
{
    struct rtwo_ctrl_auth_request request;

    if (!params || params->ssid.length > RTWO_CTRL_MAX_SSID_LENGTH ||
        params->auth_type < WLAN_OFFLOAD_AUTH_OPEN ||
        params->auth_type > WLAN_OFFLOAD_AUTH_AUTOMATIC ||
        params->data_length > RTWO_CTRL_MAX_IE_LENGTH ||
        (params->data_length && !params->data))
    {
        return -EINVAL;
    }
    memset(&request, 0, sizeof(request));
    request.ssid.length = params->ssid.length;
    memcpy(request.ssid.value, params->ssid.value, request.ssid.length);
    memcpy(request.bssid, params->bssid, sizeof(request.bssid));
    if (wlan_offload_channel_to_wire(&request.channel, &params->channel, 0))
    {
        return -EINVAL;
    }
    request.auth_type = params->auth_type;
    request.data_length = params->data_length;
    if (params->data_length)
    {
        memcpy(request.data, params->data, params->data_length);
    }
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_AUTHENTICATE,
                                interface_type, &request, sizeof(request),
                                request_id);
}

int wlan_offload_associate(struct wlan_offload_handle *handle,
                      enum wlan_offload_interface_type interface_type,
                      const struct wlan_offload_assoc_params *params,
                      uint32_t *request_id)
{
    struct rtwo_ctrl_assoc_request request;

    if (!params || params->ies_length > RTWO_CTRL_MAX_IE_LENGTH ||
        (params->ies_length && !params->ies))
    {
        return -EINVAL;
    }
    memset(&request, 0, sizeof(request));
    memcpy(request.bssid, params->bssid, sizeof(request.bssid));
    request.ies_length = params->ies_length;
    if (params->ies_length)
    {
        memcpy(request.ies, params->ies, params->ies_length);
    }
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_ASSOCIATE,
                                interface_type, &request, sizeof(request),
                                request_id);
}

int wlan_offload_disconnect(struct wlan_offload_handle *handle,
                       enum wlan_offload_interface_type interface_type,
                       uint16_t reason, uint32_t *request_id)
{
    struct rtwo_ctrl_disconnect_request request;

    memset(&request, 0, sizeof(request));
    request.reason = reason;
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_DISCONNECT,
                                interface_type, &request, sizeof(request),
                                request_id);
}

int wlan_offload_set_key(struct wlan_offload_handle *handle,
                    enum wlan_offload_interface_type interface_type,
                    const struct wlan_offload_key *key, uint32_t *request_id)
{
    struct rtwo_ctrl_key_request request;

    if (!key || key->cipher < WLAN_OFFLOAD_CIPHER_NONE ||
        key->cipher > WLAN_OFFLOAD_CIPHER_AES_CMAC || !key->data ||
        !key->data_length || key->data_length > RTWO_CTRL_MAX_KEY_LENGTH ||
        key->sequence_length > RTWO_CTRL_MAX_SEQUENCE_LENGTH ||
        (key->sequence_length && !key->sequence) || key->pairwise > 1 ||
        key->set_transmit > 1)
    {
        return -EINVAL;
    }
    memset(&request, 0, sizeof(request));
    request.cipher = key->cipher;
    request.index = key->index;
    request.pairwise = key->pairwise;
    request.set_transmit = key->set_transmit;
    request.key_length = key->data_length;
    request.sequence_length = key->sequence_length;
    memcpy(request.peer, key->peer, sizeof(request.peer));
    memcpy(request.key, key->data, key->data_length);
    if (key->sequence_length)
    {
        memcpy(request.sequence, key->sequence, key->sequence_length);
    }
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_SET_KEY, interface_type,
                                &request, sizeof(request), request_id);
}

int wlan_offload_delete_key(struct wlan_offload_handle *handle,
                       enum wlan_offload_interface_type interface_type,
                       uint8_t index, int pairwise, const uint8_t *peer,
                       uint32_t *request_id)
{
    struct rtwo_ctrl_delete_key_request request;

    if (pairwise != 0 && pairwise != 1)
    {
        return -EINVAL;
    }
    memset(&request, 0, sizeof(request));
    request.index = index;
    request.pairwise = pairwise;
    if (peer)
    {
        memcpy(request.peer, peer, sizeof(request.peer));
    }
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_DELETE_KEY,
                                interface_type, &request, sizeof(request),
                                request_id);
}

int wlan_offload_set_default_key(struct wlan_offload_handle *handle,
                            enum wlan_offload_interface_type interface_type,
                            uint8_t index, int unicast, int multicast,
                            uint32_t *request_id)
{
    struct rtwo_ctrl_default_key_request request;

    if ((unicast != 0 && unicast != 1) ||
        (multicast != 0 && multicast != 1))
    {
        return -EINVAL;
    }
    memset(&request, 0, sizeof(request));
    request.index = index;
    request.unicast = unicast;
    request.multicast = multicast;
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_SET_DEFAULT_KEY,
                                interface_type, &request, sizeof(request),
                                request_id);
}

int wlan_offload_send_mgmt(struct wlan_offload_handle *handle,
                      enum wlan_offload_interface_type interface_type,
                      const struct wlan_offload_mgmt_frame *frame,
                      uint32_t *request_id)
{
    struct rtwo_ctrl_mgmt_frame request;

    if (!frame || !frame->data || !frame->data_length ||
        frame->data_length > RTWO_CTRL_MAX_FRAME_LENGTH ||
        frame->off_channel > 1)
    {
        return -EINVAL;
    }
    memset(&request, 0, sizeof(request));
    if (wlan_offload_channel_to_wire(&request.channel, &frame->channel, 0))
    {
        return -EINVAL;
    }
    request.off_channel = frame->off_channel;
    request.wait_ms = frame->wait_ms;
    request.cookie = frame->cookie;
    request.data_length = frame->data_length;
    memcpy(request.data, frame->data, frame->data_length);
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_MGMT_TX, interface_type,
                                &request, sizeof(request), request_id);
}

int wlan_offload_send_eapol(struct wlan_offload_handle *handle,
                       enum wlan_offload_interface_type interface_type,
                       const uint8_t destination[6], const void *data,
                       size_t data_length, uint32_t *request_id)
{
    struct rtwo_ctrl_eapol_frame frame;

    if (!destination || !data || !data_length ||
        data_length > RTWO_CTRL_MAX_EAPOL_LENGTH)
    {
        return -EINVAL;
    }
    memset(&frame, 0, sizeof(frame));
    memcpy(frame.destination, destination, sizeof(frame.destination));
    frame.data_length = data_length;
    memcpy(frame.data, data, data_length);
    return wlan_offload_send_command(handle, RTWO_CTRL_CMD_EAPOL_TX, interface_type,
                                &frame, sizeof(frame), request_id);
}

int wlan_offload_external_auth_response(struct wlan_offload_handle *handle,
                                   enum wlan_offload_interface_type interface_type,
                                   uint32_t request_id, uint16_t status)
{
    struct rtwo_ctrl_external_auth_response response;

    memset(&response, 0, sizeof(response));
    response.status = status;
    return wlan_offload_send_command_id(handle,
                                   RTWO_CTRL_CMD_EXTERNAL_AUTH_RESPONSE,
                                   interface_type, request_id, &response,
                                   sizeof(response));
}

static int wlan_offload_decode_event(const uint8_t *message, size_t length,
                                struct wlan_offload_event *event)
{
    const struct rtwo_ctrl_header *header =
        (const struct rtwo_ctrl_header *)message;
    const uint8_t *payload = message + sizeof(*header);
    size_t expected = sizeof(*header);

    memset(event, 0, sizeof(*event));
    event->interface_type = (enum wlan_offload_interface_type)header->iftype;
    event->request_id = header->request_id;
    event->status = header->status;
    event->truncated = !!(header->flags & RTWO_CTRL_FLAG_TRUNCATED);
    event->overflow = !!(header->flags & RTWO_CTRL_FLAG_OVERFLOW);

#define WLAN_OFFLOAD_EVENT_PAYLOAD(_wire_type)                                      \
    do                                                                          \
    {                                                                           \
        expected += sizeof(_wire_type);                                         \
        if (length != expected)                                                 \
        {                                                                       \
            return -EPROTO;                                                     \
        }                                                                       \
    } while (0)

    switch (header->type)
    {
    case RTWO_CTRL_EVENT_RADIO_ONLINE:
        event->type = WLAN_OFFLOAD_EVENT_RADIO_ONLINE;
        break;
    case RTWO_CTRL_EVENT_RADIO_OFFLINE:
        event->type = WLAN_OFFLOAD_EVENT_RADIO_OFFLINE;
        break;
    case RTWO_CTRL_EVENT_SCAN_RESULT:
    {
        const struct rtwo_ctrl_network *wire = (const void *)payload;

        WLAN_OFFLOAD_EVENT_PAYLOAD(struct rtwo_ctrl_network);
        if (wire->ssid.length > WLAN_OFFLOAD_SSID_MAX_LENGTH ||
            wire->ies_length > WLAN_OFFLOAD_IE_MAX_LENGTH)
        {
            return -EPROTO;
        }
        event->type = WLAN_OFFLOAD_EVENT_SCAN_RESULT;
        event->data.network.ssid.length = wire->ssid.length;
        memcpy(event->data.network.ssid.value, wire->ssid.value,
               wire->ssid.length);
        memcpy(event->data.network.bssid, wire->bssid, 6);
        if (wlan_offload_channel_from_wire(&event->data.network.channel,
                                      &wire->channel, 0))
        {
            return -EPROTO;
        }
        event->data.network.rssi = wire->rssi;
        event->data.network.security = wire->security;
        event->data.network.beacon_interval = wire->beacon_interval;
        event->data.network.capability = wire->capability;
        event->data.network.ies_length = wire->ies_length;
        memcpy(event->data.network.ies, wire->ies, wire->ies_length);
        break;
    }
    case RTWO_CTRL_EVENT_SCAN_DONE:
        event->type = WLAN_OFFLOAD_EVENT_SCAN_DONE;
        break;
    case RTWO_CTRL_EVENT_CONNECT_RESULT:
        event->type = WLAN_OFFLOAD_EVENT_CONNECT_RESULT;
        break;
    case RTWO_CTRL_EVENT_DISCONNECTED:
    {
        const struct rtwo_ctrl_disconnected *wire = (const void *)payload;

        WLAN_OFFLOAD_EVENT_PAYLOAD(struct rtwo_ctrl_disconnected);
        event->type = WLAN_OFFLOAD_EVENT_DISCONNECTED;
        memcpy(event->data.disconnected.bssid, wire->bssid, 6);
        event->data.disconnected.reason = wire->reason;
        event->data.disconnected.locally_generated = wire->locally_generated;
        break;
    }
    case RTWO_CTRL_EVENT_AUTH_RX:
        event->type = WLAN_OFFLOAD_EVENT_AUTH_RX;
        goto decode_frame;
    case RTWO_CTRL_EVENT_ASSOC_RX:
        event->type = WLAN_OFFLOAD_EVENT_ASSOC_RX;
        goto decode_frame;
    case RTWO_CTRL_EVENT_MGMT_RX:
        event->type = WLAN_OFFLOAD_EVENT_MGMT_RX;
decode_frame:
    {
        const struct rtwo_ctrl_rx_frame *wire = (const void *)payload;

        WLAN_OFFLOAD_EVENT_PAYLOAD(struct rtwo_ctrl_rx_frame);
        if (wire->data_length > WLAN_OFFLOAD_FRAME_MAX_LENGTH)
        {
            return -EPROTO;
        }
        if (wlan_offload_channel_from_wire(&event->data.frame.channel,
                                      &wire->channel, 0))
        {
            return -EPROTO;
        }
        event->data.frame.rssi = wire->rssi;
        event->data.frame.length = wire->data_length;
        memcpy(event->data.frame.data, wire->data, wire->data_length);
        break;
    }
    case RTWO_CTRL_EVENT_MGMT_TX_STATUS:
    {
        const struct rtwo_ctrl_tx_status *wire = (const void *)payload;

        WLAN_OFFLOAD_EVENT_PAYLOAD(struct rtwo_ctrl_tx_status);
        if (wire->data_length > WLAN_OFFLOAD_FRAME_MAX_LENGTH)
        {
            return -EPROTO;
        }
        event->type = WLAN_OFFLOAD_EVENT_MGMT_TX_STATUS;
        event->data.tx_status.cookie = wire->cookie;
        event->data.tx_status.acknowledged = wire->acknowledged;
        event->data.tx_status.length = wire->data_length;
        memcpy(event->data.tx_status.data, wire->data, wire->data_length);
        break;
    }
    case RTWO_CTRL_EVENT_EAPOL_RX:
    {
        const struct rtwo_ctrl_eapol_frame *wire = (const void *)payload;

        WLAN_OFFLOAD_EVENT_PAYLOAD(struct rtwo_ctrl_eapol_frame);
        if (wire->data_length > WLAN_OFFLOAD_EAPOL_MAX_LENGTH)
        {
            return -EPROTO;
        }
        event->type = WLAN_OFFLOAD_EVENT_EAPOL_RX;
        memcpy(event->data.eapol.source, wire->source, 6);
        memcpy(event->data.eapol.destination, wire->destination, 6);
        event->data.eapol.length = wire->data_length;
        memcpy(event->data.eapol.data, wire->data, wire->data_length);
        break;
    }
    case RTWO_CTRL_EVENT_REGULATORY_CHANGED:
        event->type = WLAN_OFFLOAD_EVENT_REGULATORY_CHANGED;
        break;
    case RTWO_CTRL_EVENT_FIRMWARE_ERROR:
    {
        const struct rtwo_ctrl_firmware_error *wire = (const void *)payload;

        WLAN_OFFLOAD_EVENT_PAYLOAD(struct rtwo_ctrl_firmware_error);
        if (wire->dump_length > WLAN_OFFLOAD_IE_MAX_LENGTH)
        {
            return -EPROTO;
        }
        event->type = WLAN_OFFLOAD_EVENT_FIRMWARE_ERROR;
        event->data.firmware.reason = wire->reason;
        event->data.firmware.dump_length = wire->dump_length;
        memcpy(event->data.firmware.dump, wire->dump, wire->dump_length);
        break;
    }
    case RTWO_CTRL_EVENT_EXTERNAL_AUTH_REQUIRED:
    {
        const struct rtwo_ctrl_external_auth *wire =
            (const void *)payload;

        WLAN_OFFLOAD_EVENT_PAYLOAD(struct rtwo_ctrl_external_auth);
        if (!header->request_id || wire->reserved || !wire->ssid.length ||
            wire->ssid.length > WLAN_OFFLOAD_SSID_MAX_LENGTH)
        {
            return -EPROTO;
        }
        event->type = WLAN_OFFLOAD_EVENT_EXTERNAL_AUTH_REQUIRED;
        event->data.external_auth.ssid.length = wire->ssid.length;
        memcpy(event->data.external_auth.ssid.value, wire->ssid.value,
               wire->ssid.length);
        memcpy(event->data.external_auth.bssid, wire->bssid,
               sizeof(event->data.external_auth.bssid));
        event->data.external_auth.akm_suite = wire->akm_suite;
        break;
    }
    default:
        return -ENOMSG;
    }
    if (length != expected)
    {
        return -EPROTO;
    }
    return 0;
#undef WLAN_OFFLOAD_EVENT_PAYLOAD
}

int wlan_offload_receive_event(struct wlan_offload_handle *handle,
                          struct wlan_offload_event *event, int timeout_ms)
{
    uint8_t message[RTWO_CTRL_MAX_MESSAGE_SIZE];
    size_t length;
    int result;

    if (!event)
    {
        return -EINVAL;
    }
    result = wlan_offload_read_message(handle, message, sizeof(message), timeout_ms,
                                  &length);
    if (result)
    {
        return result;
    }
    return wlan_offload_decode_event(message, length, event);
}

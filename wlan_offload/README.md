# libwlan_offload

`libwlan_offload` is the RT-Smart userspace client for a WLAN offload control device. It
is the only userspace component that knows the private `/dev/wlanctlN` message
format.

Applications include the installed `wlan_offload_client.h`. That header contains
only fixed-width C types and the public client API; it does not include an
RT-Thread or kernel header. `src/wlan_offload_wire.h` is private to the library, and
the matching kernel `wlan_offload_control_protocol.h` is private to the WLAN offload
control-device implementation. Both sides reject unsupported protocol
versions and contain compile-time wire-size checks.

`wlan_offload_get_names()` reports the control, station, and AP device names the
kernel assigned to the radio behind an open handle, along with its radio index.
Use it instead of assuming `/dev/wlanctl0`: names are allocated at attachment
time, so which device an application gets depends on probe order when several
radios or vendor drivers are present. A kernel that predates the command
answers `-ENOSYS`.

The API provides capability discovery, interface state, scan, authentication,
association, key management, management frames, EAPOL, external-auth
completion, and event reception. When an external-auth event is received, pass
its `request_id` to `wlan_offload_external_auth_response()` after the supplicant has
processed the indicated AKM exchange.
Only one process may open a control device. For a host-supplicant driver that
owner is normally the `wpa_supplicant` WLAN offload backend. Firmware-offload
drivers can expose the same device for diagnostics without advertising the
external-supplicant capability.

Scan, authentication, management TX, scan-result, and management RX structures
use `struct wlan_offload_channel`. Supplicant integrations should populate the
primary channel/frequency, width, and center frequencies from their own scan or
association state; this preserves 6 GHz and 80/160/320 MHz information that a
single channel number cannot represent.

Scan results use the public `wlan_offload_security_t` and
`WLAN_OFFLOAD_SECURITY_*` constants. Their numeric values match the control wire
ABI, so applications do not need, and must not include, the kernel
`wlan_dev.h` header.

Build and install the static library and public header with:

```sh
make -C src/rtsmart/libs/wlan_offload
```

The diagnostic application in `src/rtsmart/examples/peripheral/wlan_offload`
demonstrates the public API. Its installed example binary is:

```sh
wlan_offload.elf /dev/wlanctl0 info
wlan_offload.elf /dev/wlanctl0 names
wlan_offload.elf /dev/wlanctl0 probe
wlan_offload.elf /dev/wlanctl0 scan
wlan_offload.elf /dev/wlanctl0 monitor
```

`probe` enables the station through the kernel WLAN management path before
reading capabilities. On an RT-Thread lwIP build this also creates the `wlanN`
network interface used by `ifconfig` and the `wifi` shell commands. `monitor`
uses a blocking read for an indefinite wait and prints events generated after
the command starts. Drivers configured for automatic station startup already
have their `wlanN` interface initialized, so `probe` is then an idempotent
diagnostic command.

The sample deliberately does not implement WPA. Authentication and key
management belong in `wpa_supplicant`, which should link `libwlan_offload` instead
of opening or encoding the control protocol itself.

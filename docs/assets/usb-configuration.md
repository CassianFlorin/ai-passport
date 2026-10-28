[简体中文](usb-configuration.zh_CN.md) · **English**

# Desktop USB Configuration

The personal-home application accepts Wi-Fi and AI service settings over its
existing USB Serial/JTAG connection. The configuration page runs on the computer,
so the device does not need Wi-Fi before setup. All page assets are bundled;
no cloud configuration service or phone is required.

## Open the configuration page

Use Python 3 and a desktop browser with Web Serial, such as Chrome or Edge:

```bash
python3 tools/usb_config.py
```

Open the printed loopback address, normally `http://127.0.0.1:8765/`. Add `--port`
to choose another port, or `--open` to open the default browser. An unsupported
browser displays instructions to use a compatible desktop browser.

1. Turn on the device and connect it with a USB data cable.
2. Close any serial monitor, flashing program, or other application using its port.
3. Click the connection button and select the USB JTAG/serial debug unit.
4. Read the current configuration, enter a 2.4 GHz Wi-Fi SSID and password, and
   optionally enter an AI service endpoint and device token.
5. Save. The page reports persistence separately from Wi-Fi association and IP
   acquisition. Network status refreshes automatically.
6. Disconnect from the page before opening a serial monitor or flashing tool.

The browser connection reads settings; it does not flash firmware. Firmware
without this USB protocol will time out and display an update instruction.
Opening the page does not open a serial port until the user selects a device.
The device keeps its own redesigned homepage and three-button controls.

## Device conversation entry

Double-press OK on the homepage to open the function list. Its first entry is
AI conversation, followed by system status and sound settings. UP/DOWN move
one entry at a time without wrapping; click OK to open an entry and hold OK
to return to the list. The selected entry is retained on return.

The conversation page reports live Wi-Fi state and whether a service endpoint
is stored. Its connection snapshot contains only public flags, never SSIDs,
endpoints or credentials. The USB worker publishes it after processing settings
and network events; the UI samples it without doing Flash or radio work while
holding the LVGL lock.

An endpoint is shown as saved, never as connected or authenticated. The page
explicitly says that conversation is not integrated and recording cannot start.
It provides USB setup guidance for missing configuration, connection retries,
storage errors and unavailable USB/Wi-Fi. No button on this page records audio,
sends requests or changes sound settings. Actual voice conversation still
requires an implemented service protocol and independently verified audio and
network behavior.

## Settings and limits

| Setting | Limit and behavior |
| --- | --- |
| SSID | Up to 32 UTF-8 bytes; type it manually or select a scanned network |
| Wi-Fi password | Empty for an open network, 8-63 UTF-8 bytes, or a 64-digit hexadecimal key |
| Service endpoint | Up to 384 ASCII bytes; `http://`, `https://`, `ws://`, or `wss://`; no userinfo or fragment |
| Service token | Up to 512 UTF-8 bytes |

Passwords and tokens are write-only. Configuration reads report whether each
secret exists, without returning its value. Leaving a secret blank retains it;
the corresponding clear checkbox explicitly removes it. When the SSID changes,
enter its password again or explicitly select an open network.

Saving an empty SSID with an explicitly cleared password disables the Wi-Fi
connection. Saving only a service endpoint does not reconnect an unchanged Wi-Fi
network. Wi-Fi scanning may be unavailable during an active connection attempt;
manual SSID entry remains available.

The endpoint and token are configuration for a future AI client. This increment
does not implement AI chat, negotiate a service protocol, validate credentials
with the target service, or prove Internet reachability. An acquired IP only
establishes the device's connection to its router.

## Persistence and recovery

The application stores one versioned blob in the NVS namespace `connection`,
key `values_v1`, separately from sound preferences. It updates its live settings
only after the storage operation succeeds. Missing settings retain defaults;
corrupt or unsupported records report an error and are not silently erased.
An explicit save can replace such a record.

Saved Wi-Fi settings are used on startup. Disconnections retry with increasing
delays, capped at 30 seconds. The page also offers an explicit reconnect action.
The USB worker performs storage, Wi-Fi and scan operations outside button
callbacks and the LVGL task. With no configured network, the radio starts only
when scanning or applying network settings.

Secrets are not encrypted by this feature. They are stored in the board's
existing NVS partition; no whole-partition erase or partition-layout change is
performed. Do not publish an NVS dump, device settings, or secret-bearing logs.

## Transport and verification

The wire protocol is versioned by the `@PASSPORT1` prefix. Requests use bounded
tab-separated fields, hexadecimal UTF-8 payloads, and a newline terminator.
Responses use the same prefix with a JSON object and request ID. A secret field
of `*` retains its value, whereas an empty field clears it. Unframed console
messages are ignored; oversized or binary-contaminated requests are discarded
through the next newline. The line buffer is 2304 bytes.

The static gate tests the actual C parser and versioned storage with injected
failures, exercises the browser client against a C serial peer, and verifies that
the local server only exposes bundled page assets. Node.js 20 or newer is needed
for the browser-client host tests; serving the page needs only Python 3.

```bash
./tools/validate.sh --static
./tools/validate.sh
```

Hardware acceptance still requires the reviewed firmware and explicit flash
approval. Verify USB authorization, repeated connect/disconnect, Wi-Fi scan,
wrong-password recovery, successful IP acquisition, reboot persistence, secret
retention/clearing, and simultaneous homepage/button/audio operation. Verify the
actual AI service separately after its client protocol has been implemented.

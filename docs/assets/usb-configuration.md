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
4. Read the current configuration, enter a 2.4 GHz Wi-Fi SSID and password, the
   chat service's OpenAI-compatible Base URL and API key, and optionally a
   separate speech service and model names.
5. Save. The page reports persistence separately from Wi-Fi association and IP
   acquisition. Network status refreshes automatically.
6. Disconnect from the page before opening a serial monitor or flashing tool.

The browser connection reads settings; it does not flash firmware. Firmware
without this USB protocol will time out and display an update instruction.
Opening the page does not open a serial port until the user selects a device.

The device, not the browser or a server, fetches model lists. Once Wi-Fi is
connected, the page sends a `models` request over USB with a Base URL and either
the typed API key or a request to reuse the saved one; the device calls
`GET {Base URL}/models` itself and returns only model IDs. A saved key is only
used for the exact URL it was saved with, so a page cannot send it to another
host. API keys therefore never pass through any website, including the hosted
page. When a key is already saved and Wi-Fi is connected, lists load right
after the USB connection.

The chat list fills the chat model; the speech service's list (or the chat list
when no speech service is set) fills the transcription and speech models. A
saved model stays selected; an empty field selects the first model recommended
for that purpose (chat may use any listed model). Each dropdown also lists all
other models and manual entry. If no transcription or speech model is listed,
the page warns that the service likely lacks the speech endpoints. Voices offer
common OpenAI voice names. The device keeps IDs of up to 64 bytes made of
letters, digits and `._:/@+-`, and returns at most about 6 KB of IDs. Services
without `/models` keep the manual fields. Fetching uses the same memory as a
conversation turn, so it is refused while a turn runs.

## Hosted page

`tools/usb-config/` is a static site. Publish it, for example on Cloudflare
Pages, so users configure their devices from a public HTTPS address:

```bash
npx wrangler pages deploy tools/usb-config --project-name <project>
```

`tools/usb-config/_headers` applies the same security headers as the local
server, including `connect-src 'none'`: the page makes no network requests and
talks only to the selected USB device. The static gate checks that both
policies match. No server code or secret is deployed; publishing remains a
separate, explicitly authorized step. The local `python3 tools/usb_config.py`
server stays available for offline use.
The device keeps its own redesigned homepage and three-button controls.

## Device conversation

Double-press OK on the homepage to open the function list. Its first entry is
AI conversation, followed by system status and sound settings. UP/DOWN move
one entry at a time without wrapping; click OK to open an entry and hold OK
to return to the list. The selected entry is retained on return.

The AI page is a push-to-talk client for an OpenAI-compatible cloud service:

| Key | Action |
| --- | --- |
| OK | Start a turn; press again to send the recording. While waiting, cancel; while speaking, stop speech and keep the reply |
| UP / DOWN | Scroll the transcript and reply |
| Hold UP | Clear the conversation history (when no turn is running) |
| Hold OK | Return to the list; any running turn is cancelled first |

One turn runs these steps:

1. Open `POST {speech Base URL}/audio/transcriptions`, then start the microphone and
   show "listening". 16 kHz mono PCM is streamed as a chunked multipart WAV
   upload while recording, for at most 15 seconds. Recordings under 0.3 seconds
   are discarded.
2. Send the transcript with up to the two previous exchanges and a short-reply
   system prompt to `POST {chat Base URL}/chat/completions` (`stream: false`).
3. Show the reply, then request `POST {speech Base URL}/audio/speech` with
   `response_format: "wav"` and play 16-bit PCM while it downloads. Stereo is
   mixed to mono. When the speaker is muted, no speech request is made.

Chat-only providers such as DeepSeek have no speech endpoints, so speech can
use a different provider (for example SiliconFlow or OpenAI). An empty speech
Base URL reuses the chat service's URL and key. Requests send
`Authorization: Bearer <API key>` for their own service and use the ESP-IDF certificate
bundle for HTTPS. One request exists at a time; its task, TLS buffers, 8 KB
work buffer and 16 KB recording ring are allocated only during a turn. The page
reports the failing step: unreachable service, invalid API key (401/403),
missing endpoint or model (404), rate or quota limit (429), server errors,
unsupported responses, no speech and memory or audio faults. Service error
messages are logged without the URL or key. A speech failure keeps the text
reply on screen.

The connection snapshot used for guidance contains only public flags, never
SSIDs, endpoints or credentials. The UI samples it without doing Flash or radio
work while holding the LVGL lock. The settings-page preview never waits for a
running turn; it reports that preview is unavailable during a conversation.

## Settings and limits

| Setting | Limit and behavior |
| --- | --- |
| SSID | Up to 32 UTF-8 bytes; type it manually or select a scanned network |
| Wi-Fi password | Empty for an open network, 8-63 UTF-8 bytes, or a 64-digit hexadecimal key |
| Base URL | Up to 384 ASCII bytes; `http://` or `https://`, no query, userinfo or fragment. Include the API version path, for example `https://api.openai.com/v1` |
| API key | Up to 512 UTF-8 bytes; sent as a Bearer token to the chat service |
| Speech Base URL | Optional, same rules as Base URL; used for transcription and speech. Empty reuses the chat service |
| Speech API key | Up to 512 UTF-8 bytes; write-only like the other secrets |
| Chat model | Up to 64 bytes; empty uses `gpt-4o-mini` |
| Transcription model | Up to 64 bytes; empty uses `whisper-1` |
| Speech model | Up to 64 bytes; empty uses `tts-1` |
| Voice | Up to 64 bytes; empty uses `alloy` |

Passwords and API keys are write-only. Configuration reads report whether each
secret exists, without returning its value. Leaving a secret blank retains it;
the corresponding clear checkbox explicitly removes it. When the SSID changes,
enter its password again or explicitly select an open network.

Saving an empty SSID with an explicitly cleared password disables the Wi-Fi
connection. Saving only service settings does not reconnect an unchanged Wi-Fi
network. Wi-Fi scanning may be unavailable during an active connection attempt;
manual SSID entry remains available.

Saving does not contact the AI service. The first conversation verifies the
Base URL, key and models; an acquired IP only establishes the device's
connection to its router. The service must accept a chunked request body and a
WAV upload whose RIFF sizes are `0xFFFFFFFF` (unknown length), and must return
PCM WAV from the speech endpoint. Older stored `ws://` or `wss://` endpoints
remain readable but are reported as an invalid configuration by the AI page.

## Persistence and recovery

The application stores one versioned blob in the NVS namespace `connection`,
key `values_v3`, separately from sound preferences. Records from earlier
firmware (`values_v2`, then `values_v1`) are read with the newer fields empty;
the first save writes `values_v3` and erases both older keys in the same commit. It updates its live settings
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

The framing is identified by the `@PASSPORT1` prefix; replies report data
`protocol` 3. Version 2 added four model fields; version 3 adds the speech
service URL and write-only key. Requests use bounded
tab-separated fields, hexadecimal UTF-8 payloads, and a newline terminator.
Responses use the same prefix with a JSON object and request ID. A secret field
of `*` retains its value, whereas an empty field clears it. Unframed console
messages are ignored; oversized or binary-contaminated requests are discarded
through the next newline. The line buffer is 4608 bytes.

The static gate tests the actual C parser, versioned storage and migration
with injected failures, the OpenAI-compatible request/response helpers, exercises the browser client against a C serial peer, and verifies that
the local server only exposes bundled page assets with a hosted header policy
that matches it. Node.js 20 or newer is needed
for the browser-client host tests; serving the page needs only Python 3.

```bash
./tools/validate.sh --static
./tools/validate.sh
```

Hardware acceptance still requires the reviewed firmware and explicit flash
approval. Verify USB authorization, repeated connect/disconnect, Wi-Fi scan,
wrong-password recovery, successful IP acquisition, reboot persistence, secret
retention/clearing, and simultaneous homepage/button/audio operation. For AI
conversation, verify with the intended provider: a complete turn, speech
quality, long replies and scrolling, cancel in every stage, wrong key and
model errors, Wi-Fi loss during upload, mute, and the free-heap minimum logged
after each turn.

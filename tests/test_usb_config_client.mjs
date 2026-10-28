import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import test from "node:test";
import { ConfigurationClient, encodeRequest, PREFIX } from "../tools/usb-config/protocol.mjs";
import { groupModels } from "../tools/usb-config/models.mjs";

class SerialPeer {
  constructor() {
    this.closed = false;
    this.silent = false;
    this.child = spawn(process.env.PASSPORT_PROTOCOL_FIXTURE, [], { stdio: ["pipe", "pipe", "ignore"] });
    this.readable = new ReadableStream({
      start: (controller) => { this.controller = controller; },
      cancel: () => { this.cancelled = true; },
    });
    this.writable = new WritableStream({
      write: (bytes) => {
        if (this.failWrite) throw new Error("write_failed");
        if (!this.silent) this.child.stdin.write(bytes);
      },
    });
    this.child.stdout.on("data", (chunk) => {
      if (this.cancelled) return;
      // Deliberately fragment responses as USB reads are not message boundaries.
      for (let i = 0; i < chunk.length; i += 7) {
        this.controller.enqueue(new Uint8Array(chunk.slice(i, i + 7)));
      }
    });
  }
  async open(options) { assert.equal(options.baudRate, 115200); }
  async setSignals(signals) { assert.deepEqual(signals, { dataTerminalReady: false, requestToSend: false }); }
  async close() {
    if (this.closed) return;
    this.closed = true;
    const exited = once(this.child, "exit");
    this.child.kill();
    await exited;
  }
}

const fixture = async (options) => {
  assert.ok(process.env.PASSPORT_PROTOCOL_FIXTURE, "Compile tests/usb_config_fixture.c before running");
  const peer = new SerialPeer();
  const client = new ConfigurationClient(options);
  const state = await client.connect(peer);
  assert.equal(state.protocol, 3);
  return { peer, client };
};

test("browser frames interoperate with the real C parser, including UTF-8 and secret retention", async () => {
  const { client } = await fixture();
  try {
    const saved = await client.request("save", {
      ssid: "Lab \u5bb6", password: "sample-password",
      service_url: "https://service.example/v1", service_token: "sample-token",
      chat_model: " deepseek-chat ", tts_voice: "alloy",
      speech_url: " https://api.siliconflow.cn/v1 ", speech_token: "speech-secret",
    });
    assert.equal(saved.ssid, "Lab \u5bb6");
    assert.equal(saved.password_set, true);
    assert.equal(saved.chat_model, "deepseek-chat");
    assert.equal(saved.asr_model, "");
    assert.equal(saved.tts_voice, "alloy");
    assert.equal(saved.speech_url, "https://api.siliconflow.cn/v1");
    assert.equal(saved.speech_token_set, true);
    assert.equal(JSON.stringify(saved).includes("speech-secret"), false);
    assert.equal(saved.token_set, true);
    assert.equal(JSON.stringify(saved).includes("sample-password"), false);
    assert.equal(JSON.stringify(saved).includes("sample-token"), false);
    const retained = await client.request("save", {
      ssid: saved.ssid, password: null, service_url: saved.service_url, service_token: null,
    });
    assert.equal(retained.password_set, true);
    assert.equal(retained.speech_token_set, true);
    await assert.rejects(client.request("save", {
      ssid: "another network", password: null, service_url: "", service_token: null,
    }), /password_required/u);
    const cleared = await client.request("save", {
      ssid: "another network", password: "", service_url: "", service_token: "",
    });
    assert.equal(cleared.password_set, false);
    assert.equal(cleared.token_set, false);
    assert.equal((await client.request("scan")).networks[0].ssid, "USB test");
    // Model lists come from the device; "*" may only reuse a key saved for that URL.
    await client.request("save", {
      ssid: "", password: "", service_url: "https://api.deepseek.com/v1",
      service_token: "chat-key",
    });
    const listed = await client.request("models", {
      base_url: "https://api.deepseek.com/v1", api_key: null,
    });
    assert.deepEqual(listed.models, ["fixture-chat", "whisper-1"]);
    await assert.rejects(client.request("models", {
      base_url: "https://other.example/v1", api_key: null,
    }), /key_required/u);
    assert.deepEqual((await client.request("models", {
      base_url: "https://other.example/v1", api_key: "typed",
    })).models.length, 2);
  } finally { await client.disconnect(); }
});

test("length limits use UTF-8 bytes and request IDs cover the full uint32 range", () => {
  assert.match(new TextDecoder().decode(encodeRequest(0xffffffff, "get")), /4294967295/u);
  assert.throws(() => encodeRequest(0, "get"), /invalid_request/u);
  assert.throws(() => encodeRequest(1, "save", {
    ssid: "\u5bb6".repeat(11), password: null, service_url: "", service_token: null,
  }), /invalid_settings/u);
  for (const service_url of ["https://user:pass@host", "file:///tmp/device", "http://host/#key",
    "wss://service.example/realtime"]) {
    assert.throws(() => encodeRequest(1, "save", {
      ssid: "", password: null, service_url: "", service_token: null, speech_url: service_url,
    }), /invalid_url/u);
    assert.throws(() => encodeRequest(1, "save", {
      ssid: "", password: null, service_url, service_token: null,
    }), /invalid_url/u);
  }
  assert.throws(() => encodeRequest(1, "save", {
    ssid: "Lab", password: "short", service_url: "", service_token: "",
  }), /invalid_password/u);
});

test("timeouts allow retry; failed stream writes release requests and permit reconnect", async () => {
  const { peer, client } = await fixture({ timeout: 150 });
  try {
    peer.silent = true;
    await assert.rejects(client.request("status"), /timeout/u);
    assert.equal(client.busy, false);
    peer.silent = false;
    assert.equal((await client.request("status")).device, "AI Passport");
    peer.failWrite = true;
    await assert.rejects(client.request("status"), /write_failed/u);
    assert.equal(client.busy, false);
    // A failed WritableStream stays errored; recovery needs a new USB session.
    await client.disconnect();
    await client.connect(new SerialPeer());
    assert.equal((await client.request("status")).protocol, 3);
  } finally { await client.disconnect(); }
});

test("logs, stale IDs and oversized lines cannot masquerade as a protocol response", async () => {
  const { peer, client } = await fixture();
  try {
    peer.silent = true;
    const response = client.request("status");
    const id = client.pending.id;
    client.consume("boot log\n" + PREFIX + "{broken json}\n" +
      PREFIX + JSON.stringify({ id: id - 1, ok: true, data: { stale: true } }) + "\n");
    assert.equal(client.pending.id, id);
    client.consume("x".repeat(20000) + "\n");
    assert.equal(client.line.length, 0);
    client.consume(PREFIX + JSON.stringify({ id, ok: true, data: { fresh: true } }) + "\r\n");
    assert.deepEqual(await response, { fresh: true });
  } finally { await client.disconnect(); }
});

test("disconnect rejects in-flight requests and releases both serial stream locks", async () => {
  const { peer, client } = await fixture();
  peer.silent = true;
  const pending = client.request("status");
  const rejection = assert.rejects(pending, /disconnected/u);
  await client.disconnect();
  await rejection;
  assert.equal(peer.readable.locked, false);
  assert.equal(peer.writable.locked, false);
  assert.equal(peer.closed, true);
  await assert.rejects(client.request("status"), /disconnected/u);
  const other = new SerialPeer();
  await client.connect(other);
  assert.equal((await client.request("status")).protocol, 3);
  await client.disconnect();
});

test("models requests are bounded and never send an empty key", () => {
  const text = new TextDecoder().decode(encodeRequest(5, "models", {
    base_url: "https://api.example/v1", api_key: null,
  }));
  assert.equal(text, `${PREFIX}5\tmodels\t68747470733a2f2f6170692e6578616d706c652f7631\t*\n`);
  for (const values of [{ base_url: "", api_key: null }, { base_url: "https://x/v1", api_key: "" },
    { base_url: "https://x/v1", api_key: "a\nb" }]) {
    assert.throws(() => encodeRequest(1, "models", values), /invalid_settings/u);
  }
  assert.throws(() => encodeRequest(1, "models", { base_url: "wss://x", api_key: null }),
    /invalid_url/u);
});

test("model dropdowns recommend by purpose while keeping every model selectable", () => {
  const models = ["gpt-4o-mini", "whisper-1", "tts-1", "gpt-4o-mini-tts", "gpt-4o-transcribe",
    "text-embedding-3-small", "deepseek-chat", "deepseek-chat"];
  const chat = groupModels("chat_model", models);
  assert.deepEqual(chat.recommended, ["deepseek-chat", "gpt-4o-mini"]);
  assert.deepEqual(groupModels("asr_model", models).recommended, ["gpt-4o-transcribe", "whisper-1"]);
  assert.deepEqual(groupModels("tts_model", models).recommended, ["gpt-4o-mini-tts", "tts-1"]);
  for (const kind of ["chat_model", "asr_model", "tts_model"]) {
    const { recommended, others } = groupModels(kind, models);
    assert.equal(recommended.length + others.length, 7);
  }
});

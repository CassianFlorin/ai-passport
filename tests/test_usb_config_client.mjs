import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import test from "node:test";
import { ConfigurationClient, encodeRequest, PREFIX } from "../tools/usb-config/protocol.mjs";

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
  assert.equal(state.protocol, 1);
  return { peer, client };
};

test("browser frames interoperate with the real C parser, including UTF-8 and secret retention", async () => {
  const { client } = await fixture();
  try {
    const saved = await client.request("save", {
      ssid: "Lab \u5bb6", password: "sample-password",
      service_url: "wss://service.example/voice", service_token: "sample-token",
    });
    assert.equal(saved.ssid, "Lab \u5bb6");
    assert.equal(saved.password_set, true);
    assert.equal(saved.token_set, true);
    assert.equal(JSON.stringify(saved).includes("sample-password"), false);
    assert.equal(JSON.stringify(saved).includes("sample-token"), false);
    const retained = await client.request("save", {
      ssid: saved.ssid, password: null, service_url: saved.service_url, service_token: null,
    });
    assert.equal(retained.password_set, true);
    await assert.rejects(client.request("save", {
      ssid: "another network", password: null, service_url: "", service_token: null,
    }), /password_required/u);
    const cleared = await client.request("save", {
      ssid: "another network", password: "", service_url: "", service_token: "",
    });
    assert.equal(cleared.password_set, false);
    assert.equal(cleared.token_set, false);
    assert.equal((await client.request("scan")).networks[0].ssid, "USB test");
  } finally { await client.disconnect(); }
});

test("length limits use UTF-8 bytes and request IDs cover the full uint32 range", () => {
  assert.match(new TextDecoder().decode(encodeRequest(0xffffffff, "get")), /4294967295/u);
  assert.throws(() => encodeRequest(0, "get"), /invalid_request/u);
  assert.throws(() => encodeRequest(1, "save", {
    ssid: "\u5bb6".repeat(11), password: null, service_url: "", service_token: null,
  }), /invalid_settings/u);
  for (const service_url of ["https://user:pass@host", "file:///tmp/device", "http://host/#key"]) {
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
    assert.equal((await client.request("status")).protocol, 1);
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
  assert.equal((await client.request("status")).protocol, 1);
  await client.disconnect();
});

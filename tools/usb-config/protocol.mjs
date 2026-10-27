// The same bounded framing is implemented by main/usb_config_protocol.c.
export const PREFIX = "@PASSPORT1\t";
const encoder = new TextEncoder();
const limits = { ssid: 32, password: 64, service_url: 384, service_token: 512 };

export function hex(text) {
  return [...encoder.encode(text)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

export function encodeRequest(id, command, values = {}) {
  if (!Number.isInteger(id) || id < 1 || id > 0xffffffff) throw new Error("invalid_request");
  if (!["get", "status", "scan", "save", "reconnect"].includes(command)) throw new Error("invalid_request");
  const fields = [String(id), command];
  if (command === "save") {
    for (const [key, limit] of Object.entries(limits)) {
      const value = values[key];
      if (value !== null && (typeof value !== "string" || encoder.encode(value).length > limit ||
          /[\u0000-\u001f\u007f]/u.test(value))) throw new Error("invalid_settings");
    }
    if (typeof values.ssid !== "string" || typeof values.service_url !== "string") {
      throw new Error("invalid_settings");
    }
    if (values.service_url) {
      let url;
      try { url = new URL(values.service_url); } catch { throw new Error("invalid_url"); }
      if (!["http:", "https:", "ws:", "wss:"].includes(url.protocol) || url.username ||
          url.password || url.hash || /[^\x21-\x7e]/u.test(values.service_url) ||
          values.service_url.includes("\\")) throw new Error("invalid_url");
    }
    if (values.password !== null && values.password.length &&
        (encoder.encode(values.password).length < 8 ||
         (encoder.encode(values.password).length === 64 && !/^[a-f0-9]{64}$/iu.test(values.password)))) {
      throw new Error("invalid_password");
    }
    fields.push(hex(values.ssid), values.password === null ? "*" : hex(values.password),
      hex(values.service_url), values.service_token === null ? "*" : hex(values.service_token));
  }
  return encoder.encode(`${PREFIX}${fields.join("\t")}\n`);
}

export class ConfigurationClient {
  constructor({ timeout = 6000, onDisconnect = () => {} } = {}) {
    this.timeout = timeout;
    this.onDisconnect = onDisconnect;
    this.port = null;
    this.pending = null;
    this.sequence = 0;
    this.line = "";
    this.dropping = false;
    this.writing = false;
    this.closing = false;
  }

  get busy() { return Boolean(this.pending) || this.writing; }

  async connect(port) {
    if (this.port) throw new Error("already_connected");
    this.port = port;
    this.closing = false;
    try {
      await port.open({ baudRate: 115200, bufferSize: 8192, flowControl: "none" });
      // Neither DTR nor RTS is used as a reset command.
      if (port.setSignals) await port.setSignals({ dataTerminalReady: false, requestToSend: false });
      this.reader = port.readable.getReader();
      this.reading = this.readLoop();
      const state = await this.request("get");
      if (state.protocol !== 1 || state.device !== "AI Passport" || typeof state.ssid !== "string") {
        throw new Error("incompatible_firmware");
      }
      return state;
    } catch (error) {
      await this.disconnect();
      throw error;
    }
  }

  async request(command, values) {
    if (!this.port || this.closing || !this.reader) throw new Error("disconnected");
    if (this.busy) throw new Error("busy");
    this.sequence = this.sequence % 0xffffffff + 1;
    const id = this.sequence;
    const bytes = encodeRequest(id, command, values);
    const response = new Promise((resolve, reject) => {
      this.pending = { id, resolve, reject, timer: setTimeout(() => {
        this.failPending(new Error("timeout"));
      }, command === "scan" ? Math.max(this.timeout, 15000) : this.timeout) };
    });
    this.writing = true;
    const write = (async () => {
      let writer;
      try {
        writer = this.port.writable.getWriter();
        this.writer = writer;
        await writer.write(bytes);
      }
      catch (error) {
        this.failPending(error);
        if (!this.closing) this.onDisconnect(error);
        throw error;
      }
      finally { writer?.releaseLock(); this.writer = null; this.writing = false; }
    })();
    try {
      const [, data] = await Promise.all([write, response]);
      return data;
    } finally {
      // The frame may contain credentials; retain no outgoing byte buffer.
      bytes.fill(0);
    }
  }

  failPending(error) {
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = null;
    clearTimeout(pending.timer);
    pending.reject(error);
  }

  consume(text) {
    for (const char of text) {
      if (char === "\n") {
        if (!this.dropping) this.receive(this.line.replace(/\r$/u, ""));
        this.line = "";
        this.dropping = false;
      } else if (!this.dropping) {
        if (this.line.length >= 8192) { this.line = ""; this.dropping = true; }
        else this.line += char;
      }
    }
  }

  receive(line) {
    if (!line.startsWith(PREFIX) || !this.pending) return;
    let result;
    try { result = JSON.parse(line.slice(PREFIX.length)); } catch { return; }
    if (result.id !== this.pending.id || typeof result.ok !== "boolean") return;
    const pending = this.pending;
    this.pending = null;
    clearTimeout(pending.timer);
    if (result.ok && result.data && typeof result.data === "object") pending.resolve(result.data);
    else pending.reject(new Error(typeof result.error === "string" ? result.error : "invalid_response"));
  }

  async readLoop() {
    const decoder = new TextDecoder();
    let error = new Error("disconnected");
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value) this.consume(decoder.decode(value, { stream: true }));
      }
    } catch (failure) { error = failure; }
    finally {
      this.reader.releaseLock();
      this.reader = null;
      this.failPending(error);
      if (!this.closing) this.onDisconnect(error);
    }
  }

  async disconnect() {
    this.closing = true;
    this.failPending(new Error("disconnected"));
    if (this.writer) await this.writer.abort().catch(() => {});
    if (this.reader) await this.reader.cancel().catch(() => {});
    if (this.reading) await this.reading.catch(() => {});
    if (this.port) await this.port.close().catch(() => {});
    this.port = null;
    this.line = "";
    this.dropping = false;
  }
}

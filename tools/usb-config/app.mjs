import { ConfigurationClient } from "./protocol.mjs";
import { DEFAULTS, VOICES, groupModels } from "./models.mjs";

const $ = (id) => document.getElementById(id);
let client = null;
let ready = false;
let operating = false;
let poll = null;
let pollingRequest = null;

const messages = {
  timeout: "设备没有及时响应。请确认已刷入支持 USB 配置的固件，并关闭占用串口的终端或烧录工具。",
  disconnected: "USB 已断开，请重新连接设备。",
  incompatible_firmware: "当前固件不支持这一版 USB 配置，请更新设备固件。",
  invalid_settings: "配置格式不正确。请检查字段长度、Wi-Fi 密码、Base URL 和模型名称。",
  invalid_url: "Base URL 需要使用 http:// 或 https://，且不能包含用户名、密码或片段。",
  invalid_password: "Wi-Fi 密码应为 8–63 字节，或 64 位十六进制密钥。开放网络请勾选不设密码。",
  password_required: "你修改了 Wi-Fi 名称，请重新输入密码；开放网络请勾选不设密码。",
  storage_failed: "设备保存失败，原来的有效配置仍在使用。请检查设备存储状态后重试。",
  scan_busy: "Wi-Fi 正在连接，暂时无法扫描。请等待连接结束后重试，或手动填写名称。",
  scan_failed: "扫描失败，请重试或手动填写网络名称。",
  wifi_unavailable: "设备的 Wi-Fi 暂时不可用，USB 配置仍可使用。",
  busy: "设备正在处理上一项操作，请稍候。",
};
const modelFields = ["chat_model", "asr_model", "tts_model", "tts_voice"];
const CUSTOM_MODEL = "\u0000custom";
// Chat and speech may come from different providers, each with its own list.
const services = {
  chat: { url: "service-url", token: "token", keys: ["chat_model"], status: "chat-model-status",
    list: null, saved: false },
  speech: { url: "speech-url", token: "speech-token", keys: ["asr_model", "tts_model"],
    status: "speech-model-status", list: null, saved: false },
};
let fetchingModels = false;
const modelErrors = {
  key_required: "请填写 API Key。设备只会把已保存的 Key 发给保存时的同一个 Base URL。",
  wifi_required: "设备需要先连上 Wi-Fi 才能获取模型列表。请先保存 Wi-Fi 并等待连接成功。",
  busy: "设备正在进行对话或获取列表，请稍后再试。",
  no_memory: "设备内存不足，暂时无法获取模型列表，请稍后重试。",
  timeout: "设备获取模型列表超时，请检查网络后重试。",
  disconnected: "USB 已断开，请重新连接设备。",
  auth: "API Key 无效或没有权限，无法获取模型列表。",
  not_found: "该服务没有提供模型列表接口（/models），请手动填写模型名称。",
  network: "设备无法访问该服务，请检查 Base URL 和设备的网络。",
  invalid_url: "Base URL 需要使用 http:// 或 https://，且不能包含查询参数、用户名、密码或片段。",
  invalid_response: "服务没有返回可识别的模型列表，请手动填写模型名称。",
  invalid_settings: "Base URL 或 API Key 格式不正确。",
  http: "服务返回错误，请手动填写模型名称。",
};
const wifiNames = {
  unconfigured: "尚未配置 Wi-Fi", connecting: "正在连接路由器",
  connected: "Wi-Fi 已连接", retrying: "连接失败，正在间隔重试", error: "Wi-Fi 不可用",
};

const fieldId = (key) => key.replace("_", "-");

function modelStatus(service, text, type = "") {
  $(service.status).textContent = text;
  $(service.status).className = `${service === services.speech ? "section-status " : ""}${type}`;
}
function option(value, label = value) {
  const item = document.createElement("option");
  item.value = value;
  item.textContent = label;
  return item;
}
function group(label, ids) {
  const item = document.createElement("optgroup");
  item.label = label;
  item.append(...ids.map((id) => option(id)));
  return item;
}
// The text input stays the saved value; the dropdown only edits it.
function fillModelSelect(key, list) {
  const select = $(`${fieldId(key)}-select`);
  const input = $(fieldId(key));
  if (!list) {
    select.hidden = true;
    input.hidden = false;
    return;
  }
  const { recommended, others } = groupModels(key, list);
  // With a list, use the service's own models: keep a saved choice, otherwise
  // pick the first recommended one. Chat may fall back to any listed model;
  // speech fields never borrow an unrelated (e.g. chat) model.
  const current = input.value.trim() || recommended[0] ||
    (key === "chat_model" ? others[0] : "") || "";
  input.value = current;
  const choices = [];
  if (!current) choices.push(option("", "该服务没有可用模型，请手动输入"));
  if (current && !list.includes(current)) choices.push(group("当前设置", [current]));
  if (recommended.length) choices.push(group("推荐", recommended));
  if (others.length) choices.push(group(recommended.length ? "其他模型" : "全部模型", others));
  choices.push(option(CUSTOM_MODEL, "手动输入…"));
  select.replaceChildren(...choices);
  select.value = current;
  select.hidden = false;
  input.hidden = true;
}
function fillModels() {
  for (const service of Object.values(services)) {
    for (const key of service.keys) fillModelSelect(key, service.list);
  }
}
// An empty speech URL means the chat service also provides speech.
function speechSource() {
  return $("speech-url").value.trim() ? services.speech : services.chat;
}
// The device fetches the list itself; API keys never reach this website.
async function requestModels(source) {
  const base_url = $(source.url).value.trim();
  const typed = $(source.token).value;
  if (!base_url) return { error: "请先填写 Base URL。" };
  if (!typed && !source.saved) return { error: "请先填写 API Key。" };
  try {
    const data = await client.request("models", { base_url, api_key: typed || null });
    return { models: data.models, truncated: data.truncated };
  } catch (error) {
    return { error: modelErrors[error.message] || modelErrors.network };
  }
}
async function fetchModels(automatic) {
  if (fetchingModels || !ready || operating) return;
  fetchingModels = true;
  operating = true;
  controls();
  try {
    await pollingRequest?.catch(() => {});
    const chat = services.chat;
    const speech = services.speech;
    const separate = speechSource() === speech;
    modelStatus(chat, "设备正在获取模型列表…");
    if (separate) modelStatus(speech, "等待设备获取语音服务的模型列表…");
    // One USB request at a time.
    const chatResult = await requestModels(chat);
    if (separate) modelStatus(speech, "设备正在获取语音服务的模型列表…");
    const speechResult = separate ? await requestModels(speech) : null;
    chat.list = chatResult.models || null;
    speech.list = separate ? speechResult.models || null : chat.list;
    fillModels();
    if (chatResult.models) {
      modelStatus(chat, chat.list.length
        ? `已获取 ${chat.list.length} 个模型${chatResult.truncated ? "（列表过长，只显示前一部分）" : ""}，默认选中第一个推荐模型。`
        : "服务没有返回模型，请手动填写模型名称。", chat.list.length ? "" : "error");
    } else if (!automatic || $(chat.url).value.trim()) {
      modelStatus(chat, chatResult.error, "error");
    }
    const result = separate ? speechResult : chatResult;
    if (result.models) {
      const missing = [["asr_model", "语音识别"], ["tts_model", "语音合成"]]
        .filter(([key]) => !groupModels(key, speech.list).recommended.length)
        .map(([, name]) => name);
      modelStatus(speech, missing.length
        ? `${separate ? "语音服务" : "对话服务"}没有${missing.join("和")}模型，可能不支持语音接口。` +
          (separate ? "请检查语音服务，或手动填写模型。" : "请在上方填写单独的语音服务。")
        : `已获取语音服务的模型，默认选中第一个推荐模型。`, missing.length ? "error" : "");
    } else if (separate) {
      modelStatus(speech, speechResult.error, "error");
    } else {
      modelStatus(speech, "语音使用对话服务；获取对话服务的模型后会一并更新。");
    }
  } finally {
    fetchingModels = false;
    operating = false;
    controls();
  }
}

function feedback(text, type = "") {
  $("feedback").textContent = text;
  $("feedback").className = `notice ${type}`;
}
function errorText(error) {
  if (error?.name === "NotFoundError") return "已取消选择设备，可以再次点击连接。";
  if (error?.name === "NetworkError" || error?.name === "InvalidStateError") {
    return "无法打开设备。请关闭占用串口的程序，检查 USB 数据线后重试。";
  }
  return messages[error?.message] || "操作失败，请检查 USB 连接后重试。";
}
function controls() {
  $("fields").disabled = !ready || operating;
  $("connect").disabled = operating;
  $("connect").hidden = ready;
  $("disconnect").hidden = !ready;
  $("disconnect").disabled = operating;
  $("reconnect").disabled = !ready || operating;
  $("fetch-models").disabled = !ready || operating || fetchingModels;
  $("password").disabled = !ready || operating || $("clear-password").checked;
  $("token").disabled = !ready || operating || $("clear-token").checked;
  $("speech-token").disabled = !ready || operating || $("clear-speech-token").checked;
}
function status(state) {
  $("wifi-status").textContent = wifiNames[state.wifi_state] || "状态未知";
  if (state.wifi_state === "retrying" && state.disconnect_reason) {
    $("wifi-status").textContent += `（原因 ${state.disconnect_reason}）`;
  }
  $("ip-status").textContent = state.ip || "—";
}
function load(state) {
  $("ssid").value = state.ssid;
  $("service-url").value = state.service_url || "";
  for (const key of modelFields) $(fieldId(key)).value = state[key] || "";
  $("speech-url").value = state.speech_url || "";
  $("speech-token").value = "";
  $("clear-speech-token").checked = false;
  $("speech-token-state").textContent = state.speech_token_set ? "已保存 · 留空保留" : "";
  $("speech-token").placeholder = state.speech_token_set
    ? "留空保留当前 API Key" : "粘贴语音服务的 API Key";
  services.chat.saved = Boolean(state.token_set);
  services.speech.saved = Boolean(state.speech_token_set);
  fillModels();
  $("password").value = $("token").value = "";
  $("clear-password").checked = $("clear-token").checked = false;
  $("password-state").textContent = state.password_set ? "已保存 · 留空保留" : "";
  $("token-state").textContent = state.token_set ? "已保存 · 留空保留" : "";
  $("password").placeholder = state.password_set ? "留空保留当前密码" : "输入网络密码";
  $("token").placeholder = state.token_set ? "留空保留当前 API Key" : "粘贴服务提供的 API Key";
  status(state);
}
function resetConnection() {
  ready = false;
  clearInterval(poll);
  poll = null;
  $("connection-badge").textContent = "等待 USB 连接";
  $("connection-badge").className = "badge";
  $("device-heading").textContent = "连接你的 AI Passport";
  $("device-description").textContent = "配置保存在设备中，重新连接后可以继续修改。";
  $("wifi-status").textContent = "等待连接设备";
  $("ip-status").textContent = "—";
  $("password").value = $("token").value = $("speech-token").value = "";
  controls();
}
async function operation(action) {
  if (operating) return;
  operating = true;
  controls();
  try {
    // Finish a status poll before a user action claims the single USB request slot.
    await pollingRequest?.catch(() => {});
    await action();
  }
  catch (error) { feedback(errorText(error), "error"); }
  finally { operating = false; controls(); }
}

$("connect").addEventListener("click", () => operation(async () => {
  if (client) await client.disconnect();
  const selected = await navigator.serial.requestPort({
    filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }],
  });
  const next = new ConfigurationClient({ onDisconnect: () => {
    if (client !== next) return;
    resetConnection();
    feedback("USB 已断开。未保存的内容仍留在页面，重新连接会读取设备当前配置。", "warning");
    void next.disconnect();
  } });
  client = next;
  feedback("正在读取设备配置…");
  const state = await next.connect(selected);
  ready = true;
  load(state);
  $("connection-badge").textContent = "USB 已连接";
  $("connection-badge").className = "badge connected";
  $("device-heading").textContent = "AI Passport 已连接";
  $("device-description").textContent = "在下方填写配置，保存后将直接写入设备。";
  feedback(state.storage_error !== "ESP_OK"
    ? "设备读取存储时出现异常。请核对配置，明确保存后再使用。"
    : "已读取设备配置。密码和令牌不会回显，留空会保留已保存的值。",
    state.storage_error !== "ESP_OK" ? "warning" : "success");
  // With a saved key and a connected device, fill the dropdowns right away.
  if (state.wifi_state === "connected" && state.service_url && state.token_set) {
    setTimeout(() => void fetchModels(true), 0);
  }
  poll = setInterval(async () => {
    if (!ready || operating || next.busy) return;
    try {
      pollingRequest = next.request("status");
      status(await pollingRequest);
    }
    catch (error) { if (ready) feedback(errorText(error), "warning"); }
    finally { pollingRequest = null; }
  }, 2500);
}));

$("disconnect").addEventListener("click", () => operation(async () => {
  await client?.disconnect();
  resetConnection();
  feedback("USB 会话已关闭，现在可以安全使用串口终端或烧录工具。");
}));
$("scan").addEventListener("click", () => operation(async () => {
  feedback("正在扫描附近的 2.4 GHz 网络…");
  const data = await client.request("scan");
  $("networks").replaceChildren();
  const seen = new Set();
  for (const network of data.networks || []) {
    if (!network.ssid || seen.has(network.ssid)) continue;
    seen.add(network.ssid);
    const option = document.createElement("option");
    option.value = network.ssid;
    option.label = `${network.rssi} dBm${network.open ? " · 开放网络" : ""}`;
    $("networks").append(option);
  }
  feedback(`找到 ${seen.size} 个网络。点击网络名称输入框选择，也可以手动填写。`, "success");
  $("ssid").focus();
}));
$("reconnect").addEventListener("click", () => operation(async () => {
  status(await client.request("reconnect"));
  feedback("已请求重新连接 Wi-Fi，网络状态会自动刷新。");
}));
$("fetch-models").addEventListener("click", () => void fetchModels(false));
$("service-url").addEventListener("change", () => void fetchModels(true));
$("token").addEventListener("change", () => void fetchModels(true));
$("speech-url").addEventListener("change", () => void fetchModels(true));
$("speech-token").addEventListener("change", () => void fetchModels(true));
$("clear-speech-token").addEventListener("change", controls);
for (const key of ["chat_model", "asr_model", "tts_model"]) {
  $(`${fieldId(key)}-select`).addEventListener("change", (event) => {
    const input = $(fieldId(key));
    if (event.target.value === CUSTOM_MODEL) {
      input.hidden = false;
      input.focus();
    } else {
      input.value = event.target.value;
      input.hidden = true;
    }
  });
}
$("voices").append(...VOICES.map((voice) => option(voice)));
$("clear-password").addEventListener("change", controls);
$("clear-token").addEventListener("change", controls);
$("configuration").addEventListener("submit", (event) => {
  event.preventDefault();
  const values = {
    ssid: $("ssid").value,
    password: $("clear-password").checked ? "" : $("password").value || null,
    service_url: $("service-url").value.trim(),
    service_token: $("clear-token").checked ? "" : $("token").value || null,
  };
  for (const key of modelFields) values[key] = $(fieldId(key)).value;
  values.speech_url = $("speech-url").value.trim();
  values.speech_token = $("clear-speech-token").checked ? "" : $("speech-token").value || null;
  void operation(async () => {
    feedback("正在保存配置…");
    try {
      const state = await client.request("save", values);
      load(state);
      feedback(state.wifi_state === "connected"
        ? "配置已保存到设备，Wi-Fi 已连接。可以在设备的 AI 对话页按 OK 试一次对话。"
        : "配置已保存到设备。请查看下方 Wi-Fi 连接结果；AI 服务会在第一次对话时验证。", "success");
    } finally {
      // Do not retain submitted credentials in JavaScript objects or form fields.
      values.password = values.service_token = values.speech_token = null;
      $("password").value = $("token").value = $("speech-token").value = "";
    }
  });
});

if (!("serial" in navigator) || !window.isSecureContext) {
  $("unsupported").hidden = false;
  $("connect").disabled = true;
}
window.addEventListener("pagehide", () => { if (client) void client.disconnect(); });

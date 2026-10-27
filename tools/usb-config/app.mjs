import { ConfigurationClient } from "./protocol.mjs";

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
  invalid_settings: "配置格式不正确。请检查字段长度、Wi-Fi 密码和服务地址。",
  invalid_url: "服务地址需要使用 http://、https://、ws:// 或 wss://，且不能包含用户名、密码或片段。",
  invalid_password: "Wi-Fi 密码应为 8–63 字节，或 64 位十六进制密钥。开放网络请勾选不设密码。",
  password_required: "你修改了 Wi-Fi 名称，请重新输入密码；开放网络请勾选不设密码。",
  storage_failed: "设备保存失败，原来的有效配置仍在使用。请检查设备存储状态后重试。",
  scan_busy: "Wi-Fi 正在连接，暂时无法扫描。请等待连接结束后重试，或手动填写名称。",
  scan_failed: "扫描失败，请重试或手动填写网络名称。",
  wifi_unavailable: "设备的 Wi-Fi 暂时不可用，USB 配置仍可使用。",
  busy: "设备正在处理上一项操作，请稍候。",
};
const wifiNames = {
  unconfigured: "尚未配置 Wi-Fi", connecting: "正在连接路由器",
  connected: "Wi-Fi 已连接", retrying: "连接失败，正在间隔重试", error: "Wi-Fi 不可用",
};

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
  $("password").disabled = !ready || operating || $("clear-password").checked;
  $("token").disabled = !ready || operating || $("clear-token").checked;
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
  $("password").value = $("token").value = "";
  $("clear-password").checked = $("clear-token").checked = false;
  $("password-state").textContent = state.password_set ? "已保存 · 留空保留" : "";
  $("token-state").textContent = state.token_set ? "已保存 · 留空保留" : "";
  $("password").placeholder = state.password_set ? "留空保留当前密码" : "输入网络密码";
  $("token").placeholder = state.token_set ? "留空保留当前令牌" : "粘贴服务提供的令牌";
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
  $("password").value = $("token").value = "";
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
  void operation(async () => {
    feedback("正在保存配置…");
    try {
      const state = await client.request("save", values);
      load(state);
      feedback(state.wifi_state === "connected"
        ? "配置已保存到设备，Wi-Fi 已连接。AI 服务地址已保存，尚未验证服务连接。"
        : "配置已保存到设备。请查看下方 Wi-Fi 连接结果；AI 服务连接尚未验证。", "success");
    } finally {
      // Do not retain submitted credentials in JavaScript objects or form fields.
      values.password = values.service_token = null;
      $("password").value = $("token").value = "";
    }
  });
});

if (!("serial" in navigator) || !window.isSecureContext) {
  $("unsupported").hidden = false;
  $("connect").disabled = true;
}
window.addEventListener("pagehide", () => { if (client) void client.disconnect(); });

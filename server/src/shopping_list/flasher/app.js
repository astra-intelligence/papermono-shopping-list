// PaperMono web flasher: esptool-js over WebSerial.
// Safety model (see ../docs/06-flashing-and-recovery.md):
//   * refuse anything that isn't an ESP32-S3 with 16 MB flash
//   * encourage a full per-unit backup before the first write
//   * never erase the whole chip unless explicitly asked
//   * keep the existing NVS when an image would only blank it (0xFF padding)
//   * MD5-verify every written segment (esptool-js flashMd5sum)
import { ESPLoader, Transport, HardReset } from "https://cdn.jsdelivr.net/npm/esptool-js@0.7.0/bundle.js";

const FLASH_BYTES = 16 * 1024 * 1024;
const ESP32S3_CHIP_ID = 9;
const PT_OFFSET = 0x8000;
const PT_MAX = 0xc00;
const APP_OFFSET = 0x10000;
const ESPRESSIF_VID = 0x303a;

const $ = (id) => document.getElementById(id);
const hex = (n, w = 0) => "0x" + n.toString(16).padStart(w, "0");
const fmtSize = (n) => n >= 1048576 ? (n / 1048576).toFixed(2) + " MB" : n >= 1024 ? (n / 1024).toFixed(1) + " KB" : n + " B";

const state = {
  port: null, transport: null, loader: null,
  chipDesc: "", mac: "", flashSize: "", devicePartitions: null,
  image: null,        // { name, source, segments:[{address,data}], meta }
  consoleOn: false, consoleTransport: null, busy: false,
};

/* ------------------------------------------------------------------ log -- */
const logEl = $("log");
function log(msg, cls) {
  const line = document.createElement("span");
  if (cls) line.className = cls;
  line.textContent = msg.endsWith("\n") ? msg : msg + "\n";
  logEl.appendChild(line);
  logEl.scrollTop = logEl.scrollHeight;
}
function logRaw(text) {
  logEl.appendChild(document.createTextNode(text));
  if (logEl.childNodes.length > 4000) logEl.removeChild(logEl.firstChild);
  logEl.scrollTop = logEl.scrollHeight;
}
const terminal = {
  clean() {},
  writeLine: (d) => log(d),
  write: (d) => logRaw(d),
};

/* ------------------------------------------------------------ ui state -- */
function refreshButtons() {
  const connected = !!state.loader;
  const ok = connected && state.chipOk;
  $("btnConnect").disabled = state.busy || connected || state.consoleOn || !$("confirmModel").checked || !("serial" in navigator);
  $("btnDisconnect").disabled = state.busy || (!connected && !state.consoleOn);
  $("btnBackup").disabled = state.busy || !ok;
  $("btnFlash").disabled = state.busy || !ok || !state.image || !state.image.ok;
  $("btnConsole").disabled = state.busy || connected || !("serial" in navigator);
  $("btnConsole").textContent = state.consoleOn ? "Close console" : "Open console (115200)";
  $("btnReset").disabled = state.busy || !state.consoleOn;
}
async function busy(label, fn) {
  if (state.busy) return;
  state.busy = true; refreshButtons();
  try { await fn(); }
  catch (e) { console.error(e); log(`✗ ${label} failed: ${e && e.message ? e.message : e}`, "err"); }
  finally { state.busy = false; refreshButtons(); }
}
function progress(pct) {
  const p = $("progress");
  p.hidden = pct == null;
  if (pct != null) p.value = pct;
}

/* ------------------------------------------------------ image parsing -- */
function u32(u8, o) { return (u8[o] | (u8[o + 1] << 8) | (u8[o + 2] << 16) | (u8[o + 3] << 24)) >>> 0; }
function cstr(u8, o, n) {
  let s = ""; for (let i = 0; i < n && u8[o + i]; i++) s += String.fromCharCode(u8[o + i]); return s;
}
function parsePartitionTable(u8) {
  const parts = [];
  for (let o = 0; o + 32 <= u8.length && o < PT_MAX; o += 32) {
    const m0 = u8[o], m1 = u8[o + 1];
    if (m0 === 0xaa && m1 === 0x50) {
      const type = u8[o + 2], subtype = u8[o + 3];
      parts.push({ type, subtype, offset: u32(u8, o + 4), size: u32(u8, o + 8), label: cstr(u8, o + 12, 16) });
    } else if (m0 === 0xeb && m1 === 0xeb) {
      continue; // MD5 of table
    } else break;
  }
  return parts;
}
function partTypeName(p) {
  if (p.type === 0) return p.subtype === 0 ? "app/factory" : p.subtype >= 0x10 && p.subtype < 0x20 ? `app/ota_${p.subtype - 0x10}` : `app/${hex(p.subtype)}`;
  const d = { 0: "ota", 1: "phy", 2: "nvs", 3: "coredump", 4: "nvs_keys", 5: "efuse", 0x80: "esphttpd", 0x81: "fat", 0x82: "spiffs", 0x83: "littlefs" };
  return `data/${d[p.subtype] ?? hex(p.subtype)}`;
}
// ESP image header: magic E9, ..., chip_id (u16) at offset 12.
function imageChipId(u8, base) {
  if (u8.length < base + 24 || u8[base] !== 0xe9) return null;
  return u8[base + 12] | (u8[base + 13] << 8);
}
// esp_app_desc_t follows the first segment header: magic ABCD5432 at base+0x20.
function appDesc(u8, base) {
  const o = base + 0x20;
  if (u8.length < o + 256 || u32(u8, o) !== 0xabcd5432) return null;
  return {
    version: cstr(u8, o + 16, 32), project: cstr(u8, o + 48, 32),
    time: cstr(u8, o + 80, 16), date: cstr(u8, o + 96, 16), idf: cstr(u8, o + 112, 32),
  };
}
function allFF(u8, start, end) {
  for (let i = start; i < Math.min(end, u8.length); i++) if (u8[i] !== 0xff) return false;
  return true;
}

// Build a flash plan from one image file.
function planFromFile(name, u8, offsetChoice) {
  const plan = { name, ok: false, lines: [], segments: [], meta: {} };
  const isMerged = u8.length > PT_OFFSET + 32 && u8[PT_OFFSET] === 0xaa && u8[PT_OFFSET + 1] === 0x50;
  let offset = offsetChoice === "auto" ? (isMerged ? 0 : appDesc(u8, 0) ? APP_OFFSET : null) : Number(offsetChoice);
  if (offset === null) {
    plan.lines.push("Can't auto-detect where this image goes (it isn't a merged image or an app). Pick an offset.");
    return plan;
  }
  if (offset + u8.length > FLASH_BYTES) { plan.lines.push(`Image (${fmtSize(u8.length)}) does not fit at ${hex(offset)} in 16 MB.`); return plan; }

  if (offset === 0) {
    const chip = imageChipId(u8, 0);
    if (chip !== ESP32S3_CHIP_ID) { plan.lines.push(`Bootloader header chip id = ${chip ?? "none"}; expected ${ESP32S3_CHIP_ID} (ESP32-S3). Refusing.`); return plan; }
    if (!isMerged) { plan.lines.push("No partition table at 0x8000 in this file, so it isn't a merged image. Refusing to write it at 0x0."); return plan; }
    const parts = parsePartitionTable(u8.subarray(PT_OFFSET, PT_OFFSET + PT_MAX));
    const app = parts.find((p) => p.type === 0 && p.offset === APP_OFFSET);
    const desc = appDesc(u8, APP_OFFSET);
    if (desc && imageChipId(u8, APP_OFFSET) !== ESP32S3_CHIP_ID) { plan.lines.push("App at 0x10000 is not an ESP32-S3 image. Refusing."); return plan; }
    plan.meta = { kind: "merged", parts, desc };
    plan.lines.push(`Merged image, ${fmtSize(u8.length)} → 0x0`);
    if (desc) plan.lines.push(`App: ${desc.project} ${desc.version} (built ${desc.date} ${desc.time}, IDF ${desc.idf})`);
    plan.lines.push("Partition table in image: " + parts.map((p) => `${p.label}@${hex(p.offset)}`).join(", "));
    if (!app) plan.lines.push("⚠ no app partition at 0x10000 in the image's table");

    // Segment the image; optionally skip the NVS range when it is only 0xFF padding.
    const nvs = parts.find((p) => p.type === 1 && p.subtype === 2);
    const keepNvs = $("keepNvs")?.checked ?? true;
    if (nvs && keepNvs && nvs.offset < u8.length && allFF(u8, nvs.offset, nvs.offset + nvs.size)) {
      const nvsEnd = nvs.offset + nvs.size;
      plan.segments.push({ address: 0, data: u8.slice(0, nvs.offset) });
      if (u8.length > nvsEnd) plan.segments.push({ address: nvsEnd, data: u8.slice(nvsEnd) });
      plan.lines.push(`Keeping existing NVS (${hex(nvs.offset)}+${fmtSize(nvs.size)}). The image only had blank padding there.`);
    } else {
      plan.segments.push({ address: 0, data: u8 });
      if (nvs && nvs.offset < u8.length) plan.lines.push("NVS region will be overwritten (settings/Wi-Fi reset).");
    }
  } else if (offset === APP_OFFSET) {
    const chip = imageChipId(u8, 0);
    const desc = appDesc(u8, 0);
    if (chip !== ESP32S3_CHIP_ID || !desc) { plan.lines.push(`Not an ESP32-S3 app image (chip id ${chip ?? "none"}). Refusing.`); return plan; }
    plan.meta = { kind: "app", desc };
    plan.lines.push(`App image ${desc.project} ${desc.version} (${fmtSize(u8.length)}) → 0x10000`);
    const dp = state.devicePartitions;
    if (!dp) { plan.lines.push("Connect first, so the device's partition table can be checked."); return plan; }
    const slot = dp.find((p) => p.type === 0 && p.offset === APP_OFFSET);
    if (!slot) { plan.lines.push("Device has no app partition at 0x10000. Use a merged image instead."); return plan; }
    if (slot.size < u8.length) { plan.lines.push(`App (${fmtSize(u8.length)}) is larger than slot ${slot.label} (${fmtSize(slot.size)}). Refusing.`); return plan; }
    plan.segments.push({ address: APP_OFFSET, data: u8 });
    const otadata = dp.find((p) => p.type === 1 && p.subtype === 0);
    if (otadata) {
      plan.segments.push({ address: otadata.offset, data: new Uint8Array(otadata.size).fill(0xff) });
      plan.lines.push(`Also blanking otadata @${hex(otadata.offset)} so the bootloader starts ${slot.label}.`);
    }
  } else {
    plan.lines.push(`Unsupported offset ${hex(offset)}.`); return plan;
  }
  plan.ok = true;
  return plan;
}

function showPlan(plan) {
  const el = $("plan");
  el.hidden = !plan;
  if (!plan) return;
  el.classList.toggle("bad", !plan.ok);
  el.textContent = (plan.ok ? "Ready to write:\n" : "Can't flash this:\n") + plan.lines.join("\n") +
    (plan.ok ? "\nSegments: " + plan.segments.map((s) => `${hex(s.address)} (${fmtSize(s.data.length)})`).join(", ") : "");
}

/* ------------------------------------------------------------ connect -- */
async function connect() {
  await busy("Connect", async () => {
    state.port = await requestPort();
    state.transport = new Transport(state.port, false);
    state.transport.setDeviceLostCallback?.(() => { log("Device disconnected.", "warn"); resetConnection(); });
    state.loader = new ESPLoader({ transport: state.transport, baudrate: 921600, romBaudrate: 115200, terminal });
    log("Connecting… (if this hangs: hold power ~2 s until the red LED blinks, then retry)");
    state.chipDesc = await state.loader.main();
    const chipName = state.loader.chip.CHIP_NAME;
    state.mac = await state.loader.chip.readMac(state.loader);
    state.flashSize = (await state.loader.detectFlashSize()) || "unknown";
    state.chipOk = chipName === "ESP32-S3" && state.flashSize === "16MB";

    const pt = await state.loader.readFlash(PT_OFFSET, PT_MAX);
    state.devicePartitions = parsePartitionTable(pt);
    renderDevice(chipName);
    if (!state.chipOk) log(`✗ Expected ESP32-S3 with 16MB flash, got ${chipName} / ${state.flashSize}. Flashing disabled.`, "err");
    else log(`✓ Connected: ${state.chipDesc}, ${state.flashSize}, MAC ${state.mac}`, "ok");
    reevaluatePlan();
  });
}
function renderDevice(chipName) {
  const dl = $("deviceInfo");
  dl.hidden = false;
  dl.innerHTML = "";
  const add = (k, v, cls) => { const dt = document.createElement("dt"); dt.textContent = k; const dd = document.createElement("dd"); dd.textContent = v; if (cls) dd.className = cls; dl.append(dt, dd); };
  add("Chip", state.chipDesc, chipName === "ESP32-S3" ? "ok" : "err");
  add("Flash", state.flashSize, state.flashSize === "16MB" ? "ok" : "err");
  add("MAC", state.mac);
  const box = $("partitions"), tb = box.querySelector("tbody");
  tb.innerHTML = "";
  box.hidden = false;
  if (!state.devicePartitions.length) {
    tb.innerHTML = "<tr><td colspan=4>No valid partition table found at 0x8000 (blank or erased flash)</td></tr>";
  }
  for (const p of state.devicePartitions) {
    const tr = document.createElement("tr");
    for (const v of [p.label, partTypeName(p), hex(p.offset), fmtSize(p.size)]) { const td = document.createElement("td"); td.textContent = v; tr.append(td); }
    tb.append(tr);
  }
}
async function resetConnection() {
  try { await state.transport?.disconnect(); } catch {}
  state.transport = null; state.loader = null; state.chipOk = false;
  refreshButtons();
}
async function disconnect() {
  if (state.consoleOn) await stopConsole();
  if (state.loader) {
    try { await state.loader.after("hard_reset"); } catch {}
    await resetConnection();
    log("Disconnected (device reset).");
  }
  refreshButtons();
}

/* ------------------------------------------------------------- backup -- */
async function sha256Hex(u8) {
  if (!globalThis.crypto?.subtle) return null; // only available on HTTPS / localhost
  const d = await crypto.subtle.digest("SHA-256", u8);
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}
async function backup() {
  await busy("Backup", async () => {
    log("Reading 16 MB flash…");
    const t0 = performance.now();
    progress(0);
    const data = await state.loader.readFlash(0, FLASH_BYTES, (_pkt, done, total) => progress((done / total) * 100));
    progress(null);
    const sum = await sha256Hex(data);
    const stamp = new Date().toISOString().replace(/[:T]/g, "-").slice(0, 16);
    const name = `papermono-${state.mac.replaceAll(":", "")}-${stamp}.bin`;
    const url = URL.createObjectURL(new Blob([data], { type: "application/octet-stream" }));
    const a = Object.assign(document.createElement("a"), { href: url, download: name });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
    log(`✓ Backup saved as ${name} (${((performance.now() - t0) / 1000).toFixed(0)} s)\n  sha256 ${sum ?? "n/a"}`, "ok");
    log(`  restore later with: esptool --chip esp32s3 write-flash 0 ${name}`);
  });
}

/* -------------------------------------------------------------- flash -- */
let fileBytes = null, fileName = "";
function reevaluatePlan() {
  if (catalogSelection) { state.image = catalogSelection; }
  else if (fileBytes) { state.image = planFromFile(fileName, fileBytes, $("fileOffset").value); }
  else state.image = null;
  showPlan(state.image);
  refreshButtons();
}
async function flash() {
  const plan = state.image;
  if (!plan?.ok) return;
  if ($("eraseAll").checked && !confirm("Erase the ENTIRE flash, including NVS/calibration, before writing?")) return;
  await busy("Flash", async () => {
    log(`Flashing ${plan.name}…`);
    progress(0);
    const total = plan.segments.reduce((a, s) => a + s.data.length, 0);
    const done = new Array(plan.segments.length).fill(0);
    await state.loader.writeFlash({
      fileArray: plan.segments.map((s) => ({ address: s.address, data: s.data })),
      flashMode: "keep", flashFreq: "keep", flashSize: "keep",
      eraseAll: $("eraseAll").checked, compress: true,
      reportProgress: (i, written) => { done[i] = written; progress((done.reduce((a, b) => a + b, 0) / total) * 100); },
      calculateMD5Hash: (img) => SparkMD5.ArrayBuffer.hash(img.slice().buffer),
    });
    progress(null);
    log("✓ Written and MD5-verified. Resetting device…", "ok");
    await state.loader.after("hard_reset");
    await resetConnection();
    log("Done. Open the console to watch it boot. The e-paper only changes when the new firmware refreshes it.", "ok");
  });
}

/* ------------------------------------------------------------ catalog -- */
let catalogSelection = null;
async function loadCatalog() {
  let cat;
  try {
    const r = await fetch("firmware/catalog.json", { cache: "no-store" });
    if (!r.ok) return;
    cat = await r.json();
  } catch { return; }
  if (!Array.isArray(cat.firmware) || !cat.firmware.length) return;
  const box = $("catalog");
  $("catalogBox").hidden = false;
  for (const fw of cat.firmware) {
    const lab = document.createElement("label");
    lab.className = "fw";
    const inp = Object.assign(document.createElement("input"), { type: "radio", name: "fw", value: fw.id });
    const txt = document.createElement("div");
    const title = document.createElement("b"); title.textContent = `${fw.name} ${fw.version ?? ""}`;
    const small = document.createElement("small"); small.textContent = fw.description ?? "";
    txt.append(title, small);
    lab.append(inp, txt);
    inp.addEventListener("change", () => selectCatalog(fw));
    box.append(lab);
  }
}
async function selectCatalog(fw) {
  $("fileInput").value = ""; fileBytes = null;
  await busy("Load firmware", async () => {
    // fw.file (merged @0x0) or fw.images [{file, offset, sha256}]
    const images = fw.images ?? [{ file: fw.file, offset: fw.offset ?? 0, sha256: fw.sha256 }];
    const loaded = [];
    for (const im of images) {
      log(`Downloading ${im.file}…`);
      const r = await fetch("firmware/" + im.file, { cache: "no-store" });
      if (!r.ok) throw new Error(`${im.file}: HTTP ${r.status}`);
      const u8 = new Uint8Array(await r.arrayBuffer());
      if (im.sha256) {
        const got = await sha256Hex(u8);
        if (got === null) throw new Error("SHA-256 check needs HTTPS or localhost");
        if (got !== im.sha256.toLowerCase()) throw new Error(`${im.file}: SHA-256 mismatch (got ${got})`);
        log(`  sha256 ok`, "ok");
      }
      loaded.push({ ...im, data: u8 });
    }
    if (loaded.length === 1) {
      catalogSelection = planFromFile(fw.name, loaded[0].data, String(loaded[0].offset));
    } else {
      catalogSelection = { name: fw.name, ok: true, segments: loaded.map((l) => ({ address: l.offset, data: l.data })),
        lines: loaded.map((l) => `${l.file} → ${hex(l.offset)} (${fmtSize(l.data.length)})`) };
    }
  });
  reevaluatePlan();
}

/* ------------------------------------------------------------ console -- */
const requestPort = () => navigator.serial.requestPort({ filters: [{ usbVendorId: ESPRESSIF_VID }] });
async function startConsole() {
  await busy("Console", async () => {
    if (!state.port) state.port = await requestPort();
    let t = new Transport(state.port, false);
    try { await t.connect(115200); }
    catch {
      // After a reset the USB-JTAG port may have re-enumerated; ask for it again.
      log("Port not available. Pick the PaperMono again.", "warn");
      state.port = await requestPort();
      t = new Transport(state.port, false);
      await t.connect(115200);
    }
    // Release RTS before DTR: the USB-Serial-JTAG maps RTS=1/DTR=0 to "chip in reset".
    await t.setRTS(false);
    await t.setDTR(false);
    state.consoleTransport = t; state.consoleOn = true;
    log("— console open (press Reset to see the boot log) —", "ok");
    const dec = new TextDecoder();
    t.rawRead((d) => logRaw(dec.decode(d, { stream: true })), () => !state.consoleOn)
      .finally(() => { if (state.consoleOn) { state.consoleOn = false; log("— console ended —", "warn"); } refreshButtons(); });
  });
}
async function stopConsole() {
  state.consoleOn = false;
  try { await state.consoleTransport?.disconnect(); } catch {}
  state.consoleTransport = null;
  log("— console closed —");
}
async function resetDevice() {
  if (!state.consoleTransport) return;
  await new HardReset(state.consoleTransport, false).reset();
  log("— reset —");
}

/* --------------------------------------------------------------- wire -- */
function init() {
  if (!("serial" in navigator)) $("unsupported").hidden = false;
  // NVS option lives next to erase option
  const keep = document.createElement("label");
  keep.className = "check";
  keep.innerHTML = '<input type="checkbox" id="keepNvs" checked> Keep existing NVS (settings, Wi-Fi) if the image only contains blank padding there';
  $("eraseAll").closest("label").before(keep);

  $("confirmModel").addEventListener("change", refreshButtons);
  $("btnConnect").addEventListener("click", connect);
  $("btnDisconnect").addEventListener("click", () => busy("Disconnect", disconnect));
  $("btnBackup").addEventListener("click", backup);
  $("btnFlash").addEventListener("click", flash);
  $("btnConsole").addEventListener("click", () => state.consoleOn ? stopConsole().then(refreshButtons) : startConsole());
  $("btnReset").addEventListener("click", () => busy("Reset", resetDevice));
  $("btnClear").addEventListener("click", () => { logEl.textContent = ""; });
  $("fileInput").addEventListener("change", async (e) => {
    const f = e.target.files[0];
    catalogSelection = null;
    document.querySelectorAll('input[name=fw]').forEach((r) => (r.checked = false));
    if (!f) { fileBytes = null; reevaluatePlan(); return; }
    fileName = f.name; fileBytes = new Uint8Array(await f.arrayBuffer());
    reevaluatePlan();
    log(`Loaded ${f.name} (${fmtSize(fileBytes.length)}) sha256 ${(await sha256Hex(fileBytes)) ?? "n/a"}`);
  });
  $("fileOffset").addEventListener("change", reevaluatePlan);
  document.addEventListener("change", (e) => { if (e.target.id === "keepNvs") reevaluatePlan(); });
  loadCatalog();
  refreshButtons();
}
init();

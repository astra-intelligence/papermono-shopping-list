const presetsEl = document.getElementById("presets");
const stripsEl = document.getElementById("strips");
const sameRow = document.getElementById("sameRow");
const sameBox = document.getElementById("sameBox");
const saveBtn = document.getElementById("saveBtn");
const toast = document.getElementById("toast");

// { weekday, weekend, weekend_same, presets: [...] } from GET /api/sync-schedule.
let state = null;
let saved = null; // JSON of the last-saved choice, to know when there is something to save
let profile = "weekday";

let toastTimer = null;
function showToast(message, ok = false) {
  toast.textContent = message;
  toast.classList.toggle("ok", ok);
  toast.classList.remove("hidden");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.add("hidden"), 4000);
}

function minutes(hhmm) {
  const [h, m] = hhmm.split(":").map(Number);
  return h * 60 + m;
}

function presetById(id) {
  return state.presets.find((p) => p.id === id);
}

function choice() {
  return JSON.stringify({ weekday: state.weekday, weekend: state.weekend, weekend_same: state.weekend_same });
}

function updateSaveButton() {
  saveBtn.disabled = choice() === saved;
}

function renderPresets() {
  const locked = profile === "weekend" && state.weekend_same;
  const current = locked || profile === "weekday" ? state.weekday : state.weekend;
  sameRow.classList.toggle("hidden", profile !== "weekend");
  sameBox.checked = state.weekend_same;
  presetsEl.replaceChildren(
    ...state.presets.map((p) => {
      const label = document.createElement("label");
      label.className = "preset";
      const input = document.createElement("input");
      input.type = "radio";
      input.name = "preset";
      input.value = p.id;
      input.checked = p.id === current;
      input.disabled = locked;
      const name = document.createElement("b");
      name.textContent = p.name;
      const desc = document.createElement("span");
      desc.textContent = p.description;
      const mini = document.createElement("span");
      mini.className = "mini";
      mini.textContent = `about ${p.syncs_per_day} syncs a day`;
      label.append(input, name, desc, mini);
      return label;
    })
  );
}

// One cell per 15 minutes; a cell is lit if the preset syncs at any point inside it.
function strip(preset) {
  const wrap = document.createElement("div");
  wrap.className = "strip";
  wrap.setAttribute("role", "img");
  for (let i = 0; i < 96; i++) {
    const cell = document.createElement("i");
    const from = i * 15;
    const to = from + 15;
    const on = preset.windows.some((w) => minutes(w.start) < to && from < minutes(w.end));
    if (on) cell.className = "on";
    wrap.appendChild(cell);
  }
  return wrap;
}

function renderStrips() {
  const rows = [["Weekdays", presetById(state.weekday)]];
  rows.push([
    state.weekend_same ? "Weekends (same as weekdays)" : "Weekends",
    presetById(state.weekend_same ? state.weekday : state.weekend),
  ]);
  const nodes = [];
  for (const [label, preset] of rows) {
    const title = document.createElement("div");
    title.className = "strip-label";
    title.textContent = `${label}: ${preset.name}`;
    const s = strip(preset);
    s.setAttribute("aria-label", `${label}, ${preset.name}`);
    const ticks = document.createElement("div");
    ticks.className = "ticks";
    for (const t of ["00", "06", "12", "18", "24"]) {
      const span = document.createElement("span");
      span.textContent = t;
      ticks.appendChild(span);
    }
    nodes.push(title, s, ticks);
  }
  stripsEl.replaceChildren(...nodes);
}

function render() {
  renderPresets();
  renderStrips();
  updateSaveButton();
}

document.querySelectorAll("input[name=profile]").forEach((radio) => {
  radio.addEventListener("change", () => {
    profile = radio.value;
    render();
  });
});

sameBox.addEventListener("change", () => {
  state.weekend_same = sameBox.checked;
  render();
});

presetsEl.addEventListener("change", (e) => {
  if (e.target.name !== "preset") return;
  state[profile] = e.target.value;
  render();
});

saveBtn.addEventListener("click", async () => {
  saveBtn.disabled = true;
  try {
    const res = await fetch("/api/sync-schedule", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ weekday: state.weekday, weekend: state.weekend, weekend_same: state.weekend_same }),
    });
    if (!res.ok) throw new Error(`Server said ${res.status}`);
    state = await res.json();
    saved = choice();
    showToast("Saved. The device picks it up on its next sync.", true);
  } catch (err) {
    showToast(`Couldn't save the schedule: ${err.message}. Try again.`);
  }
  updateSaveButton();
});

async function load() {
  try {
    const res = await fetch("/api/sync-schedule");
    if (!res.ok) throw new Error(`Server said ${res.status}`);
    state = await res.json();
    saved = choice();
    render();
  } catch (err) {
    document.getElementById("loadingMessage").textContent = "Couldn't load the schedule. Reload to try again.";
    showToast(`Couldn't load the schedule: ${err.message}`);
  }
}

load();

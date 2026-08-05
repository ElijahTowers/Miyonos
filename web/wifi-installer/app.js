const token = document.querySelector('meta[name="miyonos-token"]').content;
const form = document.querySelector("#installer-form");
const hostInput = document.querySelector("#host");
const passwordInput = document.querySelector("#password");
const userInput = document.querySelector("#user");
const keepBackupInput = document.querySelector("#keep-backup");
const installButton = document.querySelector("#install-button");
const rollbackButton = document.querySelector("#rollback-button");
const rollbackDialog = document.querySelector("#rollback-dialog");
const showPasswordButton = document.querySelector("#show-password");
const formError = document.querySelector("#form-error");
const statusCard = document.querySelector("#status-card");
const statusIcon = document.querySelector("#status-icon");
const statusLabel = document.querySelector("#status-label");
const statusTitle = document.querySelector("#status-title");
const statusMessage = document.querySelector("#status-message");
const logOutput = document.querySelector("#log-output");
const shutdownButton = document.querySelector("#shutdown-button");
const phases = ["connecting", "uploading", "finishing", "done"];
let polling = null;

function requestHeaders() {
  return {
    "Content-Type": "application/json",
    "X-Miyonos-Token": token,
  };
}

function validIPv4(value) {
  const parts = value.trim().split(".");
  return parts.length === 4 && parts.every((part) => {
    if (!/^\d{1,3}$/.test(part)) return false;
    const number = Number(part);
    return number >= 0 && number <= 255 && String(number) === String(Number(part));
  });
}

function setBusy(busy) {
  installButton.disabled = busy;
  rollbackButton.disabled = busy;
  hostInput.disabled = busy;
  passwordInput.disabled = busy;
  userInput.disabled = busy;
  keepBackupInput.disabled = busy;
}

function updateProgress(phase) {
  const activeIndex = phases.indexOf(phase);
  document.querySelectorAll("#progress li").forEach((item, index) => {
    item.classList.toggle("complete", activeIndex >= 0 && index < activeIndex);
    item.classList.toggle("active", index === activeIndex);
  });
}

function renderStatus(status) {
  statusCard.classList.add("visible");
  statusCard.classList.toggle("running", status.state === "running");
  statusCard.classList.toggle("error", status.state === "error");
  statusIcon.textContent = status.state === "error" ? "!" : "✓";
  statusLabel.textContent = status.state === "running"
    ? "WORKING"
    : status.state === "success"
      ? "SUCCESS"
      : status.state === "error"
        ? "NOT COMPLETED"
        : "STATUS";
  statusTitle.textContent = status.state === "running"
    ? "Please wait…"
    : status.state === "success"
      ? "Miyonos is ready to use"
      : status.state === "error"
        ? "This did not work yet"
        : "Ready to connect";
  statusMessage.textContent = status.message;
  logOutput.textContent = (status.logs || []).join("\n");
  updateProgress(status.phase);
  setBusy(status.state === "running");
  shutdownButton.hidden = status.state === "running" || status.state === "idle";
  statusCard.scrollIntoView({ behavior: "smooth", block: "nearest" });
}

async function getStatus() {
  const response = await fetch("/api/status", {
    headers: requestHeaders(),
  });
  if (!response.ok) throw new Error("The status could not be loaded.");
  return response.json();
}

async function pollStatus() {
  try {
    const status = await getStatus();
    renderStatus(status);
    if (status.state !== "running") {
      clearInterval(polling);
      polling = null;
    }
  } catch (error) {
    clearInterval(polling);
    polling = null;
    renderStatus({
      state: "error",
      phase: "error",
      message: error.message,
      logs: [],
    });
  }
}

async function run(action) {
  const host = hostInput.value.trim();
  formError.textContent = "";
  if (!validIPv4(host)) {
    formError.textContent =
      "Enter a valid IP address, for example 192.168.1.50.";
    hostInput.focus();
    return;
  }
  setBusy(true);
  renderStatus({
    state: "running",
    phase: "connecting",
    message: "Connecting to your Miyoo…",
    logs: [],
  });
  try {
    const response = await fetch("/api/run", {
      method: "POST",
      headers: requestHeaders(),
      body: JSON.stringify({
        action,
        host,
        user: userInput.value.trim() || "onion",
        password: passwordInput.value,
        keep_backup: keepBackupInput.checked,
      }),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || "The operation could not start.");
    renderStatus(result);
    polling = setInterval(pollStatus, 500);
  } catch (error) {
    renderStatus({
      state: "error",
      phase: "error",
      message: error.message,
      logs: [],
    });
  }
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  run("install");
});

showPasswordButton.addEventListener("click", () => {
  const showing = passwordInput.type === "text";
  passwordInput.type = showing ? "password" : "text";
  showPasswordButton.textContent = showing ? "Show" : "Hide";
});

rollbackButton.addEventListener("click", () => {
  formError.textContent = "";
  if (!validIPv4(hostInput.value.trim())) {
    formError.textContent =
      "Enter the IP address of your Miyoo first.";
    hostInput.focus();
    return;
  }
  rollbackDialog.showModal();
});

rollbackDialog.addEventListener("close", () => {
  if (rollbackDialog.returnValue === "confirm") run("rollback");
});

shutdownButton.addEventListener("click", async () => {
  shutdownButton.disabled = true;
  await fetch("/api/shutdown", {
    method: "POST",
    headers: requestHeaders(),
    body: "{}",
  });
  document.body.innerHTML = `
    <main class="shell">
      <section class="installer-card">
        <p class="eyebrow">DONE</p>
        <h1>You can close this tab.</h1>
        <p class="intro">The local Miyonos installer has stopped.</p>
      </section>
    </main>`;
});

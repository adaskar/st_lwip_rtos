(function () {
  "use strict";

  const state = {
    token: sessionStorage.getItem("st.authToken") || "",
    ws: null,
    reconnectTimer: 0,
    fallbackTimer: 0,
    reconnectDelay: 250,
    outputs: [],
    hasState: false,
    hasNetwork: false
  };

  const $ = (id) => document.getElementById(id);

  const views = Array.from(document.querySelectorAll(".view"));
  const navItems = Array.from(document.querySelectorAll(".nav-item"));
  const outputList = $("outputs-list");
  const eventLog = $("event-log");

  function authHeaders(extra) {
    const headers = Object.assign({}, extra || {});
    if (state.token) headers["X-ST-Auth"] = state.token;
    return headers;
  }

  async function api(path, options) {
    const request = Object.assign({ headers: {} }, options || {});
    request.headers = authHeaders(request.headers);

    const response = await fetch(path, request);
    if (response.status === 401) {
      window.location.replace("/login.html");
      throw new Error("unauthorized");
    }
    return response;
  }

  function log(message) {
    const row = document.createElement("div");
    row.textContent = new Date().toLocaleTimeString() + "  " + message;
    eventLog.prepend(row);
    while (eventLog.children.length > 80) eventLog.lastChild.remove();
  }

  function formatUptime(seconds) {
    const value = Number(seconds) || 0;
    const hours = Math.floor(value / 3600);
    const minutes = Math.floor((value % 3600) / 60);
    const remaining = value % 60;
    if (hours > 0) return hours + "h " + minutes + "m";
    if (minutes > 0) return minutes + "m " + remaining + "s";
    return remaining + "s";
  }

  function formatBytes(bytes) {
    const value = Number(bytes) || 0;
    if (value >= 1024 * 1024) return (value / (1024 * 1024)).toFixed(1) + " MiB";
    if (value >= 1024) return Math.round(value / 1024) + " KiB";
    return value + " B";
  }

  function setConnection(mode, label) {
    const pill = $("connection-state");
    pill.dataset.state = mode;
    $("connection-label").textContent = label;
  }

  function showView(name) {
    views.forEach((view) => view.classList.toggle("active", view.id === "view-" + name));
    navItems.forEach((item) => item.classList.toggle("active", item.dataset.view === name));
  }

  function renderOutputs(outputs) {
    outputList.replaceChildren();
    outputs.forEach((output) => {
      const card = document.createElement("article");
      card.className = "output-card";

      const body = document.createElement("div");
      const name = document.createElement("strong");
      const pin = document.createElement("span");
      name.textContent = output.name || "Output " + output.id;
      pin.textContent = output.pin || "";
      body.append(name, pin);

      const label = document.createElement("label");
      label.className = "switch";
      const input = document.createElement("input");
      const thumb = document.createElement("span");
      input.type = "checkbox";
      input.checked = Boolean(output.on);
      input.addEventListener("change", () => updateOutput(output.id, input.checked));
      label.append(input, thumb);

      card.append(body, label);
      outputList.append(card);
    });
  }

  function renderState(data) {
    if (!data.outputs || !data.input) return;
    state.hasState = true;
    state.outputs = data.outputs;
    $("uptime-value").textContent = formatUptime(data.uptime);
    $("input-value").textContent = data.input.active ? "High" : "Low";
    if (data.heap) {
      $("heap-free-value").textContent =
        formatBytes(data.heap.free) + " / " + formatBytes(data.heap.total);
      $("heap-low-value").textContent =
        formatBytes(data.heap.minFree) + " min";
    }
    $("last-updated").textContent = "Updated " + new Date().toLocaleTimeString();
    renderOutputs(data.outputs);
  }

  function renderNetwork(data) {
    if (typeof data.dhcp !== "boolean") return;
    state.hasNetwork = true;
    $("network-dhcp").checked = data.dhcp;
    $("network-ip").value = data.ip || "";
    $("network-netmask").value = data.netmask || "";
    $("network-gateway").value = data.gateway || "";
    updateNetworkFields();

    if (data.current) {
      $("network-current").textContent =
        "Current " + data.current.ip + " / " + data.current.netmask +
        " gateway " + data.current.gateway +
        (data.current.link ? " - link up" : " - link down");
    }
  }

  function handleMessage(raw) {
    const data = JSON.parse(raw);
    renderState(data);
    renderNetwork(data);
  }

  async function loadInitialState() {
    if (!state.hasState) {
      const stateResponse = await api("/api/state");
      renderState(await stateResponse.json());
    }

    if (!state.hasNetwork) {
      const networkResponse = await api("/api/network");
      renderNetwork(await networkResponse.json());
    }
  }

  function connectWebSocket() {
    clearTimeout(state.reconnectTimer);
    if (state.ws && state.ws.readyState < WebSocket.CLOSING) return;

    const scheme = window.location.protocol === "https:" ? "wss://" : "ws://";
    const token = state.token ? "?token=" + encodeURIComponent(state.token) : "";
    const socket = new WebSocket(scheme + window.location.host + "/ws" + token);
    state.ws = socket;
    setConnection("connecting", "Connecting");
    let opened = false;

    socket.addEventListener("open", () => {
      opened = true;
      state.reconnectDelay = 250;
      setConnection("online", "Live");
      log("WebSocket connected");
    });
    socket.addEventListener("message", (event) => handleMessage(event.data));
    socket.addEventListener("close", () => {
      if (state.ws !== socket) return;
      state.ws = null;
      if (opened) {
        setConnection("offline", "Offline");
      } else {
        setConnection("connecting", "Connecting");
      }
      state.reconnectTimer = setTimeout(connectWebSocket, state.reconnectDelay);
      state.reconnectDelay = Math.min(state.reconnectDelay * 2, 2000);
    });
    socket.addEventListener("error", () => socket.close());
  }

  function startLive() {
    connectWebSocket();
    state.fallbackTimer = setTimeout(() => {
      if (!state.hasState || !state.hasNetwork) {
        loadInitialState().catch(() => setConnection("offline", "Offline"));
      }
    }, 5000);
  }

  async function updateOutput(id, on) {
    if (state.ws && state.ws.readyState === WebSocket.OPEN) {
      state.ws.send(JSON.stringify({ id: id, on: on }));
      log("Output " + id + " set " + (on ? "on" : "off"));
      return;
    }

    try {
      const response = await api("/api/output", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: id, on: on })
      });
      renderState(await response.json());
      log("Output " + id + " set " + (on ? "on" : "off"));
    } catch (error) {
      log("Output update failed");
      renderOutputs(state.outputs);
    }
  }

  function updateNetworkFields() {
    const disabled = $("network-dhcp").checked;
    ["network-ip", "network-netmask", "network-gateway"].forEach((id) => {
      $(id).disabled = disabled;
    });
  }

  async function saveNetwork(event) {
    event.preventDefault();
    $("network-message").textContent = "";

    const payload = {
      dhcp: $("network-dhcp").checked,
      ip: $("network-ip").value.trim(),
      netmask: $("network-netmask").value.trim(),
      gateway: $("network-gateway").value.trim()
    };

    try {
      const response = await api("/api/network", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      renderNetwork(await response.json());
      $("network-message").textContent = "Saved";
      log("Network configuration saved");
    } catch (error) {
      $("network-message").textContent = "Save failed";
    }
  }

  async function logout() {
    try {
      await api("/api/logout", { method: "POST" });
    } finally {
      sessionStorage.removeItem("st.authToken");
      window.location.replace("/login.html");
    }
  }

  navItems.forEach((item) => item.addEventListener("click", () => showView(item.dataset.view)));
  $("network-dhcp").addEventListener("change", updateNetworkFields);
  $("network-form").addEventListener("submit", saveNetwork);
  $("logout-button").addEventListener("click", logout);
  $("clear-log").addEventListener("click", () => eventLog.replaceChildren());

  startLive();
})();

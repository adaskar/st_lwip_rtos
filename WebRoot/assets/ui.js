(function () {
  "use strict";

  const css = `
:root{color-scheme:light;--bg:#eef2f5;--surface:#fff;--surface-alt:#f7f9fb;--ink:#18202a;--muted:#697586;--line:#d7dde5;--accent:#0f766e;--accent-strong:#0b5f59;--danger:#b42318;--ok:#17803d;--warning:#b76e00;font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
*{box-sizing:border-box}body{min-height:100vh;margin:0;background:var(--bg);color:var(--ink)}button,input{font:inherit}.auth-page{display:grid;place-items:center}.auth-shell{width:min(100% - 32px,440px)}.auth-panel,.panel,.metric-panel,.output-card,.empty-state{background:var(--surface);border:1px solid var(--line);border-radius:8px}.auth-panel{padding:28px}.brand-lockup{display:flex;align-items:center;gap:14px}.brand-mark{width:42px;height:42px;border-radius:8px;background:linear-gradient(135deg,#0f766e 0%,#247ba0 100%)}.eyebrow{margin:0 0 4px;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:0;text-transform:uppercase}h1,h2,h3{margin:0;letter-spacing:0}h1{font-size:26px;line-height:1.2}h2{font-size:24px}h3{font-size:17px}.form-stack{display:grid;gap:18px;margin-top:26px}.field{display:grid;gap:7px}.field span,.check-field span{color:var(--muted);font-size:13px;font-weight:700}.field input{width:100%;min-height:42px;border:1px solid var(--line);border-radius:6px;padding:9px 11px;color:var(--ink);background:#fff}.field input:disabled{color:#8993a1;background:#edf1f5}.form-actions,.header-actions,.panel-heading,.section-heading{display:flex;align-items:center;justify-content:space-between;gap:14px}.button{min-height:38px;border:1px solid var(--line);border-radius:6px;padding:8px 14px;color:var(--ink);background:var(--surface);cursor:pointer}.button:hover{border-color:#9aa6b5}.button.primary{border-color:var(--accent);color:#fff;background:var(--accent)}.button.primary:hover{background:var(--accent-strong)}.button.ghost{background:transparent}.button.compact{min-height:32px;padding:5px 10px}.form-message{min-height:20px;margin:0;color:var(--danger);font-size:13px}.app-shell{min-height:100vh;display:grid;grid-template-rows:auto 1fr}.app-header{display:flex;align-items:center;justify-content:space-between;gap:20px;min-height:76px;padding:14px 24px;border-bottom:1px solid var(--line);background:var(--surface)}.connection-pill{display:inline-flex;align-items:center;gap:8px;min-width:118px;min-height:34px;padding:6px 10px;border:1px solid var(--line);border-radius:999px;color:var(--muted);background:var(--surface-alt);font-size:13px;font-weight:700}.status-dot{width:9px;height:9px;border-radius:999px;background:var(--warning)}.connection-pill[data-state=online] .status-dot{background:var(--ok)}.connection-pill[data-state=offline] .status-dot{background:var(--danger)}#retry-live-button[hidden]{display:none}.workspace{display:grid;grid-template-columns:224px minmax(0,1fr);min-height:0}.sidebar{border-right:1px solid var(--line);background:#e5ebf0;padding:18px 12px}.section-nav{display:grid;gap:6px}.nav-item{width:100%;min-height:38px;border:1px solid transparent;border-radius:6px;padding:8px 10px;text-align:left;color:#334155;background:transparent;cursor:pointer}.nav-item:hover,.nav-item.active{border-color:var(--line);background:var(--surface)}.content{min-width:0;padding:24px}.view{display:none;max-width:980px}.view.active{display:grid;gap:18px}.muted{margin:0;color:var(--muted);font-size:13px}.summary-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.metric-panel{display:grid;gap:10px;min-height:108px;padding:18px}.metric-panel span{color:var(--muted);font-size:13px;font-weight:700}.metric-panel strong{font-size:30px;line-height:1.1}.panel{padding:18px}.outputs-list{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin-top:14px}.stats-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-top:14px}.stat-cell{display:grid;gap:4px;min-height:72px;border:1px solid var(--line);border-radius:6px;padding:10px;background:var(--surface-alt)}.stat-cell span{color:var(--muted);font-size:12px;font-weight:700}.stat-cell strong{font-size:20px;line-height:1.15}.output-card{display:flex;align-items:center;justify-content:space-between;gap:16px;min-height:92px;padding:16px}.output-card div{display:grid;gap:5px}.output-card strong{font-size:18px}.output-card span{color:var(--muted);font-size:13px}.switch{position:relative;display:inline-flex;width:54px;height:30px}.switch input{position:absolute;inset:0;opacity:0;cursor:pointer}.switch span{position:absolute;inset:0;border:1px solid #aeb7c2;border-radius:999px;background:#d8dee6}.switch span:before{content:"";position:absolute;top:3px;left:3px;width:22px;height:22px;border-radius:999px;background:#fff;box-shadow:0 1px 4px rgba(24,32,42,.24);transition:transform 160ms ease}.switch input:checked+span{border-color:var(--accent);background:var(--accent)}.switch input:checked+span:before{transform:translateX(24px)}.event-log{height:150px;margin-top:12px;overflow:auto;border:1px solid var(--line);border-radius:6px;padding:10px;background:#101828;color:#d4dbe6;font:12px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}.form-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}.check-field{display:inline-flex;align-items:center;gap:10px}.check-field input{width:18px;height:18px;accent-color:var(--accent)}.empty-state{padding:28px;color:var(--muted)}@media (max-width:800px){.app-header,.header-actions,.section-heading{align-items:flex-start;flex-direction:column}.workspace{grid-template-columns:1fr}.sidebar{border-right:0;border-bottom:1px solid var(--line)}.section-nav{grid-template-columns:repeat(2,minmax(0,1fr))}.content{padding:18px}.summary-grid,.outputs-list,.form-grid,.stats-grid{grid-template-columns:1fr}}
`;

  const style = document.createElement("style");
  style.textContent = css;
  document.head.appendChild(style);

  const $ = (id) => document.getElementById(id);

  function initLogin() {
    const form = $("login-form");
    if (!form) return false;

    const password = $("password");
    const message = $("login-message");

    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      message.textContent = "";

      try {
        const response = await fetch("/api/login", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ password: password.value })
        });

        if (!response.ok) {
          message.textContent = "Invalid password";
          password.select();
          return;
        }

        const data = await response.json();
        if (data.token) sessionStorage.setItem("st.authToken", data.token);
        window.location.assign("/");
      } catch (error) {
        message.textContent = "Device is not reachable";
      }
    });

    return true;
  }

  function initDashboard() {
    if (!$("outputs-list")) return false;

    const state = {
      token: sessionStorage.getItem("st.authToken") || "",
      ws: null,
      reconnectTimer: 0,
      fallbackTimer: 0,
      reconnectDelay: 250,
      retryRequired: false,
      outputs: [],
      hasState: false,
      hasNetwork: false
    };

    const views = Array.from(document.querySelectorAll(".view"));
    const navItems = Array.from(document.querySelectorAll(".nav-item"));
    const outputList = $("outputs-list");
    const eventLog = $("event-log");
    const retryButton = $("retry-live-button");

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
      $("connection-state").dataset.state = mode;
      $("connection-label").textContent = label;
      if (retryButton) retryButton.hidden = mode !== "offline";
    }

    function showView(name) {
      views.forEach((view) => view.classList.toggle("active", view.id === "view-" + name));
      navItems.forEach((item) => item.classList.toggle("active", item.dataset.view === name));
    }

    function renderOutputs(outputs) {
      outputList.replaceChildren();
      outputs.forEach((output) => {
        const card = document.createElement("article");
        const body = document.createElement("div");
        const name = document.createElement("strong");
        const pin = document.createElement("span");
        const label = document.createElement("label");
        const input = document.createElement("input");
        const thumb = document.createElement("span");

        card.className = "output-card";
        name.textContent = output.name || "Output " + output.id;
        pin.textContent = output.pin || "";
        body.append(name, pin);
        label.className = "switch";
        input.type = "checkbox";
        input.checked = Boolean(output.on);
        input.addEventListener("change", () => updateOutput(output.id, input.checked));
        label.append(input, thumb);
        card.append(body, label);
        outputList.append(card);
      });
    }

    function renderEthernet(ethernet) {
      if (!ethernet) return;

      const fields = [
        ["eth-rx-packets", ethernet.rxPackets],
        ["eth-rx-dropped", ethernet.rxDropped],
        ["eth-rx-alloc-errors", ethernet.rxAllocErrors],
        ["eth-tx-packets", ethernet.txPackets],
        ["eth-tx-errors", ethernet.txErrors],
        ["eth-tx-busy-drops", ethernet.txBusyDrops],
        ["eth-dma-errors", ethernet.dmaErrors],
        ["eth-link-up-count", ethernet.linkUpCount],
        ["eth-link-down-count", ethernet.linkDownCount]
      ];

      fields.forEach(([id, value]) => {
        const element = $(id);
        if (element) element.textContent = Number(value || 0).toLocaleString();
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
        $("heap-low-value").textContent = formatBytes(data.heap.minFree) + " min";
      }
      $("last-updated").textContent = "Updated " + new Date().toLocaleTimeString();
      renderOutputs(data.outputs);
      renderEthernet(data.ethernet);
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
      if (!state.hasState) renderState(await (await api("/api/state")).json());
      if (!state.hasNetwork) renderNetwork(await (await api("/api/network")).json());
    }

    function connectWebSocket(force) {
      clearTimeout(state.reconnectTimer);
      if (state.retryRequired && !force) return;
      if (state.ws && state.ws.readyState < WebSocket.CLOSING) return;

      const scheme = window.location.protocol === "https:" ? "wss://" : "ws://";
      const token = state.token ? "?token=" + encodeURIComponent(state.token) : "";
      const socket = new WebSocket(scheme + window.location.host + "/ws" + token);
      let opened = false;
      state.ws = socket;
      setConnection("connecting", "Connecting");

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
        if (!opened) {
          state.retryRequired = true;
          clearTimeout(state.reconnectTimer);
          setConnection("offline", "Client limit reached");
          log("Live connection refused; retry when another tab is closed");
          return;
        }
        setConnection("offline", "Offline");
        state.reconnectTimer = setTimeout(connectWebSocket, state.reconnectDelay);
        state.reconnectDelay = Math.min(state.reconnectDelay * 2, 2000);
      });
      socket.addEventListener("error", () => socket.close());
    }

    function startLive() {
      connectWebSocket(false);
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

      try {
        const response = await api("/api/network", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            dhcp: $("network-dhcp").checked,
            ip: $("network-ip").value.trim(),
            netmask: $("network-netmask").value.trim(),
            gateway: $("network-gateway").value.trim()
          })
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
    retryButton.addEventListener("click", () => {
      state.retryRequired = false;
      state.reconnectDelay = 250;
      connectWebSocket(true);
    });
    startLive();
    return true;
  }

  initLogin() || initDashboard();
})();

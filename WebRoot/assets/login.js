(function () {
  "use strict";

  const form = document.getElementById("login-form");
  const password = document.getElementById("password");
  const message = document.getElementById("login-message");

  async function signIn(event) {
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
  }

  form.addEventListener("submit", signIn);
})();

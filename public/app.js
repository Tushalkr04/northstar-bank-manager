const accountList = document.querySelector("#accounts-list");
const transactionList = document.querySelector("#transaction-list");
const accountDialog = document.querySelector("#account-dialog");
const transactionDialog = document.querySelector("#transaction-dialog");
const accountForm = document.querySelector("#account-form");
const transactionForm = document.querySelector("#transaction-form");
const toast = document.querySelector("#toast");

let accounts = [];
let transactions = [];
let showAllTransactions = false;
let depositsOnly = false;
let toastTimer;

function setConnectionStatus(connected) {
  const status = document.querySelector("#connection-status");
  status.classList.toggle("offline", !connected);
  status.querySelector("span").textContent = connected
    ? "Local & private"
    : "Offline — start run.bat";
}

const money = new Intl.NumberFormat("en-US", {
  style: "currency",
  currency: "USD",
});

function formatMoney(value) {
  return money.format(Number(value) || 0);
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, (character) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;",
  })[character]);
}

async function api(url, options = {}) {
  let response;
  try {
    response = await fetch(url, options);
  } catch {
    setConnectionStatus(false);
    throw new Error(
      "The bank service is not running. Double-click run.bat, then refresh this page.",
    );
  }
  setConnectionStatus(true);
  let result;
  try {
    result = await response.json();
  } catch {
    throw new Error("The bank service returned an invalid response. Restart it with run.bat.");
  }
  if (!response.ok) throw new Error(result.error || "Something went wrong.");
  return result;
}

function postForm(url, form) {
  return api(url, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
    body: new URLSearchParams(new FormData(form)),
  });
}

function notify(message) {
  toast.textContent = message;
  toast.classList.add("visible");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove("visible"), 3000);
}

function dateLabel(value) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat("en-US", {
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
  }).format(date);
}

function renderAccounts() {
  document.querySelector("#account-count").textContent =
    `${accounts.length} ${accounts.length === 1 ? "account" : "accounts"}`;
  document.querySelector("#account-nav-count").textContent = accounts.length;
  document.querySelector("#total-balance").textContent = formatMoney(
    accounts.reduce((total, account) => total + Number(account.balance), 0),
  );

  if (!accounts.length) {
    accountList.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">◈</div>
        <strong>Your first account starts here</strong>
        <p>Create an account to see your balances and keep track of activity.</p>
        <button type="button" data-open-account>Create your first account →</button>
      </div>`;
  } else {
    const colors = ["", "blue", "amber", "rose"];
    const symbols = ["◈", "▤", "◇", "○"];
    accountList.innerHTML = accounts.map((account, index) => `
      <div class="account-row">
        <div class="account-symbol ${colors[index % colors.length]}">${symbols[index % symbols.length]}</div>
        <div class="account-info"><strong>${escapeHtml(account.name)}</strong><span>Account #${String(account.id).padStart(5, "0")}</span></div>
        <strong class="account-balance">${formatMoney(account.balance)}</strong>
        <div class="account-actions">
          <button class="more-button" type="button" data-account-menu="${account.id}" aria-label="Account actions">···</button>
          <div class="account-menu" data-menu-for="${account.id}" hidden>
            <button type="button" data-transaction="deposit" data-account="${account.id}">Deposit</button>
            <button type="button" data-transaction="withdraw" data-account="${account.id}">Withdraw</button>
            <button type="button" data-transaction="transfer" data-account="${account.id}">Transfer</button>
          </div>
        </div>
      </div>`).join("");
  }
  updateAccountOptions();
}

function renderTransactions() {
  const filtered = depositsOnly
    ? transactions.filter((transaction) => transaction.type === "deposit" || transaction.type === "transfer_in")
    : transactions;
  const recent = showAllTransactions ? filtered : filtered.slice(0, 5);
  if (!recent.length) {
    transactionList.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">↔</div>
        <strong>No activity just yet</strong>
        <p>Your deposits, withdrawals, and transfers will show up here.</p>
      </div>`;
  } else {
    const accountNames = new Map(accounts.map((account) => [account.id, account.name]));
    transactionList.innerHTML = recent.map((transaction) => {
      const outgoing = transaction.type === "withdraw" || transaction.type === "transfer_out";
      const label = transaction.type === "transfer_in" ? "Transfer received"
        : transaction.type === "transfer_out" ? "Transfer sent"
          : transaction.type === "withdraw" ? "Withdrawal" : "Deposit";
      const icon = transaction.type === "withdraw" || transaction.type === "transfer_out" ? "↗"
        : transaction.type === "transfer_in" ? "↙" : "↓";
      const accountName = accountNames.get(transaction.accountId) || "Account";
      const sign = outgoing ? "−" : "+";
      return `
        <div class="transaction-row">
          <div class="transaction-icon ${escapeHtml(transaction.type)}">${icon}</div>
          <div class="transaction-info">
            <strong>${escapeHtml(transaction.description || label)}</strong>
            <span>${escapeHtml(accountName)} · ${dateLabel(transaction.timestamp)}</span>
          </div>
          <strong class="transaction-amount ${outgoing ? "negative" : ""}">${sign}${formatMoney(transaction.amount)}</strong>
        </div>`;
    }).join("");
  }
  document.querySelector("#view-all-button").hidden = filtered.length <= 5;
  document.querySelector("#view-all-button").innerHTML =
    showAllTransactions ? 'Show recent activity <span>↑</span>' : 'View all activity <span>→</span>';
}

function renderStats() {
  const now = Date.now();
  const last30Days = transactions.filter((transaction) =>
    now - new Date(transaction.timestamp).getTime() <= 30 * 24 * 60 * 60 * 1000,
  );
  const incoming = last30Days
    .filter((transaction) => transaction.type === "deposit")
    .reduce((sum, transaction) => sum + Number(transaction.amount), 0);
  const outgoing = last30Days
    .filter((transaction) => transaction.type === "withdraw")
    .reduce((sum, transaction) => sum + Number(transaction.amount), 0);
  document.querySelector("#money-in").textContent = formatMoney(incoming);
  document.querySelector("#money-out").textContent = formatMoney(outgoing);
}

function updateAccountOptions() {
  const source = document.querySelector("#source-account");
  const recipient = document.querySelector("#recipient-account");
  const currentSource = source.value;
  source.innerHTML = accounts.map((account) =>
    `<option value="${account.id}">${escapeHtml(account.name)} · ${formatMoney(account.balance)}</option>`,
  ).join("");
  if (accounts.some((account) => String(account.id) === currentSource)) source.value = currentSource;
  refreshRecipients();
}

function refreshRecipients() {
  const sourceId = document.querySelector("#source-account").value;
  const recipient = document.querySelector("#recipient-account");
  recipient.innerHTML = accounts.filter((account) => String(account.id) !== sourceId)
    .map((account) => `<option value="${account.id}">${escapeHtml(account.name)} · ${formatMoney(account.balance)}</option>`)
    .join("");
}

async function refresh() {
  const [freshAccounts, freshTransactions] = await Promise.all([
    api("/api/accounts"),
    api("/api/transactions"),
  ]);
  accounts = freshAccounts;
  transactions = freshTransactions;
  renderAccounts();
  renderTransactions();
  renderStats();
}

function openAccountDialog() {
  document.querySelector("#account-error").textContent = "";
  accountForm.reset();
  accountDialog.showModal();
  document.querySelector("#account-name").focus();
}

function openTransactionDialog(type = "deposit", accountId = "") {
  if (!accounts.length) {
    notify("Create an account first to record a transaction.");
    openAccountDialog();
    return;
  }
  if (type === "transfer" && accounts.length < 2) {
    notify("Create another account before making a transfer.");
    return;
  }
  transactionForm.reset();
  document.querySelector("#transaction-error").textContent = "";
  document.querySelector("#transaction-type").value = type;
  document.querySelector("#transaction-title").textContent =
    type === "transfer" ? "Transfer money"
      : type === "withdraw" ? "Record withdrawal" : "Record deposit";
  updateAccountOptions();
  if (accountId) document.querySelector("#source-account").value = accountId;
  refreshRecipients();
  document.querySelector("#recipient-wrap").hidden = type !== "transfer";
  transactionDialog.showModal();
}

document.querySelector("#new-account-button").addEventListener("click", openAccountDialog);
document.querySelector("#add-account-link").addEventListener("click", openAccountDialog);
accountList.addEventListener("click", (event) => {
  const createButton = event.target.closest("[data-open-account]");
  if (createButton) openAccountDialog();
  const menuButton = event.target.closest("[data-account-menu]");
  if (menuButton) {
    const menu = accountList.querySelector(`[data-menu-for="${menuButton.dataset.accountMenu}"]`);
    const shouldOpen = menu.hidden;
    accountList.querySelectorAll(".account-menu").forEach((item) => { item.hidden = true; });
    menu.hidden = !shouldOpen;
  }
  const transactionButton = event.target.closest("[data-transaction]");
  if (transactionButton) {
    openTransactionDialog(transactionButton.dataset.transaction, transactionButton.dataset.account);
    transactionButton.closest(".account-menu").hidden = true;
  }
});

document.querySelector("#transaction-type").addEventListener("change", (event) => {
  const isTransfer = event.target.value === "transfer";
  document.querySelector("#recipient-wrap").hidden = !isTransfer;
  document.querySelector("#transaction-title").textContent =
    isTransfer ? "Transfer money" : event.target.value === "withdraw" ? "Record withdrawal" : "Record deposit";
});
document.querySelector("#source-account").addEventListener("change", refreshRecipients);

accountForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const submitButton = accountForm.querySelector('[type="submit"]');
  submitButton.disabled = true;
  try {
    await postForm("/api/accounts", accountForm);
    accountDialog.close();
    await refresh();
    notify("Your account is ready.");
  } catch (error) {
    document.querySelector("#account-error").textContent = error.message;
  } finally {
    submitButton.disabled = false;
  }
});

transactionForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const submitButton = transactionForm.querySelector('[type="submit"]');
  submitButton.disabled = true;
  try {
    await postForm("/api/transactions", transactionForm);
    transactionDialog.close();
    await refresh();
    notify("Transaction saved successfully.");
  } catch (error) {
    document.querySelector("#transaction-error").textContent = error.message;
  } finally {
    submitButton.disabled = false;
  }
});

document.querySelectorAll("[data-close]").forEach((button) => {
  button.addEventListener("click", () => button.closest("dialog").close());
});
document.querySelector("#view-all-button").addEventListener("click", () => {
  showAllTransactions = !showAllTransactions;
  renderTransactions();
});
document.querySelector("#activity-filter").addEventListener("click", () => {
  const filter = document.querySelector("#activity-filter span");
  depositsOnly = !depositsOnly;
  filter.textContent = depositsOnly ? "Deposits" : "All";
  renderTransactions();
});
document.querySelector("#refresh-button").addEventListener("click", async (event) => {
  event.currentTarget.classList.add("spinning");
  try {
    await refresh();
    notify("Your dashboard is up to date.");
  } catch (error) {
    notify(error.message);
  } finally {
    event.currentTarget.classList.remove("spinning");
  }
});
document.querySelector("#mobile-menu").addEventListener("click", () => {
  document.querySelector("#sidebar").classList.toggle("open");
});
document.querySelectorAll(".nav-item").forEach((link) => {
  link.addEventListener("click", () => {
    document.querySelectorAll(".nav-item").forEach((item) => item.classList.remove("active"));
    link.classList.add("active");
    document.querySelector("#sidebar").classList.remove("open");
  });
});
document.addEventListener("click", (event) => {
  if (!event.target.closest(".account-actions")) {
    accountList.querySelectorAll(".account-menu").forEach((menu) => { menu.hidden = true; });
  }
});

const today = new Date();
document.querySelector("#today-label").textContent = new Intl.DateTimeFormat("en-US", {
  weekday: "long", month: "long", day: "numeric",
}).format(today).toUpperCase();
refresh().catch((error) => {
  notify(`Could not connect to the banking service: ${error.message}`);
});

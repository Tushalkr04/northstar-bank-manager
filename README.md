# Northstar Bank Manager

A local bank-account management demo with a C++17 backend and a responsive browser interface. It runs on your own computer, uses no third-party backend libraries, and stores its account and transaction records locally.

> This is a learning/demo application, not real banking software. It does not connect to a financial institution and is not designed to safeguard real funds or sensitive financial data.

## Features

- Create accounts with an optional opening balance.
- Deposit, withdraw, and transfer money between accounts.
- Validate transactions and prevent overdrafts.
- View account balances, recent activity, and 30-day money-in/money-out totals.
- Preserve data in `data/bank-data.txt` across restarts.
- Use the interface on desktop or mobile.

## Sample accounts

The project includes fictional example accounts so the dashboard is populated immediately:

- Everyday Checking — $2,285.75
- Rainy Day Savings — $8,200.00
- Travel Fund — $2,100.00

The sample data also includes example deposits, a withdrawal, and a transfer. These balances are for demonstration only; they are not real money.

## Requirements

- CMake 3.16 or newer.
- A C++17 compiler (Visual Studio 2019+, MinGW-w64, GCC, or Clang).

## Build and run on Windows

**Easiest:** double-click `run.bat`. It builds the app with CMake or `g++`, starts the C++ backend, waits for it to respond, and opens the dashboard. Keep the minimized **Northstar Bank Manager** window open while you use the app.

On Windows, the **Northstar Bank Manager** shortcut on your Desktop opens the app the same way.

If you prefer to build manually, open PowerShell in the project folder and run:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\bank_manager.exe
```

For single-configuration generators, such as MinGW Makefiles, start `.\build\bank_manager.exe` instead.

Open **http://127.0.0.1:8080** in your browser. Keep the terminal open while you use the app; press **Ctrl+C** to stop the server. Your data is saved in `data\bank-data.txt` in the project folder, even when the executable is started from the build directory.

If the page reports that the service is offline, start `run.bat` and refresh the browser. The browser page alone does not run the backend.

The server listens only on `127.0.0.1` and is intended for local use. If port 8080 is already in use, close the other application and restart this app.

## Build on macOS or Linux

```sh
cmake -S . -B build
cmake --build build
./build/bank_manager
```

Then open **http://127.0.0.1:8080**.

## API

- `GET /api/accounts`
- `POST /api/accounts` — form fields: `name`, `openingBalance`
- `GET /api/transactions`
- `GET /api/transactions?accountId=1`
- `POST /api/transactions` — form fields: `type` (`deposit`, `withdraw`, or `transfer`), `accountId`, `amount`, optional `recipientId`, optional `note`

The frontend is served by the C++ application, so no separate web server or JavaScript package installation is required.

# ParentClient

A Windows parental control client overlay application built with C++20 and the Win32 API. Connects to a backend server over WebSocket to receive remote commands (lock, unlock, shutdown, restart) and display notifications. Runs as a single executable with no console window.

## Features

- **WebSocket Communication** – WinHTTP-based WebSocket client with automatic reconnect (exponential backoff: 2s → 60s) and 30s keep-alive ping.
- **Lock Overlay** – Fullscreen topmost window that blocks keyboard and mouse input. Supports PIN-based local unlock.
- **System Tray** – Minimizes to tray icon with right-click status menu. Displays server-pushed notification balloons. Exit requires PIN.
- **Remote Commands** – `LOCK`, `UNLOCK`, `SHUTDOWN`, `RESTART` with configurable `delaySeconds`. When a delay is specified, a message box notifies the user (e.g. *"You will be LOCKed after 300s"*) and the action executes silently after the countdown.
- **Secret Key Auth** – Optional `X-Secret-Key` request header sent on every WebSocket upgrade, configured via `SECRET_KEY` in `config.ini`.
- **Stable Device ID** – Derived from C: drive volume serial number and computer name.
- **Unicode Throughout** – All strings use `WCHAR` / `L""` literals / `W`-suffix Win32 functions.
- **Logging** – Timestamped logs written to `log.txt` in the same directory as the executable. Logs connection events, commands, errors, and application lifecycle.

## Project Structure

```
├── CMakeLists.txt
├── CMakePresets.json
├── install.bat                # Interactive installer (downloads exe, writes config.ini, registers task)
├── uninstall.bat              # Removes scheduled task and install directory
├── include/
│   └── json.hpp              # nlohmann/json (single-header)
└── src/
    ├── main.cpp               # Entry point, message routing
    ├── config.h / config.cpp  # Reads config.ini next to the executable
    ├── device_id.h / .cpp     # Hardware-based stable device ID
    ├── websocket.h / .cpp     # WinHTTP WebSocket + reconnect
    ├── command_handler.h / .cpp # Processes server commands
    ├── overlay.h / .cpp       # Fullscreen lock overlay
    └── tray.h / .cpp          # System tray icon and menu
```

## Requirements

- Windows 10 or later
- Visual Studio 2022+ or MSVC v14.30+ with C++20 support
- CMake 3.10+
- Ninja build system (bundled with Visual Studio)

## Build

### Visual Studio (Open Folder / CMake)

Open the project folder in Visual Studio. It will detect `CMakePresets.json` and configure automatically. Select the **x64-debug** or **x64-release** preset, then build.

### Command Line

```powershell
cmake --preset x64-release
cmake --build out/build/x64-release
```

The output executable is `out/build/x64-release/ParentClient.exe`.

## Installation

Pre-built scripts are provided for deploying to a managed machine.

### `install.bat` — must be run as Administrator

1. If `ParentClient.exe` is **not** found next to the script, it is downloaded automatically from the URL defined in the `DOWNLOAD_URL` variable at the top of the script — edit this before distributing.
2. The script prompts for all server settings interactively:

   | Prompt | Config key | Default |
   |---|---|---|
   | Server scheme | `SERVER_SCHEME` | `wss` |
   | Server host | `SERVER_HOST` | *(required)* |
   | Server port | `SERVER_PORT` | `443` |
   | Secret key | `SECRET_KEY` | *(empty)* |
   | Unlock PIN | `UNLOCK_PIN` | `1234` |

3. Copies `ParentClient.exe` to `%ProgramFiles%\ParentClient\`.
4. Writes `config.ini` to the install directory from the collected values.
5. Registers a Windows Scheduled Task named **ParentClient** that launches the executable at every user logon with highest available privileges.

### `uninstall.bat` — must be run as Administrator

1. Kills any running `ParentClient.exe` instance.
2. Deletes the **ParentClient** scheduled task.
3. Removes the `%ProgramFiles%\ParentClient\` directory and all its contents.

### Linked Libraries

All are Windows system libraries — no external dependencies beyond `nlohmann/json` (header-only, included):

| Library       | Purpose                          |
|---------------|----------------------------------|
| winhttp.lib   | WebSocket / HTTP communication   |
| ws2_32.lib    | Winsock (IP address resolution)  |
| shell32.lib   | System tray notifications        |
| user32.lib    | Window management, input hooks   |
| gdi32.lib     | Font / brush rendering           |

## Dependencies

### nlohmann/json

| | |
|---|---|
| **Repository** | [github.com/nlohmann/json](https://github.com/nlohmann/json) |
| **License** | MIT |
| **Distribution** | Single-header — bundled at `include/json.hpp` |

[nlohmann/json](https://github.com/nlohmann/json) (also known as *JSON for Modern C++*) is a header-only C++11 library that provides intuitive JSON parsing and serialization using standard C++ types. No linking or build step is required — dropping `json.hpp` into the include path is sufficient.

In this project it is used to:

- **Serialize** outgoing messages (`register`, `status`, `event`, `ping`) into JSON strings before sending over the WebSocket.
- **Parse** all inbound WebSocket frames (`registered`, `command`, `message`) into typed fields.

Because the library is header-only and committed directly to the repository, there are no package-manager or internet dependencies at build time.

## Configuration

Place a `config.ini` file in the same directory as the executable:

```ini
SERVER_SCHEME=wss
SERVER_HOST=yourdomain.com
SERVER_PORT=443
SECRET_KEY=your-secret-key
UNLOCK_PIN=1234
```

If the file is missing or a key is absent, these defaults apply:

| Key             | Default     |
|-----------------|-------------|
| SERVER_SCHEME   | `wss`       |
| SERVER_HOST     | `localhost` |
| SERVER_PORT     | `443`       |
| SECRET_KEY      | *(empty)*   |
| UNLOCK_PIN      | `1234`      |

## WebSocket Protocol

Endpoint: `{SCHEME}://{HOST}:{PORT}/ws/device`

If `SECRET_KEY` is set in `config.ini`, the upgrade request includes the header:

```
X-Secret-Key: <value>
```

### Client → Server

| Type       | Fields                                    | When                     |
|------------|-------------------------------------------|--------------------------|
| `register` | `deviceId`, `deviceName`, `ipAddress`     | Immediately after connect |
| `status`   | `lockStatus` (`LOCKED` / `UNLOCKED`)      | After lock state change   |
| `event`    | `eventType`, `description` (optional)     | On significant events     |

**Event types:** `POWER_ON`, `SHUTDOWN`, `LOCK`, `UNLOCK`, `RESTART`, `CONNECT`, `DISCONNECT`

### Server → Client

| Type         | Fields                        | Action                              |
|--------------|-------------------------------|-------------------------------------|
| `registered` | `status`                      | Acknowledge registration            |
| `command`    | `command`, `delaySeconds`     | Execute command (with optional delay) |
| `message`    | `content`                     | Show tray balloon notification      |

**Command types:** `LOCK`, `UNLOCK`, `SHUTDOWN`, `RESTART`

When `delaySeconds > 0`, a message box immediately notifies the user (e.g. *"You will be LOCKed after 300s"*). The command executes silently in the background after the delay. An `UNLOCK` command received during the delay cancels the pending action.

## Logging

All application events are logged to `log.txt` in the same directory as the executable:

- **Application lifecycle** – Startup and shutdown timestamps
- **Configuration** – Server URL, device ID, config file loading
- **WebSocket** – Connection, disconnection, reconnect attempts, send/receive
- **Commands** – Received commands with parameters, execution results
- **Overlay** – Show/hide events
- **Errors** – Connection failures, invalid messages, PIN errors

Each log entry includes:
```
[2026-02-17 18:45:32.123] [INFO] Application starting
[2026-02-17 18:45:32.456] [WARN] Config file not found, using defaults
[2026-02-17 18:45:33.789] [ERROR] WinHttpConnect failed
```

Log levels: `INFO`, `WARN`, `ERROR`. Logs are flushed after each write to ensure persistence even if the application crashes.

## License

See [LICENSE](LICENSE) if present, otherwise consult the repository owner.

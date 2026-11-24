# AI Usage Desk Buddy

Author: **KwoYeung**

An AI Passport desk companion for Codex and DSH. It shows usage, DeepSeek balance, and which assistant is working, and plays a quiet alert when a task finishes. The screen follows the active assistant and rotates when both are busy.

## Features

- Codex usage windows, 7-day/30-day token totals, and active task count
- DeepSeek balance, 7/30-day tokens, today's tokens and request count
- DSH working/ready state and a six-second completion notice with a chime
- Automatic page switching: Codex only, DSH only, or a five-second rotation when both work
- Pixel companion, auto sleep, brightness, sound and sleep settings

## Requirements

- AI Passport device running this repository's firmware
- Python 3.10+ and Codex CLI signed in on the computer
- Microsoft Edge WebView2 Runtime on Windows (normally included with Windows 10/11)

**No Windows EXE is required.** The console runs from source; this repository no longer ships an EXE build script or prebuilt binary.

## Quick start

1. Build and flash the `main` firmware (below).
2. Start the console:
   - Windows: double-click `start-console.vbs` (use `start-console.cmd` for error output).
   - macOS: run `bash start-console.command`.
3. Choose Bluetooth in the console, search for the device, and enter the six-digit pairing code shown on the device when prompted. The device is connected when its top-left shows “已连接BLE”.
4. Open **Status → DeepSeek** in the console, save and verify your DeepSeek API key to show the balance.
5. For DSH working/completion alerts, install `dsh-plugin/` in DSH (Plugins → Add plugin). Codex features work without it.

For LAN, enable Wi-Fi on the device and complete provisioning, then choose **LAN / Wi-Fi** in the console and confirm the six-digit code.

## Controls

| Action | Function |
| --- | --- |
| Up | Switch between the home and DeepSeek pages |
| Hold OK | Open the settings menu |
| OK | Confirm / enter in menus |
| Down | Move in menus |

Auto page switching: Codex-only work returns home, DSH-only work opens the DeepSeek page, both working rotates every five seconds, and idle leaves the current page. Menus, approvals, pairing codes and settings pause automatic switching.

## Build and flash

Requires ESP-IDF 6.1:

```sh
idf.py set-target esp32c3
idf.py build
idf.py -p <PORT> flash
```

A merged image for flashing from `0x0`:

```sh
idf.py merge-bin
```

## Layout

- `main/` — device firmware
- `tools/` — local console and Codex bridge
- `tests/` — host tests
- `dsh-plugin/` — DSH activity plugin
- `docs/CONSOLE.md` — console guide

## Privacy

- The DeepSeek API key stays in the local console config directory with mode 0600; it is never sent to the device or written to logs.
- Token totals are read only from local DSH session usage counters; no prompt or answer text is read.
- The DSH plugin publishes only working state and a completion counter.
- The device receives only usage, balance, state counters and timestamps.

## License

Original code in this project is MIT-licensed, copyright **KwoYeung**. Third-party components, fonts and protocol references retain their own terms in [NOTICE](NOTICE).

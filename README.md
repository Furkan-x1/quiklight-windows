# Quiklight Windows

Windows ambient LED controller for the Quiklight / DX-LIGHT compatible HID controller.

## Build

Double-click `build-windows.bat`.

The batch file removes the CMake build cache first, configures an x64 Release build with Visual Studio 2022, then builds the executable.

## HID diagnostics

The target controller is `VID_1A86 / PID_FE07`. The Windows HID backend enumerates all matching HID interfaces, including `MI_00`, and opens the device with write access first (then read/write as a fallback). This avoids failures on Windows HID interfaces that reject read access.

If the device cannot be opened, the application reports the Windows `GetLastError()` code in the error message.

The device should appear in Device Manager as a HID device similar to:

`HID\\VID_1A86&PID_FE07&MI_00`

## CLI diagnostics

The executable accepts:

- `--list-devices`
- `--list-monitors`
- `--help`

## Windows startup

Click **Start with Windows** in the GUI once. This creates or updates `Quiklight Windows.lnk` in your user Startup folder and launches the app with `--startup`. The shortcut points to the current executable beside the settings file.

At sign-in, `--startup` starts Quiklight hidden (the main window stays closed) with a visible system-tray icon and automatically activates the saved lighting mode, equivalent to pressing **Start**. Left-click the tray icon to open settings; right-click to Start/Stop, Open, or Exit. Click **Start with Windows** again if an older shortcut already exists so its `--startup` argument is updated.

The optional `dist/install-startup-task.ps1` Scheduled Task installer also passes `--startup`.

Use only one logon startup mechanism at a time. If you previously created a Task Scheduler task manually, remove it or update it with `dist/install-startup-task.ps1` before also enabling the in-app Startup shortcut; otherwise Windows may launch the app twice.

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


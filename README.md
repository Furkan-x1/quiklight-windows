# Quiklight Windows

A native Windows port of the Quiklight ambient LED application for compatible DX-LIGHT/Quiklight HID controllers.

## Features

- Windows Desktop Duplication screen capture
- USB HID LED output
- Capture mode with configurable edge sampling depth
- Native Win32 GUI (no external GUI framework)
- Manual RGB controls
- Built-in lighting effects:
  - Capture (screen)
  - Static / Manual color
  - Rainbow
  - Color cycle
  - Breathing
  - Wave
  - Strobe
  - Fire
  - Ocean
  - Forest
  - Aurora
  - Twinkle
  - Police
  - Warm white
  - Cool white
  - White
- Brightness, smoothing, FPS and effect speed controls
- Capture color controls: saturation, value, gamma, minimum saturation and hue shift
- LED direction/order mapping controls
- System tray integration
- Optional Windows startup shortcut
- Settings saved to `quiklight.ini` next to the executable

## Capture edge depth

`Edge capture depth` controls how far into the image the ambient color sampler looks.

- `1-2%`: only the pixels very close to the monitor border influence the LEDs.
- `3-5%`: a wider strip of the image contributes; a good starting point for most content.
- `10%+`: colors farther from the border have a strong influence.

For a typical monitor, start around **2-5%** and increase it if important image colors are not reaching the LEDs.

## Color controls

- **Saturation**: multiplies captured color saturation. Higher values make colors stronger.
- **Value**: multiplies captured brightness/value before output.
- **Gamma**: changes the brightness curve of captured colors. Lower values generally brighten midtones.
- **Min saturation**: raises weak/gray colors toward the selected minimum saturation.
- **Hue shift**: rotates captured colors around the color wheel.
- **Smoothing**: blends the previous frame with the new frame. Higher values reduce flicker but increase response delay.

## Manual/effect controls

The manual RGB sliders are used by Static, Breathing, Wave, Strobe and Twinkle. Rainbow, Fire, Ocean, Forest, Aurora, Police and the white presets use their own colors.

`Effect speed` controls animation speed. Capture mode uses the separate `Capture FPS` setting instead.

## Build

Install Visual Studio 2022 / Build Tools with **Desktop development with C++** and the Windows SDK.

Then double-click:

```text
build-windows.bat
```

The executable is produced as:

```text
build\\Release\\QuiklightWindows.exe
```

## Diagnostics

```text
QuiklightWindows.exe --list-devices
QuiklightWindows.exe --list-monitors
```

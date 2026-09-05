# uBTAudioTray <img src="res/bt_icon_on.png" alt="uBTAudioTray Icon" width="60"/>

Mostly LLM generated C rewrite of QuickBTTray.

A lightweight Windows tray application for quick management of Bluetooth audio devices.

<img src="res/Screenshot_uBTAudioTray.png" alt="uBTAudioTray Icon"/>

I created this app because AirPods don’t support automatic switching between an iPhone and a PC. While they stay paired to both, manually connecting through Windows 11 Bluetooth menus several times a day quickly becomes tedious (which I do at work). This app minimizes that friction: a simple left-click on the tray icon instantly toggles the connection of the devices you’ve selected in the app's menu. This app should hopefully work for anyone who has a Bluetooth headset without multipoint support.

## Features
- Lives in the system tray with a modern, theme-aware menu
- One-click on the tray icon to connect/disconnect for selected Bluetooth audio devices
- Manual connect/disconnect buttons for each device
- Visual status icons for device connection state
- Tray icon animates during connect/disconnect
- Supports both API and UI (Windows Settings) connection methods
- "Start with Windows" option (adds/removes registry entry)
- Settings menu for connection method and app options
- "Open Bluetooth & Devices Settings" shortcut
- Dark/light mode support

## How It Works
- The app scans for Bluetooth audio devices and lists them in the tray menu.
- Select devices with checkboxes for batch connect/disconnect.
- Single left-click the tray icon to connect/disconnect selected devices.
- Right-click opens the full menu with device controls and settings.
- The tray icon blinks when connecting/disconnecting.
- "Start with Windows" stores the app's path in the registry at:
	`HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Run`

## Usage Instructions
1. **Run uBTAudioTray.exe** (standalone, no install required)
2. **Select your Bluetooth audio devices** in the right-click tray menu
3. **Click the tray icon** to connect/disconnect selected devices
4. **Use the gear/settings menu** for connection methods and notifaction settings

## Requirements
- Windows 10/11


## Building
You need the Win10 or Win11 SDK installed.
- To build a portable, standalone EXE:
	```
	build.bat
	```

## Notes
- The app does not collect or transmit any user data.
- If you move the EXE, re-enable "Start with Windows" to update the registry path.
- For best results with UI automation, avoid interacting with the PC until the connection completes.

## License
MIT License

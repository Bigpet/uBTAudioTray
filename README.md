# uBTAudioTray <img src="res/bt_icon_on.png" alt="uBTAudioTray Icon" width="40"/>

A lightweight Windows tray application for quick management of Bluetooth audio devices. Especially for one-click connect/disconnect.

Mostly LLM generated C rewrite of Joel-Hjertens' [QuickBTTray](https://github.com/Joel-Hjerten/QuickBTTray). This README also derives from their version.

<img src="res/Screenshot_uBTAudioTray.png?v=2" alt="uBTAudioTray Icon"/>

For quick connecting/disconnecting of Bluetooth audio devices.
Useful when you have Bluetooth device without working Multipoint connection switching. As well in case of a Windows application that doesn't release the audio stream correctly. Then it can be pain to go through the same Menu multiple times a day to disconnect/reconnect the Bluetooth Audio device.
This app minimizes that friction: a simple left-click on the tray icon instantly toggles the connection of the devices you’ve selected in the app's menu.

## Features
- One-click on the tray icon to connect/disconnect for selected Bluetooth audio devices
- Lives in the system tray
- Dark/light mode support
- Manual connect/disconnect buttons for each device
- Visual status icons for device connection state
- Tray icon animates during connect/disconnect
- Supports KS, API and UI (Windows Settings) connection methods
- "Start with Windows" option (adds/removes registry entry)
- Settings menu for connection method and app options
- "Open Bluetooth & Devices Settings" shortcut

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

- **To build default standalone x64 EXE (requires Visual Studio)**:
	```cmd
	build.bat
	```

Otherwise the CMakeLists.txt can be used to compile with other compilers.

## Notes
- The app does not collect or transmit any user data.
- If you move the EXE, re-enable "Start with Windows" to update the registry path.
- For best results with UI automation, avoid interacting with the PC until the connection completes.

## Alternatives

If this is almost what you want but not quite, you can look into:

Similar Software includes: 
* QuickBTTray (software this was forked from)
* BTAudioSysTrayTool 
* ToothTray (pioneered the KS API approach afaik)
* ToothTrayCli
* bqc (BT Quick Connect)
* MagicPods (more aipods/Sony/Samsung features)

## License
MIT License

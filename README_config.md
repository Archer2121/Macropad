Macropad Config GUI

Simple Tkinter app to assign macros to the two buttons on the Macropad firmware and manage a small macro library.

Install requirements:

```powershell
pip install requests
```

Run the GUI:

```powershell
python Macropad/macropad_config.py
```

Usage:

- Enter your device host or IP (e.g. `macropad.local` or `192.168.4.1`).
- Use the Macros tab to create, edit, and save macros.
- Use the Buttons tab to select which macro is assigned to Button 1 and Button 2.
- Click `Assign & Send` to send the current assignments to the device using the firmware `/set` HTTP endpoint.

Notes:

- The firmware exposes `/set?m1=...&m2=...` to save macros into device preferences; this GUI uses that endpoint.
- Macros are saved locally to `Macropad/macros.json`.
- If your device isn't reachable, ensure it's on the same network and the hostname/IP is correct.

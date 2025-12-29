import requests
import tkinter as tk
from tkinter import messagebox
import serial.tools.list_ports
import subprocess
import tempfile
import os

# ===== CONFIG =====
DEVICE_URL = "http://macropad.local"
FW_VERSION_URL = "https://raw.githubusercontent.com/Archer2121/Macropad/main/version.txt"
FW_BIN_URL     = "https://raw.githubusercontent.com/Archer2121/Macropad/main/main/build/esp32.esp32.lolin_s3/main.ino.bin"
BAUDRATE = "460800"

# ===== SERIAL =====
def list_ports():
    ports = serial.tools.list_ports.comports()
    port_menu["menu"].delete(0, "end")
    for p in ports:
        port_menu["menu"].add_command(
            label=p.device,
            command=lambda v=p.device: port_var.set(v)
        )
    if ports:
        port_var.set(ports[0].device)

# ===== OTA UPDATE =====
def ota_update():
    try:
        local = requests.get(DEVICE_URL + "/version", timeout=2).text.strip()
        remote = requests.get(FW_VERSION_URL, timeout=3).text.strip()

        if local == remote:
            messagebox.showinfo("Up to date", f"Firmware {local} is current")
            return

        if not messagebox.askyesno(
            "Update Available",
            f"Installed: {local}\nLatest: {remote}\n\nInstall update?"
        ):
            return

        fw = requests.get(FW_BIN_URL).content
        path = os.path.join(tempfile.gettempdir(), "macropad.bin")
        open(path, "wb").write(fw)

        requests.post(
            DEVICE_URL + "/update",
            files={"firmware": open(path, "rb")},
            timeout=10
        )

        messagebox.showinfo("Success", "OTA update complete\nDevice rebooting")

    except Exception as e:
        messagebox.showerror("OTA Error", str(e))

# ===== SERIAL UPDATE =====
def serial_update():
    port = port_var.get()
    if not port:
        messagebox.showerror("Error", "No COM port selected")
        return

    try:
        fw = requests.get(FW_BIN_URL).content
        path = os.path.join(tempfile.gettempdir(), "macropad.bin")
        open(path, "wb").write(fw)

        cmd = [
            "esptool.py",
            "--chip", "esp32s3",
            "--port", port,
            "--baud", BAUDRATE,
            "write_flash",
            "-z",
            "0x0",
            path
        ]

        subprocess.run(cmd, check=True)

        messagebox.showinfo("Success", "Serial flash complete")

    except Exception as e:
        messagebox.showerror("Serial Error", str(e))

# ===== UI =====
root = tk.Tk()
root.title("ESP32 Macropad Updater")
root.geometry("360x260")

tk.Label(root, text="ESP32 Macropad Firmware", font=("Arial", 12)).pack(pady=10)

tk.Button(root, text="OTA Update (WiFi)", command=ota_update).pack(pady=5)

tk.Label(root, text="USB Serial Update").pack(pady=5)

port_var = tk.StringVar()
port_menu = tk.OptionMenu(root, port_var, "")
port_menu.pack()

tk.Button(root, text="Refresh COM Ports", command=list_ports).pack(pady=5)
tk.Button(root, text="Flash via COM Port", command=serial_update).pack(pady=5)

list_ports()
root.mainloop()

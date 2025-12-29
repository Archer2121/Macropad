import requests
import tkinter as tk
from tkinter import messagebox, ttk
import serial.tools.list_ports
import subprocess
import tempfile
import os
import threading

# ================= CONFIG =================
DEVICE_URL = "http://macropad.local"

FW_VERSION_URL = "https://raw.githubusercontent.com/Archer2121/Macropad/main/version.txt"
FW_BIN_URL = "https://raw.githubusercontent.com/Archer2121/Macropad/main/main/build/esp32.esp32.lolin_s3/main.ino.bin"

BAUDRATE = "460800"

# ================= UI SETUP =================
root = tk.Tk()
root.title("ESP32 Macropad Updater")
root.geometry("420x360")

tk.Label(root, text="ESP32 Macropad Firmware Updater", font=("Arial", 12, "bold")).pack(pady=10)

progress = ttk.Progressbar(root, length=360, mode="determinate")
progress.pack(pady=10)

status = tk.Label(root, text="Idle")
status.pack()

def set_progress(val, text=None):
    progress["value"] = val
    if text:
        status.config(text=text)
    root.update_idletasks()

# ================= SERIAL PORTS =================
port_var = tk.StringVar()
port_menu = tk.OptionMenu(root, port_var, "")
port_menu.pack(pady=5)

def refresh_ports():
    ports = serial.tools.list_ports.comports()
    menu = port_menu["menu"]
    menu.delete(0, "end")
    for p in ports:
        menu.add_command(label=p.device, command=lambda v=p.device: port_var.set(v))
    if ports:
        port_var.set(ports[0].device)

tk.Button(root, text="Refresh COM Ports", command=refresh_ports).pack()

# ================= OTA UPDATE =================
def ota_update():
    def task():
        try:
            set_progress(0, "Checking firmware version...")

            local = requests.get(DEVICE_URL + "/version", timeout=3).text.strip()
            remote = requests.get(FW_VERSION_URL, timeout=3).text.strip()

            if local == remote:
                set_progress(0, "Firmware already up to date")
                messagebox.showinfo("Up to date", f"Version {local}")
                return

            if not messagebox.askyesno(
                "Update Available",
                f"Installed: {local}\nLatest: {remote}\n\nInstall update?"
            ):
                return

            # Download firmware
            set_progress(5, "Downloading firmware...")
            r = requests.get(FW_BIN_URL, stream=True)
            total = int(r.headers.get("Content-Length", 1))
            downloaded = 0

            fw_path = os.path.join(tempfile.gettempdir(), "macropad.bin")
            with open(fw_path, "wb") as f:
                for chunk in r.iter_content(4096):
                    if chunk:
                        f.write(chunk)
                        downloaded += len(chunk)
                        pct = 5 + int(45 * downloaded / total)
                        set_progress(pct)

            # Upload OTA
            set_progress(55, "Uploading OTA...")
            with open(fw_path, "rb") as fw:
                requests.post(
                    DEVICE_URL + "/update",
                    files={"firmware": fw},
                    timeout=30
                )

            set_progress(100, "OTA update complete")
            messagebox.showinfo("Success", "Firmware updated successfully\nDevice rebooting")

        except Exception as e:
            set_progress(0, "OTA error")
            messagebox.showerror("OTA Error", str(e))

    threading.Thread(target=task, daemon=True).start()

# ================= SERIAL UPDATE =================
def serial_update():
    def task():
        port = port_var.get()
        if not port:
            messagebox.showerror("Error", "No COM port selected")
            return

        try:
            set_progress(0, "Downloading firmware...")
            r = requests.get(FW_BIN_URL)
            fw_path = os.path.join(tempfile.gettempdir(), "macropad.bin")
            open(fw_path, "wb").write(r.content)

            set_progress(10, "Flashing via serial...")

            cmd = [
                "esptool.py",
                "--chip", "esp32s3",
                "--port", port,
                "--baud", BAUDRATE,
                "write_flash",
                "-z",
                "0x0",
                fw_path
            ]

            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True
            )

            for line in proc.stdout:
                if "%" in line:
                    try:
                        pct = int(line.strip().split("%")[0].split()[-1])
                        set_progress(10 + int(pct * 0.9), f"Flashing {pct}%")
                    except:
                        pass

            proc.wait()
            set_progress(100, "Serial flash complete")
            messagebox.showinfo("Success", "Firmware flashed via USB")

        except Exception as e:
            set_progress(0, "Serial error")
            messagebox.showerror("Serial Error", str(e))

    threading.Thread(target=task, daemon=True).start()

# ================= BUTTONS =================
tk.Button(root, text="OTA Update (Wi-Fi)", command=ota_update).pack(pady=8)
tk.Button(root, text="Flash via COM Port (USB)", command=serial_update).pack(pady=5)

refresh_ports()
root.mainloop()

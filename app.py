import tkinter as tk
from tkinter import ttk, messagebox
import requests
import threading
import serial.tools.list_ports
import subprocess
import os

FW_VERSION_URL = "https://raw.githubusercontent.com/Archer2121/Macropad/main/version.txt"
FW_BIN_URL = "https://raw.githubusercontent.com/Archer2121/Macropad/main/main/build/esp32.esp32.lolin_s3/main.ino.bin"

BIN_FILE = "macropad_firmware.bin"


# ---------------- DEVICE DISCOVERY ----------------

def find_device():
    for i in range(1, 255):
        ip = f"192.168.1.{i}"
        try:
            r = requests.get(f"http://{ip}/version", timeout=0.25)
            if r.status_code == 200:
                return ip
        except:
            pass
    return None


# ---------------- GUI APP ----------------

class MacropadApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Macropad Firmware Updater")
        self.geometry("420x260")
        self.resizable(False, False)

        self.status = tk.StringVar(value="Idle")
        self.progress = tk.IntVar(value=0)

        ttk.Label(self, text="Macropad Updater", font=("Segoe UI", 14)).pack(pady=10)

        self.progressbar = ttk.Progressbar(self, length=360, variable=self.progress)
        self.progressbar.pack(pady=10)

        ttk.Label(self, textvariable=self.status).pack()

        ttk.Button(self, text="Update via Wi-Fi (OTA)", command=self.ota_wifi).pack(pady=5)
        ttk.Button(self, text="Update via USB (COM)", command=self.ota_usb).pack(pady=5)

        self.combobox = ttk.Combobox(self, state="readonly")
        self.combobox.pack(pady=5)
        self.refresh_ports()

        ttk.Button(self, text="Refresh COM Ports", command=self.refresh_ports).pack(pady=5)

    # ---------------- HELPERS ----------------

    def set_status(self, msg):
        self.status.set(msg)
        self.update_idletasks()

    def download_firmware(self):
        self.set_status("Downloading firmware...")
        self.progress.set(0)

        r = requests.get(FW_BIN_URL, stream=True)
        total = int(r.headers.get("content-length", 0))
        downloaded = 0

        with open(BIN_FILE, "wb") as f:
            for chunk in r.iter_content(1024):
                f.write(chunk)
                downloaded += len(chunk)
                self.progress.set(int((downloaded / total) * 100))
                self.update_idletasks()

    # ---------------- OTA WIFI ----------------

    def ota_wifi(self):
        threading.Thread(target=self._ota_wifi).start()

    def _ota_wifi(self):
        self.set_status("Searching for Macropad...")
        ip = find_device()
        if not ip:
            messagebox.showerror("Not Found", "Macropad not found on Wi-Fi")
            return

        self.set_status(f"Found {ip}, triggering OTA...")
        try:
            requests.get(f"http://{ip}/ota", timeout=2)
            self.set_status("OTA triggered — upload via Arduino OTA")
            messagebox.showinfo(
                "OTA Ready",
                "Device rebooted into OTA mode.\n\nUpload firmware using Arduino OTA or next OTA cycle."
            )
        except:
            messagebox.showerror("Error", "Failed to trigger OTA")

    # ---------------- OTA USB ----------------

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        self.combobox["values"] = [p.device for p in ports]
        if ports:
            self.combobox.current(0)

    def ota_usb(self):
        threading.Thread(target=self._ota_usb).start()

    def _ota_usb(self):
        port = self.combobox.get()
        if not port:
            messagebox.showerror("Error", "No COM port selected")
            return

        self.download_firmware()
        self.set_status("Flashing firmware via USB...")

        try:
            subprocess.run([
                "esptool",
                "--chip", "esp32s3",
                "--port", port,
                "--baud", "460800",
                "write_flash", "0x0", BIN_FILE
            ], check=True)

            self.progress.set(100)
            self.set_status("Update complete")
            messagebox.showinfo("Success", "Firmware update complete!")

        except subprocess.CalledProcessError:
            messagebox.showerror("Error", "Flashing failed")

        finally:
            if os.path.exists(BIN_FILE):
                os.remove(BIN_FILE)


# ---------------- RUN ----------------

if __name__ == "__main__":
    MacropadApp().mainloop()

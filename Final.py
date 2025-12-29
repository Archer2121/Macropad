import os, threading, subprocess, time, json
import serial, serial.tools.list_ports
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

# ================= CONFIG =================
CHIP = "esp32s3"
FLASH_BAUD = "460800"
SERIAL_BAUD = 115200

FW_DIR = "firmware"
FILES = {
    "bootloader": ("bootloader.bin", "0x0000"),
    "partitions": ("partitions.bin", "0x8000"),
    "boot_app0": ("boot_app0.bin", "0xE000"),
    "app": ("firmware.bin", "0x10000"),
}

# ==========================================

class UnifiedMacropadApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("LOLIN S3 Macropad Control Center")
        self.geometry("900x620")

        self.port = tk.StringVar()
        self.status = tk.StringVar(value="Disconnected")
        self.ser = None

        self.build_ui()
        self.refresh_ports()

    # ---------- UI ----------
    def build_ui(self):
        top = ttk.Frame(self)
        top.pack(fill="x", padx=10, pady=5)

        ttk.Label(top, text="Device:").pack(side="left")
        self.port_box = ttk.Combobox(top, textvariable=self.port, width=20)
        self.port_box.pack(side="left", padx=5)

        ttk.Button(top, text="Refresh", command=self.refresh_ports).pack(side="left")
        ttk.Button(top, text="Connect", command=self.connect_serial).pack(side="left", padx=5)

        ttk.Label(top, textvariable=self.status).pack(side="right")

        self.tabs = ttk.Notebook(self)
        self.tabs.pack(fill="both", expand=True)

        self.tab_serial()
        self.tab_flash()
        self.tab_macros()

    # ---------- Serial Tab ----------
    def tab_serial(self):
        tab = ttk.Frame(self.tabs)
        self.tabs.add(tab, text="Serial Monitor")

        self.serial_box = scrolledtext.ScrolledText(tab, height=28)
        self.serial_box.pack(fill="both", expand=True)

        cmd = ttk.Frame(tab)
        cmd.pack(fill="x")

        self.serial_entry = ttk.Entry(cmd)
        self.serial_entry.pack(side="left", fill="x", expand=True, padx=5)

        ttk.Button(cmd, text="Send", command=self.send_serial).pack(side="right")

    # ---------- Firmware Tab ----------
    def tab_flash(self):
        tab = ttk.Frame(self.tabs)
        self.tabs.add(tab, text="Firmware")

        ttk.Label(tab, text="Firmware Update", font=("Segoe UI", 12, "bold")).pack(pady=5)
        ttk.Button(tab, text="Flash Firmware", command=self.flash_firmware).pack(pady=10)

        self.flash_log = scrolledtext.ScrolledText(tab, height=20)
        self.flash_log.pack(fill="both", expand=True, padx=10)

    # ---------- Macro Config Tab ----------
    def tab_macros(self):
        tab = ttk.Frame(self.tabs)
        self.tabs.add(tab, text="Macro Config")

        ttk.Label(tab, text="Button Macros", font=("Segoe UI", 12, "bold")).pack()

        self.macro_a = ttk.Entry(tab, width=60)
        self.macro_b = ttk.Entry(tab, width=60)

        ttk.Label(tab, text="Button 1 (Pin 2)").pack()
        self.macro_a.pack(pady=5)

        ttk.Label(tab, text="Button 2 (Pin 4)").pack()
        self.macro_b.pack(pady=5)

        ttk.Button(tab, text="Send to Device", command=self.send_macros).pack(pady=10)

    # ---------- Device ----------
    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_box["values"] = ports
        if ports:
            self.port.set(ports[0])

    def connect_serial(self):
        try:
            self.ser = serial.Serial(self.port.get(), SERIAL_BAUD, timeout=0.1)
            self.status.set("Connected")
            threading.Thread(target=self.read_serial, daemon=True).start()
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def read_serial(self):
        while self.ser:
            try:
                line = self.ser.readline().decode(errors="ignore")
                if line:
                    self.serial_box.insert(tk.END, line)
                    self.serial_box.see(tk.END)
            except:
                break

    def send_serial(self):
        if self.ser:
            self.ser.write((self.serial_entry.get() + "\n").encode())
            self.serial_entry.delete(0, tk.END)

    # ---------- Firmware ----------
    def flash_firmware(self):
        threading.Thread(target=self._flash_thread, daemon=True).start()

    def _flash_thread(self):
        try:
            self.flash_log.insert(tk.END, "Erasing flash...\n")
            subprocess.run(
                ["esptool", "--chip", CHIP, "--port", self.port.get(), "erase-flash"],
                check=True
            )

            cmd = ["esptool", "--chip", CHIP, "--port", self.port.get(),
                   "--baud", FLASH_BAUD, "write_flash"]

            for _, (f, addr) in FILES.items():
                cmd += [addr, os.path.join(FW_DIR, f)]

            self.flash_log.insert(tk.END, "Flashing...\n")
            subprocess.run(cmd, check=True)

            self.flash_log.insert(tk.END, "✔ Flash complete\n")

        except Exception as e:
            self.flash_log.insert(tk.END, f"ERROR: {e}\n")

    # ---------- Macros ----------
    def send_macros(self):
        if not self.ser:
            return

        data = {
            "btn1": self.macro_a.get(),
            "btn2": self.macro_b.get()
        }

        msg = "MACRO:" + json.dumps(data) + "\n"
        self.ser.write(msg.encode())

# ================= RUN =================
UnifiedMacropadApp().mainloop()

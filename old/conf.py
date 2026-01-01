import serial, threading
import serial.tools.list_ports
import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext

PRESETS = [
    "CTRL+C", "CTRL+V", "CTRL+X",
    "ALT+TAB", "WIN+D", "WIN+L",
    "CTRL+ALT+DEL"
]

class MacroEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Macropad Macro Editor")
        self.geometry("720x420")

        self.port = tk.StringVar()
        self.m1 = tk.StringVar(value=PRESETS[0])
        self.m2 = tk.StringVar(value=PRESETS[1])

        ttk.Label(self, text="COM Port").pack()
        self.ports = ttk.Combobox(self, textvariable=self.port)
        self.ports.pack()

        ttk.Button(self, text="Refresh Ports", command=self.refresh_ports).pack(pady=2)

        ttk.Label(self, text="Button 1").pack()
        ttk.Combobox(self, values=PRESETS, textvariable=self.m1).pack()

        ttk.Label(self, text="Button 2").pack()
        ttk.Combobox(self, values=PRESETS, textvariable=self.m2).pack()

        ttk.Button(self, text="Send Macros", command=self.send).pack(pady=6)

        ttk.Label(self, text="Serial Monitor").pack()
        self.serial_box = scrolledtext.ScrolledText(self, height=12)
        self.serial_box.pack(fill="both", expand=True, padx=10)

        self.ser = None
        self.refresh_ports()
        self.start_serial()

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.ports["values"] = ports
        if ports:
            self.port.set(ports[0])

    def start_serial(self):
        try:
            self.ser = serial.Serial(self.port.get(), 115200, timeout=0.1)
            threading.Thread(target=self.read_serial, daemon=True).start()
        except:
            pass

    def read_serial(self):
        while self.ser:
            try:
                line = self.ser.readline().decode(errors="ignore")
                if line:
                    self.serial_box.insert(tk.END, line)
                    self.serial_box.see(tk.END)
            except:
                break

    def send(self):
        try:
            cmd = f"/set?m1={self.m1.get()}&m2={self.m2.get()}\n"
            self.ser.write(cmd.encode())
            messagebox.showinfo("Success", "Macros sent")
        except Exception as e:
            messagebox.showerror("Error", str(e))

MacroEditor().mainloop()

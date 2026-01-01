#!/usr/bin/env python3
"""Lolin S3 Configurator — improved interface for the sketch.

Features added:
- Auto-discover common AP IPs (tries 192.168.4.1 and lolin.local)
- Fetch and display current configuration
- Save configuration and wait/poll for device to reboot
- Simple status messages in UI

Usage: python Macropad/lolin_configurator.py
Requires: requests
"""
import tkinter as tk
from tkinter import messagebox
import threading
import time
import requests


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('Lolin S3 Configurator')

        tk.Label(self, text='Device URL or IP').grid(row=0, column=0, sticky='w')
        self.ip_entry = tk.Entry(self, width=36)
        self.ip_entry.grid(row=0, column=1, padx=6, pady=6)
        tk.Button(self, text='Auto-discover', command=self.autodiscover).grid(row=0, column=2, padx=6)

        tk.Label(self, text='WiFi SSID').grid(row=1, column=0, sticky='w')
        self.ssid = tk.Entry(self, width=50)
        self.ssid.grid(row=1, column=1, columnspan=2, padx=6, pady=6)

        tk.Label(self, text='WiFi Password').grid(row=2, column=0, sticky='w')
        self.wpass = tk.Entry(self, width=50, show='*')
        self.wpass.grid(row=2, column=1, columnspan=2, padx=6, pady=6)

        tk.Label(self, text='Button 1 Macro').grid(row=3, column=0, sticky='nw')
        self.m1 = tk.Text(self, width=50, height=4)
        self.m1.grid(row=3, column=1, columnspan=2, padx=6, pady=6)

        tk.Label(self, text='Button 2 Macro').grid(row=4, column=0, sticky='nw')
        self.m2 = tk.Text(self, width=50, height=4)
        self.m2.grid(row=4, column=1, columnspan=2, padx=6, pady=6)

        btn_frame = tk.Frame(self)
        btn_frame.grid(row=5, column=1, pady=8)
        tk.Button(btn_frame, text='Fetch', command=self.fetch).pack(side='left', padx=6)
        tk.Button(btn_frame, text='Save', command=self.save).pack(side='left', padx=6)

        self.status = tk.Label(self, text='Ready', anchor='w')
        self.status.grid(row=6, column=0, columnspan=3, sticky='we', padx=6, pady=(0,6))

    def set_status(self, txt):
        self.status.config(text=txt)
        self.update_idletasks()

    def _normalize_base(self, raw):
        if not raw: return None
        raw = raw.strip()
        if raw.startswith('http://') or raw.startswith('https://'):
            return raw.rstrip('/')
        return 'http://' + raw.rstrip('/')

    def _url(self):
        return self._normalize_base(self.ip_entry.get())

    def autodiscover(self):
        # Try common AP address and mDNS name
        candidates = ['http://192.168.4.1', 'http://lolin.local']
        self.set_status('Auto-discovering...')
        for c in candidates:
            try:
                r = requests.get(c + '/config', timeout=1.0)
                if r.status_code == 200:
                    self.ip_entry.delete(0, 'end')
                    self.ip_entry.insert(0, c)
                    self.set_status(f'Found device at {c}')
                    self.fetch()
                    return
            except Exception:
                pass
        self.set_status('No device found (try entering IP or connect to AP)')

    def fetch(self):
        base = self._url()
        if not base:
            messagebox.showerror('Error', 'Enter device address or use Auto-discover')
            return
        self.set_status('Fetching config...')
        try:
            r = requests.get(base + '/config', timeout=3)
            r.raise_for_status()
            j = r.json()
            self.ssid.delete(0, 'end'); self.ssid.insert(0, j.get('wifi_ssid',''))
            self.wpass.delete(0, 'end'); self.wpass.insert(0, j.get('wifi_pass',''))
            self.m1.delete('1.0', 'end'); self.m1.insert('1.0', j.get('macro1',''))
            self.m2.delete('1.0', 'end'); self.m2.insert('1.0', j.get('macro2',''))
            self.set_status('Config fetched')
        except Exception as e:
            messagebox.showerror('Error', f'Fetch failed: {e}')
            self.set_status('Fetch failed')

    def save(self):
        base = self._url()
        if not base:
            messagebox.showerror('Error', 'Enter device address or use Auto-discover')
            return
        payload = {
            'wifi_ssid': self.ssid.get().strip(),
            'wifi_pass': self.wpass.get().strip(),
            'macro1': self.m1.get('1.0','end').strip(),
            'macro2': self.m2.get('1.0','end').strip(),
        }
        self.set_status('Saving config...')
        try:
            r = requests.post(base + '/config', json=payload, timeout=5)
            r.raise_for_status()
            self.set_status('Saved — device rebooting')
            # Wait/poll for device to come back up (either AP at 192.168.4.1 or same base)
            threading.Thread(target=self._wait_for_reboot, args=(base,)).start()
        except Exception as e:
            messagebox.showerror('Error', f'Save failed: {e}')
            self.set_status('Save failed')

    def _wait_for_reboot(self, base):
        # After POST the device restarts. Try polling the provided base and 192.168.4.1.
        candidates = [base, 'http://192.168.4.1']
        timeout = 20
        start = time.time()
        while time.time() - start < timeout:
            for c in candidates:
                try:
                    r = requests.get(c + '/config', timeout=1.0)
                    if r.status_code == 200:
                        self.set_status(f'Device available at {c}')
                        # Update ip field to discovered location
                        self.ip_entry.delete(0, 'end'); self.ip_entry.insert(0, c)
                        return
                except Exception:
                    pass
            time.sleep(0.8)
        self.set_status('Device did not appear — it may be on another network')
        messagebox.showinfo('Notice', 'Device did not respond after reboot. If you configured WiFi, the device may have connected to your router — check your router DHCP list.')


if __name__ == '__main__':
    app = App()
    app.mainloop()

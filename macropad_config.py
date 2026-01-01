import tkinter as tk
from tkinter import ttk, messagebox
import json
import os
import urllib.parse

try:
    import requests
except ImportError:
    requests = None

BASE_DIR = os.path.dirname(__file__)
MACRO_FILE = os.path.join(BASE_DIR, "macros.json")

DEFAULT_MACROS = [
    {"name": "Default CtrlAltDel", "value": "CTRL+ALT+DEL"},
    {"name": "Show Desktop", "value": "WIN+D"},
    {"name": "Volume Mute", "value": "MUTE"}
]

class MacroManager:
    def __init__(self):
        self.macros = []
        self.assignments = {"m1": "", "m2": ""}
        self.load()

    def load(self):
        if os.path.exists(MACRO_FILE):
            try:
                with open(MACRO_FILE, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                self.macros = data.get('macros', DEFAULT_MACROS)
                self.assignments = data.get('assignments', self.assignments)
            except Exception:
                self.macros = DEFAULT_MACROS.copy()
        else:
            self.macros = DEFAULT_MACROS.copy()

    def save(self):
        data = {'macros': self.macros, 'assignments': self.assignments}
        with open(MACRO_FILE, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2)

    def add_macro(self, name, value):
        self.macros.append({'name': name, 'value': value})

    def update_macro(self, idx, name, value):
        self.macros[idx] = {'name': name, 'value': value}

    def delete_macro(self, idx):
        del self.macros[idx]

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('Macropad Config')
        self.geometry('640x360')
        self.manager = MacroManager()
        self.create_widgets()

    def create_widgets(self):
        top = ttk.Frame(self)
        top.pack(fill='x', padx=8, pady=6)

        ttk.Label(top, text='Device Host/IP:').pack(side='left')
        self.host_var = tk.StringVar(value='http://macropad.local')
        self.host_entry = ttk.Entry(top, textvariable=self.host_var, width=36)
        self.host_entry.pack(side='left', padx=6)

        ttk.Button(top, text='Send Assignments', command=self.send_assignments).pack(side='left', padx=6)
        ttk.Button(top, text='Save Locally', command=self.save_locally).pack(side='left')

        nb = ttk.Notebook(self)
        nb.pack(fill='both', expand=True, padx=8, pady=8)

        # Buttons Tab
        btn_frame = ttk.Frame(nb)
        nb.add(btn_frame, text='Buttons')

        self.create_button_tab(btn_frame)

        # Macros Tab
        macros_frame = ttk.Frame(nb)
        nb.add(macros_frame, text='Macros')

        self.create_macros_tab(macros_frame)

    def create_button_tab(self, parent):
        frm = ttk.Frame(parent)
        frm.pack(fill='both', expand=True, padx=10, pady=10)

        # Button 1
        b1 = ttk.LabelFrame(frm, text='Button 1')
        b1.pack(side='left', fill='both', expand=True, padx=6, pady=6)
        self.b1_var = tk.StringVar(value=self.manager.assignments.get('m1', ''))
        ttk.Label(b1, text='Assigned Macro:').pack(anchor='w', padx=6, pady=(6,0))
        self.b1_combo = ttk.Combobox(b1, state='readonly')
        self.b1_combo.pack(fill='x', padx=6)
        # Button 2
        b2 = ttk.LabelFrame(frm, text='Button 2')
        b2.pack(side='left', fill='both', expand=True, padx=6, pady=6)
        self.b2_var = tk.StringVar(value=self.manager.assignments.get('m2', ''))
        ttk.Label(b2, text='Assigned Macro:').pack(anchor='w', padx=6, pady=(6,0))
        self.b2_combo = ttk.Combobox(b2, state='readonly')
        self.b2_combo.pack(fill='x', padx=6)

        # Fill combo lists
        self.refresh_macro_comboboxes()

        # Buttons below
        footer = ttk.Frame(parent)
        footer.pack(fill='x', padx=10, pady=6)
        ttk.Button(footer, text='Assign & Send', command=self.assign_and_send).pack(side='left')
        ttk.Button(footer, text='Set from entries', command=self.set_assignments_from_entries).pack(side='left', padx=6)

    def create_macros_tab(self, parent):
        left = ttk.Frame(parent)
        left.pack(side='left', fill='both', expand=True, padx=6, pady=6)

        self.macro_list = tk.Listbox(left)
        self.macro_list.pack(fill='both', expand=True, padx=6, pady=6)
        self.macro_list.bind('<<ListboxSelect>>', self.on_macro_select)

        right = ttk.Frame(parent)
        right.pack(side='left', fill='y', padx=6, pady=6)

        ttk.Label(right, text='Name:').pack(anchor='w')
        self.m_name = tk.StringVar()
        ttk.Entry(right, textvariable=self.m_name, width=28).pack()

        ttk.Label(right, text='Macro String:').pack(anchor='w', pady=(6,0))
        self.m_value = tk.StringVar()
        ttk.Entry(right, textvariable=self.m_value, width=28).pack()

        ttk.Button(right, text='Add', command=self.add_macro).pack(fill='x', pady=(6,0))
        ttk.Button(right, text='Update', command=self.update_macro).pack(fill='x', pady=(6,4))
        ttk.Button(right, text='Delete', command=self.delete_macro).pack(fill='x')

        ttk.Button(right, text='Save Macros', command=self.save_locally).pack(fill='x', pady=(10,0))

        self.refresh_macro_list()

    def refresh_macro_comboboxes(self):
        names = [m['name'] for m in self.manager.macros]
        self.b1_combo['values'] = names
        self.b2_combo['values'] = names
        # select current assignment if exists
        def select_combo(combo, assigned_value):
            for i, m in enumerate(self.manager.macros):
                if m['value'] == assigned_value or m['name'] == assigned_value:
                    combo.current(i)
                    return
            combo.set('')
        select_combo(self.b1_combo, self.manager.assignments.get('m1', ''))
        select_combo(self.b2_combo, self.manager.assignments.get('m2', ''))

    def refresh_macro_list(self):
        self.macro_list.delete(0, 'end')
        for m in self.manager.macros:
            self.macro_list.insert('end', f"{m['name']} -> {m['value']}")
        self.refresh_macro_comboboxes()

    def on_macro_select(self, evt):
        sel = self.macro_list.curselection()
        if not sel: return
        i = sel[0]
        m = self.manager.macros[i]
        self.m_name.set(m['name'])
        self.m_value.set(m['value'])

    def add_macro(self):
        name = self.m_name.get().strip()
        val = self.m_value.get().strip()
        if not name or not val:
            messagebox.showwarning('Missing', 'Name and macro string required')
            return
        self.manager.add_macro(name, val)
        self.refresh_macro_list()
        self.m_name.set('')
        self.m_value.set('')

    def update_macro(self):
        sel = self.macro_list.curselection()
        if not sel:
            messagebox.showwarning('Select', 'Select a macro to update')
            return
        i = sel[0]
        name = self.m_name.get().strip()
        val = self.m_value.get().strip()
        if not name or not val:
            messagebox.showwarning('Missing', 'Name and macro string required')
            return
        self.manager.update_macro(i, name, val)
        self.refresh_macro_list()

    def delete_macro(self):
        sel = self.macro_list.curselection()
        if not sel:
            messagebox.showwarning('Select', 'Select a macro to delete')
            return
        i = sel[0]
        if messagebox.askyesno('Confirm', 'Delete selected macro?'):
            self.manager.delete_macro(i)
            self.refresh_macro_list()

    def save_locally(self):
        try:
            # store current combobox selections as assignments
            m1 = self.get_selected_macro_value(self.b1_combo)
            m2 = self.get_selected_macro_value(self.b2_combo)
            if m1:
                self.manager.assignments['m1'] = m1
            if m2:
                self.manager.assignments['m2'] = m2
            self.manager.save()
            messagebox.showinfo('Saved', f'Macros saved to {MACRO_FILE}')
        except Exception as e:
            messagebox.showerror('Error', str(e))

    def get_selected_macro_value(self, combo):
        sel = combo.get()
        # if sel matches a name choose its value
        for m in self.manager.macros:
            if m['name'] == sel:
                return m['value']
        # else treat it as raw macro
        return sel

    def assign_and_send(self):
        m1 = self.get_selected_macro_value(self.b1_combo)
        m2 = self.get_selected_macro_value(self.b2_combo)
        self.manager.assignments['m1'] = m1
        self.manager.assignments['m2'] = m2
        self.manager.save()
        self.send_to_device(m1, m2)

    def set_assignments_from_entries(self):
        # allow user to type raw macro strings into combobox entries (non-readonly mode not used), fallback
        m1 = self.b1_combo.get()
        m2 = self.b2_combo.get()
        self.manager.assignments['m1'] = m1
        self.manager.assignments['m2'] = m2
        self.manager.save()
        messagebox.showinfo('Assigned', 'Assignments updated locally')

    def send_assignments(self):
        # just send stored assignments
        m1 = self.manager.assignments.get('m1', '')
        m2 = self.manager.assignments.get('m2', '')
        if not m1 and not m2:
            messagebox.showwarning('Nothing', 'No assignments to send')
            return
        self.send_to_device(m1, m2)

    def send_to_device(self, m1, m2):
        if requests is None:
            messagebox.showerror('Missing dependency', 'The `requests` package is required. Install with: pip install requests')
            return
        host = self.host_var.get().strip()
        if not host:
            messagebox.showwarning('Host', 'Enter device host or IP')
            return
        if not host.startswith('http://') and not host.startswith('https://'):
            host = 'http://' + host
        # build URL
        params = {}
        if m1 is not None:
            params['m1'] = m1
        if m2 is not None:
            params['m2'] = m2
        query = urllib.parse.urlencode(params, safe='+')
        url = host.rstrip('/') + '/set?' + query
        try:
            resp = requests.get(url, timeout=5)
            if resp.status_code == 200:
                messagebox.showinfo('Success', 'Assignments sent to device')
            else:
                messagebox.showerror('Error', f'HTTP {resp.status_code}: {resp.text}')
        except Exception as e:
            messagebox.showerror('Request failed', str(e))

if __name__ == '__main__':
    app = App()
    app.mainloop()

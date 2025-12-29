import requests, tkinter as tk
from tkinter import messagebox
import tempfile, os

DEVICE="http://macropad.local"
FW_VERSION_URL="https://github.com/Archer2121/Macropad/blob/main/version.txt"
FW_BIN_URL="https://github.com/Archer2121/Macropad/blob/main/main/build/esp32.esp32.lolin_s3/main.ino.bin"

def update():
 try:
  local=requests.get(DEVICE+"/version",timeout=2).text.strip()
  remote=requests.get(FW_VERSION_URL).text.strip()
  if local==remote:
   messagebox.showinfo("Up to date",local); return
  r=requests.get(FW_BIN_URL)
  path=os.path.join(tempfile.gettempdir(),"macropad.bin")
  open(path,"wb").write(r.content)
  requests.post(DEVICE+"/update",files={"firmware":open(path,"rb")})
  messagebox.showinfo("Updated","Firmware installed")
 except Exception as e:
  messagebox.showerror("Error",str(e))

root=tk.Tk()
root.title("Macropad Updater")
tk.Button(root,text="Install Latest Firmware",command=update).pack(padx=20,pady=20)
root.mainloop()

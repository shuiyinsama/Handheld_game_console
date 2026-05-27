import os
import queue
import re
import subprocess
import threading
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk

try:
    import winreg
except ImportError:
    winreg = None


PROJECT_ROOT = Path(__file__).resolve().parents[1]
IDF_SCRIPT = PROJECT_ROOT / "tools" / "idf-build.ps1"


class HandheldControlApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Handheld Console Control Panel")
        self.geometry("1080x680")
        self.minsize(900, 540)

        self.process = None
        self.reader_thread = None
        self.log_queue = queue.Queue()

        self._build_ui()
        self.refresh_ports()
        self.after(100, self._drain_log_queue)

    def _build_ui(self):
        self.configure(bg="#181b1e")

        top = tk.Frame(self, bg="#1f2428", padx=18, pady=16)
        top.pack(side=tk.TOP, fill=tk.X)

        title_row = tk.Frame(top, bg="#1f2428")
        title_row.pack(side=tk.TOP, fill=tk.X)

        title = tk.Label(
            title_row,
            text="Handheld Console",
            bg="#1f2428",
            fg="#ffffff",
            font=("Segoe UI", 17, "bold"),
        )
        title.pack(side=tk.LEFT)

        self.status_var = tk.StringVar(value="Idle")
        status = tk.Label(
            title_row,
            textvariable=self.status_var,
            bg="#1f2428",
            fg="#aab8c4",
            font=("Segoe UI", 10),
            padx=18,
        )
        status.pack(side=tk.LEFT)

        controls = tk.Frame(top, bg="#1f2428")
        controls.pack(side=tk.TOP, fill=tk.X, pady=(14, 0))

        tk.Label(
            controls,
            text="COM Port",
            bg="#1f2428",
            fg="#dce7ef",
            font=("Segoe UI", 10),
        ).pack(side=tk.LEFT)

        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(
            controls,
            textvariable=self.port_var,
            width=34,
        )
        self.port_combo.pack(side=tk.LEFT, padx=(8, 10))

        self.buttons = []
        self._add_button(controls, "Refresh", self.refresh_ports)
        self._add_button(controls, "Build", lambda: self.start_task("Build"))
        self._add_button(controls, "Flash", lambda: self.start_task("Flash"))
        self._add_button(controls, "Flash + Monitor", lambda: self.start_task("FlashMonitor"))
        self._add_button(controls, "Monitor", lambda: self.start_task("Monitor"))

        self.stop_button = tk.Button(
            controls,
            text="Stop",
            width=10,
            command=self.stop_task,
            bg="#6b303b",
            fg="#ffffff",
            activebackground="#7d3b47",
            activeforeground="#ffffff",
            relief=tk.FLAT,
            state=tk.DISABLED,
        )
        self.stop_button.pack(side=tk.LEFT, padx=(10, 0))

        log_frame = tk.Frame(self, bg="#0a0c0e")
        log_frame.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        self.log = tk.Text(
            log_frame,
            bg="#0a0c0e",
            fg="#dce7ef",
            insertbackground="#dce7ef",
            font=("Consolas", 10),
            wrap=tk.NONE,
            relief=tk.FLAT,
        )
        self.log.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        y_scroll = tk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log.yview)
        y_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.log.configure(yscrollcommand=y_scroll.set)

        x_scroll = tk.Scrollbar(self, orient=tk.HORIZONTAL, command=self.log.xview)
        x_scroll.pack(side=tk.BOTTOM, fill=tk.X)
        self.log.configure(xscrollcommand=x_scroll.set)

        self.append_log(f"Project root: {PROJECT_ROOT}\n")
        self.append_log(f"ESP-IDF script: {IDF_SCRIPT}\n")

    def _add_button(self, parent, text, command):
        button = tk.Button(
            parent,
            text=text,
            command=command,
            width=max(10, len(text) + 2),
            bg="#242a30",
            fg="#ecf0f3",
            activebackground="#303840",
            activeforeground="#ffffff",
            relief=tk.FLAT,
        )
        button.pack(side=tk.LEFT, padx=5)
        self.buttons.append(button)
        return button

    def append_log(self, text):
        self.log.insert(tk.END, text)
        self.log.see(tk.END)

    def refresh_ports(self):
        ports = self._list_ports()
        if "COM6" not in ports:
            ports.append("COM6")
        ports = sorted(set(ports), key=self._port_sort_key)

        previous = self.port_var.get().strip()
        self.port_combo["values"] = ports
        if previous:
            self.port_var.set(previous)
        elif ports:
            self.port_var.set(ports[0])
        else:
            self.port_var.set("COM6")

    def _list_ports(self):
        ports = []
        if winreg is None:
            return ports

        try:
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM")
            index = 0
            while True:
                try:
                    _, value, _ = winreg.EnumValue(key, index)
                    if isinstance(value, str) and re.fullmatch(r"COM\d+", value, re.IGNORECASE):
                        ports.append(value.upper())
                    index += 1
                except OSError:
                    break
            winreg.CloseKey(key)
        except OSError:
            pass
        return ports

    @staticmethod
    def _port_sort_key(port):
        match = re.search(r"COM(\d+)", port, re.IGNORECASE)
        return int(match.group(1)) if match else 9999

    def _selected_port(self):
        text = self.port_var.get().strip()
        match = re.search(r"COM\d+", text, re.IGNORECASE)
        return match.group(0).upper() if match else text

    def _idf_args(self, action):
        port = self._selected_port()
        if action == "Build":
            return ["build"]
        if action == "Flash":
            if not port:
                raise ValueError("Select or type a COM port first.")
            return ["-p", port, "flash"]
        if action == "FlashMonitor":
            if not port:
                raise ValueError("Select or type a COM port first.")
            return ["-p", port, "flash", "monitor"]
        if action == "Monitor":
            if not port:
                raise ValueError("Select or type a COM port first.")
            return ["-p", port, "monitor"]
        raise ValueError(f"Unknown action: {action}")

    def start_task(self, action):
        if self.process and self.process.poll() is None:
            messagebox.showwarning("Handheld Console", "A task is already running.")
            return

        try:
            idf_args = self._idf_args(action)
        except ValueError as error:
            messagebox.showwarning("Handheld Console", str(error))
            return

        command = [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(IDF_SCRIPT),
            *idf_args,
        ]

        self.append_log("\n> " + " ".join(command) + "\n")
        self._set_running(True, action)

        creationflags = 0
        if os.name == "nt":
            creationflags = subprocess.CREATE_NO_WINDOW

        try:
            self.process = subprocess.Popen(
                command,
                cwd=PROJECT_ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                creationflags=creationflags,
            )
        except OSError as error:
            self._set_running(False)
            messagebox.showerror("Handheld Console", str(error))
            return

        self.reader_thread = threading.Thread(
            target=self._read_process_output,
            args=(action, self.process),
            daemon=True,
        )
        self.reader_thread.start()

    def _read_process_output(self, action, process):
        try:
            for line in process.stdout:
                self.log_queue.put(line)
            code = process.wait()
            self.log_queue.put(f"\n[{action}] exited with code {code}\n")
        finally:
            self.log_queue.put(("DONE", action))

    def _drain_log_queue(self):
        try:
            while True:
                item = self.log_queue.get_nowait()
                if isinstance(item, tuple) and item[0] == "DONE":
                    self.process = None
                    self._set_running(False)
                else:
                    self.append_log(item)
        except queue.Empty:
            pass
        self.after(100, self._drain_log_queue)

    def stop_task(self):
        if not self.process or self.process.poll() is not None:
            return
        pid = self.process.pid
        self.append_log(f"\n> taskkill /PID {pid} /T /F\n")
        subprocess.Popen(
            ["taskkill.exe", "/PID", str(pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )

    def _set_running(self, running, action=""):
        self.status_var.set(f"Running: {action}" if running else "Idle")
        state = tk.DISABLED if running else tk.NORMAL
        for button in self.buttons:
            button.configure(state=state)
        self.stop_button.configure(state=(tk.NORMAL if running else tk.DISABLED))


if __name__ == "__main__":
    app = HandheldControlApp()
    app.mainloop()

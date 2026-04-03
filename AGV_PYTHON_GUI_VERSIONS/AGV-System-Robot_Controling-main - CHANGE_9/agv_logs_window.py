# agv_logs_window.py
# Live log viewer window for ESP32 -> /logs
# Requirements: Python 3.x (tkinter is built-in)

import json
import time
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

try:
    import requests
except ImportError:
    raise SystemExit("Please install requests: pip install requests")


ESP32_IP = "172.20.10.12"   # <-- CHANGE THIS to your ESP32 IP
LOG_URL  = f"http://{ESP32_IP}/logs"

POLL_MS = 400               # refresh rate (ms)
MAX_LINES_IN_UI = 2500      # keep UI responsive


class LogViewer(tk.Tk):
    def __init__(self):
        super().__init__()

        self.title("AGV Live Logs")
        self.geometry("900x600")

        # State
        self.running = True
        self.auto_scroll = tk.BooleanVar(value=True)
        self.show_raw = tk.BooleanVar(value=False)

        self.seen = set()           # dedupe cache
        self.seen_order = []        # to limit memory
        self.seen_limit = 3000

        # UI
        self._build_ui()

        # Start polling (thread + ui update queue)
        self.queue = []
        self.lock = threading.Lock()

        self.poll_thread = threading.Thread(target=self._poll_loop, daemon=True)
        self.poll_thread.start()

        self.after(120, self._drain_queue)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def _build_ui(self):
        top = ttk.Frame(self)
        top.pack(fill="x", padx=10, pady=8)

        ttk.Label(top, text=f"Source: {LOG_URL}").pack(side="left")

        ttk.Checkbutton(top, text="Auto-scroll", variable=self.auto_scroll).pack(side="left", padx=12)
        ttk.Checkbutton(top, text="Show raw JSON", variable=self.show_raw).pack(side="left", padx=12)

        ttk.Button(top, text="Clear", command=self.clear).pack(side="right")
        ttk.Button(top, text="Save...", command=self.save).pack(side="right", padx=8)

        # Text area
        mid = ttk.Frame(self)
        mid.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        self.text = tk.Text(mid, wrap="none", font=("Consolas", 10))
        self.text.pack(side="left", fill="both", expand=True)

        yscroll = ttk.Scrollbar(mid, orient="vertical", command=self.text.yview)
        yscroll.pack(side="right", fill="y")
        self.text.configure(yscrollcommand=yscroll.set)

        xscroll = ttk.Scrollbar(self, orient="horizontal", command=self.text.xview)
        xscroll.pack(fill="x", padx=10, pady=(0, 10))
        self.text.configure(xscrollcommand=xscroll.set)

        # Status bar
        self.status = ttk.Label(self, text="Starting...", anchor="w")
        self.status.pack(fill="x", padx=10, pady=(0, 8))

        # Basic tags (optional colors) - not mandatory, but helps readability
        self.text.tag_configure("SYS", foreground="blue")
        self.text.tag_configure("CMD", foreground="purple")
        self.text.tag_configure("PID", foreground="darkgreen")
        self.text.tag_configure("SENS", foreground="gray30")
        self.text.tag_configure("DECIDE", foreground="darkorange")
        self.text.tag_configure("TURN", foreground="brown")
        self.text.tag_configure("STATE", foreground="darkred")
        self.text.tag_configure("ACT", foreground="teal")
        self.text.tag_configure("MODE", foreground="navy")
        self.text.tag_configure("FUNC", foreground="black")
        self.text.tag_configure("ERR", foreground="red")

    def on_close(self):
        self.running = False
        self.destroy()

    def clear(self):
        self.text.delete("1.0", "end")

    def save(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Text file", "*.txt"), ("All files", "*.*")]
        )
        if not path:
            return
        data = self.text.get("1.0", "end-1c")
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(data)
            messagebox.showinfo("Saved", f"Saved logs to:\n{path}")
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def _poll_loop(self):
        session = requests.Session()
        last_ok = time.time()

        while self.running:
            try:
                r = session.get(LOG_URL, timeout=1.2)
                r.raise_for_status()
                payload = r.json()  # expected: list of strings (each string is a JSON-line from Arduino)

                new_lines = []
                for item in payload:
                    # item is a string (the original JSON line)
                    if not isinstance(item, str):
                        continue

                    # dedupe
                    if item in self.seen:
                        continue
                    self._mark_seen(item)

                    # format output
                    line = self._format_line(item)
                    new_lines.append(line)

                if new_lines:
                    with self.lock:
                        self.queue.extend(new_lines)

                last_ok = time.time()
                self._set_status(f"Connected ✅  | received {len(new_lines)} new logs", ok=True)

            except Exception as e:
                # show disconnected if long time without ok
                if time.time() - last_ok > 2.5:
                    self._set_status(f"Disconnected ⚠️  | {e}", ok=False)

            time.sleep(POLL_MS / 1000.0)

    def _set_status(self, msg, ok=True):
        # thread-safe update via after
        def _do():
            self.status.configure(text=msg)
        try:
            self.after(0, _do)
        except tk.TclError:
            pass

    def _mark_seen(self, s):
        self.seen.add(s)
        self.seen_order.append(s)
        if len(self.seen_order) > self.seen_limit:
            old = self.seen_order.pop(0)
            self.seen.discard(old)

    def _format_line(self, raw_line: str) -> str:
        """
        raw_line example:
        {"t":12345,"tag":"PID","msg":"pos=... err=..."}
        """
        if self.show_raw.get():
            return raw_line

        try:
            obj = json.loads(raw_line)
            t = obj.get("t", "")
            tag = obj.get("tag", "LOG")
            msg = obj.get("msg", "")
            return f"[{t:>7}] {tag:<7} | {msg}"
        except Exception:
            # if it's not valid JSON, just show raw
            return raw_line

    def _drain_queue(self):
        if not self.running:
            return

        batch = []
        with self.lock:
            if self.queue:
                batch = self.queue[:]
                self.queue.clear()

        if batch:
            for line in batch:
                # try to tag by TAG word inside formatted line
                tag = "FUNC"
                try:
                    if line.startswith("{"):
                        obj = json.loads(line)
                        tag = obj.get("tag", "FUNC")
                    else:
                        # formatted: [t] TAG | msg
                        parts = line.split()
                        if len(parts) >= 2:
                            tag = parts[1]
                except Exception:
                    tag = "ERR"

                self.text.insert("end", line + "\n", tag)

            # limit lines
            current = int(self.text.index("end-1c").split(".")[0])
            if current > MAX_LINES_IN_UI:
                # delete oldest 20%
                delete_to = int(MAX_LINES_IN_UI * 0.2)
                self.text.delete("1.0", f"{delete_to}.0")

            if self.auto_scroll.get():
                self.text.see("end")

        self.after(120, self._drain_queue)


if __name__ == "__main__":
    app = LogViewer()
    app.mainloop()

import customtkinter as ctk
from tkinter import CENTER, messagebox
import threading
import requests


class ParameterPage(ctk.CTkFrame):
    def __init__(self, master):
        super().__init__(master)

        self.esp32_ip = "172.20.10.12"  # change if needed

        # ===== Title =====
        self.title_label = ctk.CTkLabel(
            self,
            text="AGV Parameters",
            font=ctk.CTkFont(size=15, weight="bold"),
            text_color="#FFFFFF"
        )
        self.title_label.place(relx=0.2, rely=0.12, anchor=CENTER)

        self.note_label = ctk.CTkLabel(
            self,
            text="Update Nano parameters",
            font=ctk.CTkFont(size=7),
            text_color="gray"
        )
        self.note_label.place(relx=0.2, rely=0.20, anchor=CENTER)

        # ===== Inputs =====
        self.form = ctk.CTkFrame(self, corner_radius=14)
        self.form.place(relx=0.7, rely=0.5, anchor=CENTER)

        # (label, key, placeholder)
        fields = [
            ("Base Speed", "base_speed", "20"),
            ("KP", "kp", "2"),
            ("KD", "kd", "14"),
            ("U-turn time (ms)", "time_uturn", "2000"),
            ("Turn kick time (ms)", "time_turn", "2000"),
            ("Straight before turn (ms)", "time_strght_bfr_turn", "3000"),
            ("Turn execute limit (ms)", "time_turn_execute", "6000"),
            ("Turn Speed", "turnSpeed", "20"),
        ]

        self.entries = {}
        for r, (label, key, ph) in enumerate(fields):
            ctk.CTkLabel(self.form, text=label, font=ctk.CTkFont(size=13)).grid(
                row=r, column=0, padx=(16, 10), pady=7, sticky="e"
            )
            e = ctk.CTkEntry(self.form, width=140, placeholder_text=ph)
            e.grid(row=r, column=1, padx=(10, 16), pady=7)
            self.entries[key] = e

        # ===== Buttons =====
        self.update_btn = ctk.CTkButton(
            self,
            text="Update Parameters",
            width=100,
            height=40,
            corner_radius=20,
            fg_color="#1E90FF",
            hover_color="#4682B4",
            font=ctk.CTkFont(size=12, weight="bold"),
            command=self.send_params
        )
        self.update_btn.place(relx=0.15, rely=0.6, anchor=CENTER)

        self.home_btn = ctk.CTkButton(
            self,
            text="Home",
            width=140,
            height=38,
            corner_radius=18,
            fg_color="#808080",
            hover_color="#696969",
            font=ctk.CTkFont(size=14, weight="bold"),
            command=self.go_home
        )
        self.home_btn.place(relx=0.15, rely=0.8, anchor=CENTER)

    def _as_int_str(self, s: str) -> str:
        s = (s or "").strip()
        if not s:
            return ""
        # keep simple: only allow ints; clamp in ESP32 anyway
        try:
            int(s)
            return s
        except Exception:
            return ""

    def send_params(self):
        params = {"mode": "Params", "id": "AGV1"}

        # only send fields that user filled (ESP32 uses defaults if missing)
        for k, e in self.entries.items():
            v = self._as_int_str(e.get())
            if v != "":
                params[k] = v

        url = f"http://{self.esp32_ip}/send"
        threading.Thread(target=self._send_request, args=(url, params), daemon=True).start()

    def _send_request(self, url, params):
        try:
            r = requests.get(url, params=params, timeout=3)
            if r.status_code == 200:
                messagebox.showinfo("Parameters", "Updated ✅")
            else:
                messagebox.showerror("Parameters", f"Failed: {r.status_code}\n{r.text}")
        except Exception as e:
            messagebox.showerror("Parameters", str(e))

    def go_home(self):
        self.master.show_home_page()
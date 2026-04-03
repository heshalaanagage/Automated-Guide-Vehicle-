import customtkinter as ctk
from tkinter import CENTER
import requests
import threading


class EmergencyPage(ctk.CTkFrame):
    def __init__(self, master):
        super().__init__(master)

        self.esp32_ip = "172.20.10.9"   # Your ESP32 IP
        self.connected = True

        # ======= Title =======
        self.title_label = ctk.CTkLabel(
            self,
            text="AGV System - Emergency Control",
            font=ctk.CTkFont(size=28, weight="bold"),
            text_color="#FFFFFF"
        )
        self.title_label.place(relx=0.5, rely=0.12, anchor=CENTER)

        self.mode_label = ctk.CTkLabel(
            self,
            text="Mode: Emergency",
            font=ctk.CTkFont(size=18, weight="bold"),
            text_color="#FF4500"
        )
        self.mode_label.place(relx=0.5, rely=0.23, anchor=CENTER)

        # ======= Buttons Layout =======
        self.grip_engage_btn = ctk.CTkButton(
            self, text="Gripper Engage",
            width=180, height=50,
            fg_color="#1E90FF", hover_color="#4682B4",
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            command=lambda: self.send_emergency_cmd("GRIP_ENGAGE")
        )
        self.grip_engage_btn.place(relx=0.33, rely=0.45, anchor=CENTER)

        self.grip_disengage_btn = ctk.CTkButton(
            self, text="Gripper Disengage",
            width=180, height=50,
            fg_color="#1E90FF", hover_color="#4682B4",
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            command=lambda: self.send_emergency_cmd("GRIP_DISENGAGE")
        )
        self.grip_disengage_btn.place(relx=0.67, rely=0.45, anchor=CENTER)

        self.agv_start_btn = ctk.CTkButton(
            self, text="AGV Start",
            width=180, height=50,
            fg_color="#32CD32", hover_color="#2E8B57",
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            command=lambda: self.send_emergency_cmd("AGV_START")
        )
        self.agv_start_btn.place(relx=0.33, rely=0.65, anchor=CENTER)

        self.agv_stop_btn = ctk.CTkButton(
            self, text="AGV Stop",
            width=180, height=50,
            fg_color="#DC143C", hover_color="#B22222",
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            command=lambda: self.send_emergency_cmd("AGV_STOP")
        )
        self.agv_stop_btn.place(relx=0.67, rely=0.65, anchor=CENTER)

        # ======= HOME button =======
        self.home_btn = ctk.CTkButton(
            self, text="Home",
            width=150, height=40,
            fg_color="#808080", hover_color="#696969",
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            command=self.go_home
        )
        self.home_btn.place(relx=0.5, rely=0.85, anchor=CENTER)

    # ===================== SEND CMD TO ESP32 =====================
    def send_emergency_cmd(self, command_type):
        if not self.connected:
            print("ESP NOT CONNECTED")
            return

        params = {
            "mode": "Emergency",
            "id": "AGV1",
            "command": command_type
        }

        url = f"http://{self.esp32_ip}/send"
        threading.Thread(target=self._send_request,
                         args=(url, params),
                         daemon=True).start()

    def _send_request(self, url, params):
        try:
            response = requests.get(url, params=params, timeout=3)
            print("Sent:", params)
            print("ESP Response:", response.text)
        except Exception as e:
            print("Failed:", e)

    # ====================== GO HOME ======================
    def go_home(self):
        self.master.show_home_page()

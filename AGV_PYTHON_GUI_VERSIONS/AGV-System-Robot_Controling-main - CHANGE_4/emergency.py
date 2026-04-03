import customtkinter as ctk
from tkinter import CENTER
import requests
import threading


class EmergencyPage(ctk.CTkFrame):
    def __init__(self, master):
        super().__init__(master)

        self.esp32_ip = "172.20.10.12"
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

        # ======= Buttons =======
        self.grip_engage_btn = ctk.CTkButton(
            self, text="Gripper Engage",
            width=180, height=50,
            command=lambda: self.send_emergency_cmd("GRIP_ENGAGE")
        )
        self.grip_engage_btn.place(relx=0.33, rely=0.45, anchor=CENTER)

        self.grip_disengage_btn = ctk.CTkButton(
            self, text="Gripper Disengage",
            width=180, height=50,
            command=lambda: self.send_emergency_cmd("GRIP_DISENGAGE")
        )
        self.grip_disengage_btn.place(relx=0.67, rely=0.45, anchor=CENTER)

        self.agv_start_btn = ctk.CTkButton(
            self, text="AGV Start",
            width=180, height=50,
            fg_color="#32CD32",
            command=lambda: self.send_emergency_cmd("AGV_START")
        )
        self.agv_start_btn.place(relx=0.33, rely=0.65, anchor=CENTER)

        self.agv_stop_btn = ctk.CTkButton(
            self, text="AGV Stop",
            width=180, height=50,
            fg_color="#DC143C",
            command=lambda: self.send_emergency_cmd("AGV_STOP")
        )
        self.agv_stop_btn.place(relx=0.67, rely=0.65, anchor=CENTER)

        # ======= PID TEXT BOXES =======
        self.base_speed_entry = ctk.CTkEntry(self, placeholder_text="Base Speed", width=120)
        self.base_speed_entry.place(relx=0.3, rely=0.78, anchor=CENTER)

        self.kp_entry = ctk.CTkEntry(self, placeholder_text="KP", width=100)
        self.kp_entry.place(relx=0.5, rely=0.78, anchor=CENTER)

        self.kd_entry = ctk.CTkEntry(self, placeholder_text="KD", width=100)
        self.kd_entry.place(relx=0.7, rely=0.78, anchor=CENTER)

        self.update_pid_btn = ctk.CTkButton(
            self, text="Update PID",
            width=150,
            fg_color="#FFA500",
            command=self.update_pid_values
        )
        self.update_pid_btn.place(relx=0.5, rely=0.88, anchor=CENTER)

        # ======= HOME =======
        self.home_btn = ctk.CTkButton(
            self, text="Home",
            width=120,
            command=self.go_home
        )
        self.home_btn.place(relx=0.4, rely=0.3)

    # ================= SEND NORMAL COMMAND =================
    def send_emergency_cmd(self, command_type):

        params = {
            "mode": "Emergency",
            "id": "AGV1",
            "command": command_type
        }

        url = f"http://{self.esp32_ip}/send"

        threading.Thread(
            target=self._send_request,
            args=(url, params),
            daemon=True
        ).start()

    # ================= SEND PID UPDATE =================
    def update_pid_values(self):

        base = self.base_speed_entry.get()
        kp = self.kp_entry.get()
        kd = self.kd_entry.get()

        if base and kp and kd:

            params = {
                "mode": "Emergency",
                "id": "AGV1",
                "command": "UPDATE_PID",
                "base": base,
                "kp": kp,
                "kd": kd
            }

            url = f"http://{self.esp32_ip}/send"

            threading.Thread(
                target=self._send_request,
                args=(url, params),
                daemon=True
            ).start()

    def _send_request(self, url, params):
        try:
            response = requests.get(url, params=params, timeout=3)
            print("Sent:", params)
            print("ESP Response:", response.text)
        except Exception as e:
            print("Failed:", e)

    def go_home(self):
        self.master.show_home_page()


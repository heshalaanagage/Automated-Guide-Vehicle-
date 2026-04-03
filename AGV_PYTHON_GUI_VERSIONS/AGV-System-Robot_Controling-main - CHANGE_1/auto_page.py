import customtkinter as ctk
from tkinter import CENTER
import requests
import threading

class AutoPage(ctk.CTkFrame):
    def __init__(self, master):
        super().__init__(master)

        # ======== Title and Labels ========
        self.title_label = ctk.CTkLabel(
            self, text="AGV System",
            font=ctk.CTkFont(size=28, weight="bold"),
            text_color="#FFFFFF"
        )
        self.title_label.place(relx=0.5, rely=0.12, anchor=CENTER)

        self.mode_label = ctk.CTkLabel(
            self, text="Mode: Auto",
            font=ctk.CTkFont(size=18, weight="bold"),
            text_color="#00BFFF"
        )
        self.mode_label.place(relx=0.5, rely=0.25, anchor=CENTER)

        self.battery_label = ctk.CTkLabel(
            self, text="Battery: 85%",
            font=ctk.CTkFont(size=18),
            text_color="#7CFC00"
        )
        self.battery_label.place(relx=0.5, rely=0.35, anchor=CENTER)

        # ======== Entry Boxes ========
        self.wait_label = ctk.CTkLabel(
            self, text="Waiting Time (minutes):", 
            font=ctk.CTkFont(size=14)
        )
        self.wait_label.place(relx=0.35, rely=0.5, anchor=CENTER)
        self.wait_entry = ctk.CTkEntry(self, width=100, placeholder_text="0")
        self.wait_entry.place(relx=0.65, rely=0.5, anchor=CENTER)

        self.loop_label = ctk.CTkLabel(
            self, text="Loop Count:", 
            font=ctk.CTkFont(size=14)
        )
        self.loop_label.place(relx=0.35, rely=0.6, anchor=CENTER)
        self.loop_entry = ctk.CTkEntry(self, width=100, placeholder_text="0")
        self.loop_entry.place(relx=0.65, rely=0.6, anchor=CENTER)

        # ======== Buttons ========
        self.start_btn = ctk.CTkButton(
            self, text="Start",
            width=150, height=40,
            fg_color="#1E90FF", hover_color="#4682B4",
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            command=self.send_data_to_esp
        )
        self.start_btn.place(relx=0.38, rely=0.8, anchor=CENTER)

        self.home_btn = ctk.CTkButton(
            self, text="Home",
            width=150, height=40,
            fg_color="#808080", hover_color="#696969",
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            command=self.go_home
        )
        self.home_btn.place(relx=0.62, rely=0.8, anchor=CENTER)

        # ======== ESP32 IP CONFIG ========
        self.esp32_ip = "172.20.10.9"  # 🔧 Change to your ESP32 IP
        self.connected = True

    # ================== SEND DATA TO ESP32 ==================
    def send_data_to_esp(self):
        if not self.connected:
            print("ESP Disconnected")
            return

        wait_time = self.wait_entry.get()
        loop_count = self.loop_entry.get()
        identifier = "AGV1"

        params = {
            "mode": "Auto",           # ✅ Required for ESP32
            "id": identifier,
            "wait_time": wait_time,
            "loop_count": loop_count
        }

        url = f"http://{self.esp32_ip}/send"
        threading.Thread(target=self._send_request, args=(url, params), daemon=True).start()

    def _send_request(self, url, params):
        try:
            response = requests.get(url, params=params, timeout=3)
            print("Sent to ESP32:", params)
            print("ESP32 Response:", response.text)
        except requests.RequestException as e:
            print("Failed to send to ESP32:", e)

    # ================== GO HOME ==================
    def go_home(self):
        if hasattr(self.master, "show_home_page"):
            self.master.show_home_page()
        else:
            print("Home page function not found.")

import customtkinter as ctk
from tkinter import CENTER
import threading
import requests
import time

class HomePage(ctk.CTkFrame):
    def __init__(self, master):
        super().__init__(master)

        # ======== Status Label ========
        self.status_label = ctk.CTkLabel(
            self,
            text="Status: Disconnected",
            text_color="red",
            font=ctk.CTkFont(size=14, weight="bold")
        )
        self.status_label.place(relx=0.95, rely=0.05, anchor="ne")

        # ======== Title ========
        self.title_label = ctk.CTkLabel(
            self,
            text="AGV System",
            font=ctk.CTkFont(size=36, weight="bold"),
            text_color="#FFFFFF"
        )
        self.title_label.place(relx=0.5, rely=0.35, anchor=CENTER)

        # ======== Auto Button ========
        self.auto_btn = ctk.CTkButton(
            self,
            text="Auto Mode",
            width=160,
            height=45,
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            fg_color="#1E90FF",
            hover_color="#4682B4",
            command=self.open_auto
        )
        self.auto_btn.place(relx=0.38, rely=0.55, anchor=CENTER)

        # ======== Manual Button ========
        self.manual_btn = ctk.CTkButton(
            self,
            text="Manual Mode",
            width=160,
            height=45,
            corner_radius=20,
            font=ctk.CTkFont(size=16, weight="bold"),
            fg_color="#32CD32",
            hover_color="#2E8B57",
            command=self.open_manual
        )
        self.manual_btn.place(relx=0.62, rely=0.55, anchor=CENTER)

        # ======== Footer ========
        self.footer_label = ctk.CTkLabel(
            self,
            text="Created by Heshala Angage | © All Rights Reserved",
            font=ctk.CTkFont(size=12),
            text_color="gray"
        )
        self.footer_label.place(relx=0.5, rely=0.95, anchor=CENTER)

        # ======== ESP32 Wi-Fi Config ========
        self.esp32_ip = "172.20.10.9"  # 🔧 Change this to your ESP32 IP
        self.keep_checking = True

        # ======== Start Wi-Fi Thread ========
        threading.Thread(target=self.check_esp32_status, daemon=True).start()

    # ======== Poll ESP32 via Wi-Fi ========
    def check_esp32_status(self):
        while self.keep_checking:
            try:
                url = f"http://{self.esp32_ip}/"
                response = requests.get(url, timeout=2)
                if response.text.strip() == "Connected":
                    self.update_status(True)
                else:
                    self.update_status(False)
            except requests.RequestException:
                self.update_status(False)

            time.sleep(3)  # Check every 3 seconds

    # ======== Update GUI Label ========
    def update_status(self, connected):
        color = "#00FF7F" if connected else "red"
        text = "Status: Connected" if connected else "Status: Disconnected"
        self.status_label.configure(text=text, text_color=color)

    # ======== Navigation Functions ========
    def open_auto(self):
        self.master.show_auto_page()

    def open_manual(self):
        self.master.show_manual_page()

    def destroy(self):
        self.keep_checking = False
        super().destroy()

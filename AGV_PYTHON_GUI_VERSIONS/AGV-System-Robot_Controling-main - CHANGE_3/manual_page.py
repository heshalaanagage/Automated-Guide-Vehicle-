import customtkinter as ctk
from tkinter import CENTER
import requests
import threading

class ManualPage(ctk.CTkFrame):
    def __init__(self, master):
        super().__init__(master)
        self.esp32_ip = "172.20.10.12"  # ESP32 IP
        self.connected = True

        # ===== Title & Battery =====
        self.title_label = ctk.CTkLabel(self, text="AGV System", font=ctk.CTkFont(size=28, weight="bold"), text_color="#FFFFFF")
        self.title_label.place(relx=0.5, rely=0.12, anchor=CENTER)
        self.battery_label = ctk.CTkLabel(self, text="Battery: 80%", font=ctk.CTkFont(size=18), text_color="#7CFC00")
        self.battery_label.place(relx=0.5, rely=0.25, anchor=CENTER)

        # Main Buttons
        self.charge_btn = ctk.CTkButton(self, text="Charge", width=160, height=45, corner_radius=20,
                                        font=ctk.CTkFont(size=16, weight="bold"), fg_color="#FFA500", hover_color="#FF8C00",
                                        command=self.show_charge_section)
        self.charge_btn.place(relx=0.38, rely=0.38, anchor=CENTER)

        self.trip_btn = ctk.CTkButton(self, text="Trip", width=160, height=45, corner_radius=20,
                                      font=ctk.CTkFont(size=16, weight="bold"), fg_color="#1E90FF", hover_color="#4682B4",
                                      command=self.show_trip_section)
        self.trip_btn.place(relx=0.62, rely=0.38, anchor=CENTER)

        self.section_frame = ctk.CTkFrame(self, fg_color="transparent")
        self.section_frame.place(relx=0.5, rely=0.7, anchor=CENTER)

    # ================== CHARGE SECTION ==================
    def show_charge_section(self):
        self.clear_section()
        label = ctk.CTkLabel(self.section_frame, text="Charging Time (minutes):", font=ctk.CTkFont(size=14))
        label.grid(row=0, column=0, padx=10, pady=5, sticky="e")
        self.charge_entry = ctk.CTkEntry(self.section_frame, width=100, placeholder_text="0")
        self.charge_entry.grid(row=0, column=1, padx=10, pady=5)

        disconnect_btn = ctk.CTkButton(self.section_frame, text="Disconnect", width=120, height=35,
                                       corner_radius=15, fg_color="#DC143C", hover_color="#B22222",
                                       command=self.disconnect_esp)
        disconnect_btn.grid(row=1, column=0, columnspan=2, pady=10)

        self.show_bottom_buttons(["Home", "Start"], mode="Charge")

    # ================== TRIP SECTION ==================
    def show_trip_section(self):
        self.clear_section()
        label1 = ctk.CTkLabel(self.section_frame, text="Drop Location:", font=ctk.CTkFont(size=14))
        label1.grid(row=0, column=0, padx=10, pady=5, sticky="e")
        self.drop_option = ctk.CTkOptionMenu(self.section_frame, values=["A", "B", "C"])
        self.drop_option.grid(row=0, column=1, padx=10, pady=5)

        label2 = ctk.CTkLabel(self.section_frame, text="Waiting Time (minutes):", font=ctk.CTkFont(size=14))
        label2.grid(row=1, column=0, padx=10, pady=5, sticky="e")
        self.wait_entry = ctk.CTkEntry(self.section_frame, width=100, placeholder_text="0")
        self.wait_entry.grid(row=1, column=1, padx=10, pady=5)

        label3 = ctk.CTkLabel(self.section_frame, text="Loop Count:", font=ctk.CTkFont(size=14))
        label3.grid(row=2, column=0, padx=10, pady=5, sticky="e")
        self.loop_entry = ctk.CTkEntry(self.section_frame, width=100, placeholder_text="0")
        self.loop_entry.grid(row=2, column=1, padx=10, pady=5)

        self.show_bottom_buttons(["Home", "Start", "Stop"], mode="Trip")

    # ================== UTILITIES ==================
    def clear_section(self):
        for widget in self.section_frame.winfo_children():
            widget.destroy()

    def show_bottom_buttons(self, labels, mode=None):
        frame = ctk.CTkFrame(self.section_frame, fg_color="transparent")
        frame.grid(row=10, column=0, columnspan=2, pady=(15,0))
        for i, name in enumerate(labels):
            if name == "Start":
                btn = ctk.CTkButton(frame, text=name, width=120, height=35, corner_radius=15,
                                    fg_color="#1E90FF", hover_color="#4682B4",
                                    command=lambda m=mode: self.send_manual_data(m))
            elif name == "Stop" or name == "Disconnect":
                continue
            else:
                btn = ctk.CTkButton(frame, text=name, width=120, height=35, corner_radius=15,
                                    fg_color="#808080", hover_color="#696969", command=self.go_home)
            btn.grid(row=0, column=i, padx=10)

    def send_manual_data(self, mode):
        if not self.connected:
            print("ESP Disconnected")
            return
        try:
            params = {"mode": "Manual", "id": "AGV1"}
            if mode == "Charge":
                params["type"] = "Charge"
                params["charge_time"] = self.charge_entry.get()
            elif mode == "Trip":
                params["type"] = "Trip"
                params["drop_location"] = self.drop_option.get()
                params["wait_time"] = self.wait_entry.get()
                params["loop_count"] = self.loop_entry.get()
            url = f"http://{self.esp32_ip}/send"
            threading.Thread(target=self._send_request, args=(url, params), daemon=True).start()
        except Exception as e:
            print("Send Error:", e)

    def _send_request(self, url, params):
        try:
            response = requests.get(url, params=params, timeout=3)
            print("Sent to ESP32:", params)
            print("ESP32 Response:", response.text)
        except Exception as e:
            print("Failed to send:", e)

    def disconnect_esp(self):
        try:
            url = f"http://{self.esp32_ip}/disconnect"
            response = requests.get(url, timeout=2)
            self.connected = False
            print("ESP32 Disconnected:", response.text)
        except Exception as e:
            print("Disconnect Error:", e)

    def go_home(self):
        self.master.show_home_page()

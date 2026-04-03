import customtkinter as ctk
from home_page import HomePage
from auto_page import AutoPage
from manual_page import ManualPage  # <-- Add this line

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

class MainApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("AGV Control System")
        self.geometry("700x400")
        self.resizable(False, False)
        self.eval('tk::PlaceWindow . center')

        self.current_frame = None
        self.show_home_page()

    def clear_frame(self):
        if self.current_frame is not None:
            self.current_frame.destroy()

    def show_home_page(self):
        self.clear_frame()
        self.current_frame = HomePage(self)
        self.current_frame.pack(fill="both", expand=True)

    def show_auto_page(self):
        self.clear_frame()
        self.current_frame = AutoPage(self)
        self.current_frame.pack(fill="both", expand=True)

    def show_manual_page(self):  # <-- Add this
        self.clear_frame()
        self.current_frame = ManualPage(self)
        self.current_frame.pack(fill="both", expand=True)


if __name__ == "__main__":
    app = MainApp()
    app.mainloop()

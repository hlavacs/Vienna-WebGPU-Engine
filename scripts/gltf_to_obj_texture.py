from PIL import Image
import os
import tkinter as tk
from tkinter import filedialog

def split_metallic_roughness():
    # Open file dialog
    root = tk.Tk()
    root.withdraw()  # Hide main window
    file_path = filedialog.askopenfilename(
        title="Select Metallic-Roughness Texture",
        filetypes=[("PNG Images", "*.png"), ("All Files", "*.*")]
    )
    if not file_path:
        print("No file selected.")
        return

    # Load image
    img = Image.open(file_path)
    if img.mode != 'RGB':
        img = img.convert('RGB')

    r, g, b = img.split()

    folder, filename = os.path.split(file_path)
    name, ext = os.path.splitext(filename)

    # Save AO (R), Roughness (G), Metallic (B)
    ao_path = os.path.join(folder, f"{name}_AO.png")
    roughness_path = os.path.join(folder, f"{name}_Roughness.png")
    metallic_path = os.path.join(folder, f"{name}_Metallic.png")

    r.save(ao_path)
    g.save(roughness_path)
    b.save(metallic_path)

    print(f"Saved:\n  AO: {ao_path}\n  Roughness: {roughness_path}\n  Metallic: {metallic_path}")

if __name__ == "__main__":
    split_metallic_roughness()

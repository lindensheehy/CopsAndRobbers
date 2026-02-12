import tkinter as tk
from tkinter import filedialog, messagebox
import os
import subprocess
import sys

# --- CONFIGURATION ---
# Define the argument requirements for each tool.
# Format: "filename.py": [("Label", "FileTypes"), ...]
# FileTypes is a tuple like (("Text Files", "*.txt"), ("All Files", "*.*"))
TOOL_CONFIG = {
    "edge_builder.py": [
        ("Adjacency Matrix", "*.txt")
    ],
    "edge_parser.py": [
        ("Edge List", "*.txt")
    ],
    "generator.py": [],  # No args
    "pos_builder.py": [
        ("Graph Image", "*.png")
    ],
    "verifier.py": [
        ("Adjacency Matrix", "*.txt"),
        ("Degrees List", "*.txt")
    ],
    "visualizer.py": [
        ("Adjacency Matrix", "*.txt"),
        ("Position List (Optional)", "*.txt") 
    ]
}

DEFAULT_ASSET_DIR = "assets"

class MasterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Cops & Robbers Tool Suite")
        self.root.geometry("600x700")

        # --- Top Section: File List ---
        tk.Label(root, text="Select a Tool", font=("Arial", 12, "bold")).pack(pady=(10, 5))
        
        self.tool_list = tk.Listbox(root, selectmode=tk.SINGLE, font=("Courier", 10), height=15)
        self.tool_list.pack(fill=tk.BOTH, expand=True, padx=20, pady=5)
        self.tool_list.bind('<<ListboxSelect>>', self.on_tool_selected)
        
        # --- Middle Section: Argument Inputs ---
        self.arg_frame = tk.LabelFrame(root, text="Arguments", padx=10, pady=10)
        self.arg_frame.pack(fill=tk.X, padx=20, pady=10)
        
        # We will dynamically add widgets to this list
        self.arg_entries = [] 

        # --- Bottom Section: Buttons ---
        btn_frame = tk.Frame(root)
        btn_frame.pack(pady=20)

        self.btn_refresh = tk.Button(btn_frame, text="Refresh Tools", command=self.scan_directory)
        self.btn_refresh.pack(side=tk.LEFT, padx=10)

        self.btn_run = tk.Button(btn_frame, text="RUN TOOL", command=self.run_tool, bg="#4CAF50", fg="white", font=("Arial", 10, "bold"))
        self.btn_run.pack(side=tk.LEFT, padx=10)

        # Initial Scan
        self.scan_directory()

    def scan_directory(self):
        """Scans for .py files recursively."""
        self.tool_list.delete(0, tk.END)
        root_dir = os.path.dirname(os.path.abspath(__file__))
        current_script = os.path.basename(__file__)
        
        found_files = []
        for dirpath, _, filenames in os.walk(root_dir):
            for f in filenames:
                if f.endswith('.py') and f != current_script:
                    rel_path = os.path.relpath(os.path.join(dirpath, f), root_dir)
                    found_files.append(rel_path.replace("\\", "/"))
        
        found_files.sort()
        for f in found_files:
            self.tool_list.insert(tk.END, f)

    def on_tool_selected(self, event):
        """Updates the Argument Frame based on the selected tool."""
        # Clear previous inputs
        for widget in self.arg_frame.winfo_children():
            widget.destroy()
        self.arg_entries = []

        selection = self.tool_list.curselection()
        if not selection:
            return

        tool_path = self.tool_list.get(selection[0])
        tool_name = os.path.basename(tool_path)

        # Check configuration
        if tool_name in TOOL_CONFIG:
            args_needed = TOOL_CONFIG[tool_name]
            
            if not args_needed:
                tk.Label(self.arg_frame, text="No arguments required.", fg="gray").pack()
            
            for i, (label_text, file_type) in enumerate(args_needed):
                # Row Container
                row = tk.Frame(self.arg_frame)
                row.pack(fill=tk.X, pady=2)
                
                # Label
                tk.Label(row, text=label_text, width=20, anchor='w').pack(side=tk.LEFT)
                
                # Entry (for path)
                entry = tk.Entry(row)
                entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
                self.arg_entries.append(entry)
                
                # Browse Button
                # We use a lambda with default arg to capture the specific entry/file_type for this loop iteration
                btn = tk.Button(row, text="Browse...", 
                                command=lambda e=entry, ft=file_type: self.browse_file(e, ft))
                btn.pack(side=tk.RIGHT)
        else:
            tk.Label(self.arg_frame, text="Unknown tool. Running without arguments.", fg="orange").pack()

    def browse_file(self, entry_widget, file_pattern):
        """Opens a file dialog and inserts result into the entry widget."""
        # Determine initial directory (assets if exists, else current)
        init_dir = DEFAULT_ASSET_DIR if os.path.exists(DEFAULT_ASSET_DIR) else "."
        
        filename = filedialog.askopenfilename(
            initialdir=init_dir,
            title="Select File",
            filetypes=[("Allowed Files", file_pattern), ("All Files", "*.*")]
        )
        
        if filename:
            # Make path relative if possible for cleaner look
            try:
                rel_path = os.path.relpath(filename, ".")
                entry_widget.delete(0, tk.END)
                entry_widget.insert(0, rel_path)
            except:
                entry_widget.delete(0, tk.END)
                entry_widget.insert(0, filename)

    def run_tool(self):
        selection = self.tool_list.curselection()
        if not selection:
            messagebox.showwarning("Warning", "Please select a tool first.")
            return

        tool_rel_path = self.tool_list.get(selection[0])
        
        # Collect arguments from entries
        args = []
        for entry in self.arg_entries:
            val = entry.get().strip()
            if val:
                args.append(val)
        
        # Construct command
        cmd = [sys.executable, tool_rel_path] + args
        
        print(f"Executing: {' '.join(cmd)}")
        
        try:
            # Run!
            subprocess.Popen(cmd)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to launch tool:\n{e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = MasterApp(root)
    root.mainloop()
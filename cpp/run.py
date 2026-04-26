import tkinter as tk
from tkinter import filedialog, messagebox
import os
import subprocess
import time
import threading
import json
from pathlib import Path

# --- CONFIGURATION ---
DEFAULT_ASSET_DIR = "../assets"
INPUT_CACHE_FILE = os.path.join(DEFAULT_ASSET_DIR, "master_cache.json")

class MasterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Cops & Robbers Tool Suite")
        self.root.geometry("650x750")
        
        # Ensure working directory is set to this script's location
        os.chdir(os.path.dirname(os.path.abspath(__file__)))
        
        # Load user input cache
        self.input_cache = self.load_input_cache()

        # --- Top Section: File List ---
        tk.Label(root, text="Select an Executable", font=("Arial", 12, "bold")).pack(pady=(10, 5))
        
        self.tool_list = tk.Listbox(root, selectmode=tk.SINGLE, font=("Courier", 10), height=15)
        self.tool_list.pack(fill=tk.BOTH, expand=True, padx=20, pady=5)
        self.tool_list.bind('<<ListboxSelect>>', self.on_tool_selected)
        
        # --- Middle Section: Argument Inputs ---
        self.arg_frame = tk.LabelFrame(root, text="Arguments", padx=10, pady=10)
        self.arg_frame.pack(fill=tk.X, padx=20, pady=10)
        
        self.graph_entry = None
        self.cops_entry = None

        # --- Bottom Section: Buttons ---
        btn_frame = tk.Frame(root)
        btn_frame.pack(pady=20)

        self.btn_refresh = tk.Button(btn_frame, text="Refresh Tools", command=self.scan_directory)
        self.btn_refresh.pack(side=tk.LEFT, padx=10)

        self.btn_run = tk.Button(btn_frame, text="RUN TOOL", command=self.run_tool, bg="#4CAF50", fg="white", font=("Arial", 10, "bold"))
        self.btn_run.pack(side=tk.LEFT, padx=10)

        # Initial Scan
        self.scan_directory()

    def load_input_cache(self):
        if os.path.exists(INPUT_CACHE_FILE):
            try:
                with open(INPUT_CACHE_FILE, "r") as f:
                    return json.load(f)
            except Exception as e:
                print(f"Warning: Could not load input cache ({e}). Starting fresh.")
        return {}

    def save_input_cache(self):
        os.makedirs(os.path.dirname(INPUT_CACHE_FILE), exist_ok=True)
        with open(INPUT_CACHE_FILE, "w") as f:
            json.dump(self.input_cache, f, indent=4)

    def scan_directory(self):
        """Scans strictly for .exe files to populate the UI."""
        self.tool_list.delete(0, tk.END)
        
        # Look in build/bin first, fallback to current dir
        bin_dir = os.path.abspath("build/bin")
        search_dir = bin_dir if os.path.exists(bin_dir) else os.path.abspath(".")
        
        found_files = []
        for dirpath, _, filenames in os.walk(search_dir):
            for f in filenames:
                if f.endswith('.exe'):
                    rel_path = os.path.relpath(os.path.join(dirpath, f), os.path.abspath("."))
                    found_files.append(rel_path.replace("\\", "/"))
        
        found_files.sort()
        for f in found_files:
            self.tool_list.insert(tk.END, f)

    def on_tool_selected(self, event):
        """Updates the Argument Frame based on the selected tool."""
        for widget in self.arg_frame.winfo_children():
            widget.destroy()

        selection = self.tool_list.curselection()
        if not selection: return

        tool_path = self.tool_list.get(selection[0])
        tool_name = os.path.basename(tool_path)

        # Retrieve cached values or set defaults
        cached_graph = self.input_cache.get(tool_name, {}).get("Graph File", "")
        cached_cops = self.input_cache.get(tool_name, {}).get("Number of Cops", "1")

        # -- Graph File Row --
        row1 = tk.Frame(self.arg_frame)
        row1.pack(fill=tk.X, pady=4)
        tk.Label(row1, text="Graph File", width=15, anchor='w').pack(side=tk.LEFT)
        
        self.graph_entry = tk.Entry(row1)
        if cached_graph:
            self.graph_entry.insert(0, cached_graph)
        self.graph_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        btn = tk.Button(row1, text="Browse...", command=lambda: self.browse_file(self.graph_entry, "*.txt", tool_name, "Graph File"))
        btn.pack(side=tk.RIGHT)

        # -- Number of Cops Row --
        row2 = tk.Frame(self.arg_frame)
        row2.pack(fill=tk.X, pady=4)
        tk.Label(row2, text="Number of Cops", width=15, anchor='w').pack(side=tk.LEFT)
        
        self.cops_entry = tk.Spinbox(row2, from_=1, to=20, width=5)
        self.cops_entry.delete(0, "end")
        self.cops_entry.insert(0, cached_cops)
        self.cops_entry.pack(side=tk.LEFT, padx=5)

    def browse_file(self, entry_widget, file_pattern, tool_name, label_text):
        init_dir = DEFAULT_ASSET_DIR if os.path.exists(DEFAULT_ASSET_DIR) else "."
        
        filename = filedialog.askopenfilename(
            initialdir=init_dir,
            title="Select File",
            filetypes=[("Graph Files", file_pattern), ("All Files", "*.*")]
        )
        
        if filename:
            try:
                rel_path = os.path.relpath(filename, ".")
                final_path = rel_path
            except:
                final_path = filename

            entry_widget.delete(0, tk.END)
            entry_widget.insert(0, final_path)

            if tool_name not in self.input_cache:
                self.input_cache[tool_name] = {}
            self.input_cache[tool_name][label_text] = final_path
            self.save_input_cache()

    def run_tool(self):
        selection = self.tool_list.curselection()
        if not selection:
            messagebox.showwarning("Warning", "Please select a tool first.")
            return

        tool_rel_path = self.tool_list.get(selection[0])
        tool_name = os.path.basename(tool_rel_path)
        
        graph_val = self.graph_entry.get().strip()
        cops_val = self.cops_entry.get().strip()

        if not graph_val or not cops_val:
            messagebox.showwarning("Warning", "Both Graph File and Number of Cops are required.")
            return
            
        # Save exact current inputs to cache
        if tool_name not in self.input_cache:
            self.input_cache[tool_name] = {}
        self.input_cache[tool_name]["Graph File"] = graph_val
        self.input_cache[tool_name]["Number of Cops"] = cops_val
        self.save_input_cache()
        
        # Hardcoded argument structure
        cmd = [tool_rel_path, graph_val, cops_val]
        
        print(f"\nExecuting: {' '.join(cmd)}")
        
        def execute_and_time():
            start_time = time.time()
            try:
                subprocess.run(cmd)
                elapsed = time.time() - start_time
                print(f"--- {tool_name} finished in {elapsed:.4f} seconds ---\n")
            except Exception as e:
                self.root.after(0, lambda: messagebox.showerror("Error", f"Failed to launch tool:\n{e}"))

        threading.Thread(target=execute_and_time, daemon=True).start()

if __name__ == "__main__":
    root = tk.Tk()
    app = MasterApp(root)
    root.mainloop()
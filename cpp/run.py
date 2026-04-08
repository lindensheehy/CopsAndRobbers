import tkinter as tk
from tkinter import filedialog, messagebox
import os
import subprocess
import sys
import time
import threading
import json
import re
from pathlib import Path

# --- CONFIGURATION ---
DEFAULT_ASSET_DIR = "../assets"
INPUT_CACHE_FILE = os.path.join(DEFAULT_ASSET_DIR, "master_cache.json")
PARSER_CACHE_FILE = os.path.join(DEFAULT_ASSET_DIR, "parser_cache.json")

def auto_discover_cpp_args(solvers_dir="solvers"):
    """
    Scans the C++ solver files, checks the cache based on modification time,
    parses out required arguments via regex, and builds the UI configuration.
    """
    dynamic_config = {}
    solvers_path = Path(solvers_dir)
    
    if not solvers_path.exists():
        return dynamic_config

    # 1. Load the existing parser cache
    parser_cache = {}
    if os.path.exists(PARSER_CACHE_FILE):
        try:
            with open(PARSER_CACHE_FILE, "r") as f:
                parser_cache = json.load(f)
        except Exception:
            parser_cache = {}

    cache_updated = False
    usage_pattern = re.compile(r'argv\[0\]\s*<<\s*"([^"]+)"')
    
    # 2. Iterate through solvers and check against cache
    for cpp_file in solvers_path.glob("*.cpp"):
        exe_name = f"{cpp_file.stem}.exe"
        mtime = cpp_file.stat().st_mtime
        
        # Cache Hit: File hasn't changed since last parse
        if exe_name in parser_cache and parser_cache[exe_name].get("mtime") == mtime:
            dynamic_config[exe_name] = parser_cache[exe_name].get("args", [])
            continue
            
        # Cache Miss: Parse the file
        try:
            content = cpp_file.read_text(encoding='utf-8')
            match = usage_pattern.search(content)
            
            args = []
            if match:
                usage_str = match.group(1)
                tokens = re.findall(r'<([^>]+)>', usage_str)
                
                for token in tokens:
                    token_lower = token.lower()
                    if ".txt" in token_lower or "graph" in token_lower:
                        args.append(("Graph File", "*.txt"))
                    elif "cop" in token_lower:
                        args.append(("Number of Cops", "int"))
                    elif "ticket" in token_lower:
                        args.append(("Max Tickets", "int"))
                    else:
                        clean_token = token.replace("_", " ").title()
                        args.append((clean_token, "any"))
            
            # Update cache dictionary
            dynamic_config[exe_name] = args
            parser_cache[exe_name] = {"mtime": mtime, "args": args}
            cache_updated = True
            
        except Exception as e:
            print(f"Warning: Failed to parse {cpp_file.name} for arguments. ({e})")
            dynamic_config[exe_name] = []
            
    # 3. Save cache to disk if any new files were parsed
    if cache_updated:
        os.makedirs(os.path.dirname(PARSER_CACHE_FILE), exist_ok=True)
        with open(PARSER_CACHE_FILE, "w") as f:
            json.dump(parser_cache, f, indent=4)
            
    return dynamic_config

class MasterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Cops & Robbers Tool Suite")
        self.root.geometry("650x750")
        
        # Ensure working directory is set to this script's location
        os.chdir(os.path.dirname(os.path.abspath(__file__)))
        
        # Dynamically discover configurations
        self.exe_config = auto_discover_cpp_args()
        
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
        root_dir = os.path.abspath(".")
        
        # Re-run auto discovery in case you added a new .cpp file or modified one
        self.exe_config = auto_discover_cpp_args()
        
        found_files = []
        for dirpath, _, filenames in os.walk(root_dir):
            for f in filenames:
                if f.endswith('.exe'):
                    rel_path = os.path.relpath(os.path.join(dirpath, f), root_dir)
                    found_files.append(rel_path.replace("\\", "/"))
        
        found_files.sort()
        for f in found_files:
            self.tool_list.insert(tk.END, f)

    def on_tool_selected(self, event):
        """Updates the Argument Frame based on the selected tool."""
        for widget in self.arg_frame.winfo_children():
            widget.destroy()
        self.arg_entries = []

        selection = self.tool_list.curselection()
        if not selection: return

        tool_path = self.tool_list.get(selection[0])
        tool_name = os.path.basename(tool_path)

        if tool_name in self.exe_config:
            args_needed = self.exe_config[tool_name]
            
            if not args_needed:
                tk.Label(self.arg_frame, text="No arguments required.", fg="gray").pack()
            
            for i, (label_text, arg_type) in enumerate(args_needed):
                cached_val = self.input_cache.get(tool_name, {}).get(label_text, "")

                row = tk.Frame(self.arg_frame)
                row.pack(fill=tk.X, pady=4)
                
                tk.Label(row, text=label_text, width=20, anchor='w').pack(side=tk.LEFT)
                
                if arg_type == "int":
                    entry = tk.Spinbox(row, from_=1, to=10, width=5)
                    if cached_val:
                        entry.delete(0, "end")
                        entry.insert(0, cached_val)
                    entry.pack(side=tk.LEFT, padx=5)
                    self.arg_entries.append((entry, label_text))
                    tk.Label(row, text="(Integer)", fg="gray", font=("Arial", 8)).pack(side=tk.LEFT)
                    
                else:
                    entry = tk.Entry(row)
                    if cached_val:
                        entry.insert(0, cached_val)
                    entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
                    self.arg_entries.append((entry, label_text))
                    
                    btn = tk.Button(row, text="Browse...", 
                                    command=lambda e=entry, ft=arg_type, tn=tool_name, lt=label_text: self.browse_file(e, ft, tn, lt))
                    btn.pack(side=tk.RIGHT)
        else:
            tk.Label(self.arg_frame, text="No parsed configuration. Running without arguments.", fg="orange").pack()

    def browse_file(self, entry_widget, file_pattern, tool_name, label_text):
        init_dir = DEFAULT_ASSET_DIR if os.path.exists(DEFAULT_ASSET_DIR) else "."
        
        filename = filedialog.askopenfilename(
            initialdir=init_dir,
            title="Select File",
            filetypes=[("Allowed Files", file_pattern), ("All Files", "*.*")]
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
        
        args = []
        for entry, label in self.arg_entries:
            val = entry.get().strip()
            if val:
                args.append(val)
                if tool_name not in self.input_cache:
                    self.input_cache[tool_name] = {}
                self.input_cache[tool_name][label] = val
        
        self.save_input_cache()
        
        # All tools are now assumed to be executables
        cmd = [tool_rel_path] + args
        
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
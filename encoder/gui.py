import argparse
import contextlib
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Open the Q-HUFDEL graphical package generator."
    )
    return parser.parse_args()


class ProcessDialog(tk.Toplevel):
    def __init__(self, master: tk.Tk):
        super().__init__(master)
        self.title("Q-HUFDEL Packager")
        self.geometry("760x420")
        self.transient(master)
        self.protocol("WM_DELETE_WINDOW", lambda: None)

        self.output = tk.Text(self, wrap="none", state="disabled")
        scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.output.yview)
        self.output.configure(yscrollcommand=scrollbar.set)
        self.output.grid(row=0, column=0, sticky="nsew")
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.close_button = ttk.Button(self, text="Close", command=self.destroy)
        self.close_button.grid(row=1, column=0, columnspan=2, pady=6)
        self.close_button.state(["disabled"])
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)

    def append(self, text: str):
        self.output.configure(state="normal")
        self.output.insert("end", text)
        self.output.see("end")
        self.output.configure(state="disabled")

    def finish(self):
        self.protocol("WM_DELETE_WINDOW", self.destroy)
        self.close_button.state(["!disabled"])


class QueueWriter:
    def __init__(self, output_queue: queue.Queue):
        self.output_queue = output_queue

    def write(self, text: str):
        if text:
            self.output_queue.put(text)

    def flush(self):
        pass


def run_backend(
    name: str,
    file1_path: str,
    file1_version: int,
    file2_path: str,
    file2_version: int,
    output_dir: str,
    merge_from: int = None,
    merge_to: int = None,
    generate_json_report: bool = False,
) -> str:
    from do_compress import do_compress

    output_filename = f"{name}_{file1_version}_{file2_version}.bin"
    return do_compress(
        name,
        Path(file1_path),
        file1_version,
        Path(file2_path),
        file2_version,
        Path(output_dir),
        output_filename=output_filename,
        merge_from=256 if merge_from is None else merge_from,
        merge_to=merge_to,
        generate_json_report=generate_json_report,
    )


# ---------------------------------------------------------------------------
# GUI
# ---------------------------------------------------------------------------
class App(ttk.Frame):
    def __init__(self, master: tk.Tk):
        super().__init__(master, padding=16)
        self.master = master
        master.title("Q-HUFDEL Packaging Tool")
        master.minsize(720, 430)
        self.grid(sticky="nsew")
        master.columnconfigure(0, weight=1)
        master.rowconfigure(0, weight=1)
        self.columnconfigure(1, weight=1)

        self.file1 = tk.StringVar()
        self.ver1 = tk.IntVar()
        self.file2 = tk.StringVar()
        self.ver2 = tk.IntVar()
        self.output = tk.StringVar()
        self.output_filename = tk.StringVar()
        self.merge_from = tk.StringVar()
        self.merge_to = tk.StringVar()
        self.generate_json_report = tk.BooleanVar()
        self.status = tk.StringVar(value="Ready")
        self.process_dialog = None
        self.process_queue = None

        self._build_widgets()

    # -- layout -------------------------------------------------------------
    def _build_widgets(self):
        ttk.Label(self, text="Name:").grid(
            row=0, column=0, sticky="w", pady=(0, 8)
        )
        ttk.Entry(self, textvariable=self.output_filename).grid(
            row=0, column=1, sticky="ew", padx=6, pady=(0, 8)
        )

        self._file_row(1, "File 1:", self.file1, self.ver1, self._browse_file1)
        self._file_row(2, "File 2:", self.file2, self.ver2, self._browse_file2)

        ttk.Label(self, text="Output folder:").grid(
            row=3, column=0, sticky="w", pady=(12, 4)
        )
        ttk.Entry(self, textvariable=self.output).grid(
            row=3, column=1, sticky="ew", padx=6, pady=(12, 4)
        )
        ttk.Button(self, text="Browse...", command=self._browse_output).grid(
            row=3, column=2, pady=(12, 4)
        )

        ttk.Label(self, text="Merge from (inclusive):").grid(
            row=4, column=0, sticky="w", pady=4
        )
        ttk.Entry(self, textvariable=self.merge_from).grid(
            row=4, column=1, sticky="ew", padx=6, pady=4
        )

        ttk.Label(self, text="Merge to (exclusive):").grid(
            row=5, column=0, sticky="w", pady=4
        )
        ttk.Entry(self, textvariable=self.merge_to).grid(
            row=5, column=1, sticky="ew", padx=6, pady=4
        )

        ttk.Checkbutton(
            self,
            text="Generate JSON report",
            variable=self.generate_json_report,
        ).grid(row=6, column=0, columnspan=2, sticky="w", pady=4)

        self.run_btn = ttk.Button(self, text="Run", command=self._on_run)
        self.run_btn.grid(row=7, column=0, columnspan=5, pady=(18, 6))

        self.progress = ttk.Progressbar(self, mode="indeterminate")
        self.progress.grid(row=8, column=0, columnspan=5, sticky="ew")

        ttk.Label(self, textvariable=self.status).grid(
            row=9, column=0, columnspan=5, sticky="w", pady=(6, 0)
        )

    def _file_row(self, row, label, path_var, ver_var, browse_cmd):
        ttk.Label(self, text=label).grid(row=row, column=0, sticky="w", pady=4)
        ttk.Entry(self, textvariable=path_var).grid(
            row=row, column=1, sticky="ew", padx=6, pady=4
        )
        ttk.Button(self, text="Browse...", command=browse_cmd).grid(
            row=row, column=2, pady=4
        )
        ttk.Label(self, text="Version:").grid(
            row=row, column=3, sticky="e", padx=(12, 4)
        )
        ttk.Entry(self, textvariable=ver_var, width=10).grid(row=row, column=4, pady=4)

    # -- file dialogs -------------------------------------------------------
    def _browse_file1(self):
        path = filedialog.askopenfilename(title="Select first file")
        if path:
            self.file1.set(path)

    def _browse_file2(self):
        path = filedialog.askopenfilename(title="Select second file")
        if path:
            self.file2.set(path)

    def _browse_output(self):
        path = filedialog.askdirectory(title="Select output folder")
        if path:
            self.output.set(path)

    # -- validation + execution --------------------------------------------
    def _validate(self):
        import os

        name = self.output_filename.get().strip()
        if not name:
            return "Output name is required."
        if Path(name).name != name:
            return "Output name must not contain folders."
        if Path(name).suffix:
            return "Output name must not include an extension."
        for label, var in (("File 1", self.file1), ("File 2", self.file2)):
            p = var.get().strip()
            if not p:
                return f"{label} path is required."
            if not os.path.isfile(p):
                return f"{label} does not exist:\n{p}"
        if not self.ver1.get():
            return "Version number for File 1 is required."
        if not self.ver2.get():
            return "Version number for File 2 is required."
        out = self.output.get().strip()
        if not out:
            return "Output folder is required."
        if not os.path.isdir(out):
            return f"Output folder does not exist:\n{out}"
        merge_from = self.merge_from.get().strip()
        merge_to = self.merge_to.get().strip()
        if bool(merge_from) != bool(merge_to):
            return "Merge from and merge to must both be provided or both be empty."
        if merge_from and merge_to:
            try:
                merge_from_value = int(merge_from)
                merge_to_value = int(merge_to)
            except ValueError:
                return "Merge bounds must be integers."
            if merge_from_value < 0 or merge_to_value <= merge_from_value:
                return "Merge to must be greater than merge from, and merge bounds cannot be negative."
        return None

    def _on_run(self):
        error = self._validate()
        if error:
            messagebox.showerror("Missing or invalid input", error)
            return

        args = (
            self.output_filename.get().strip(),
            self.file1.get().strip(),
            self.ver1.get(),
            self.file2.get().strip(),
            self.ver2.get(),
            self.output.get().strip(),
            int(self.merge_from.get()) if self.merge_from.get().strip() else None,
            int(self.merge_to.get()) if self.merge_to.get().strip() else None,
            self.generate_json_report.get(),
        )

        self.run_btn.state(["disabled"])
        self.progress.start(12)
        self.status.set("Processing...")
        self.process_queue = queue.Queue()
        self.process_dialog = ProcessDialog(self.master)
        self._poll_process_output()

        # Run in a worker thread so the GUI stays responsive.
        threading.Thread(target=self._worker, args=args, daemon=True).start()

    def _worker(self, *args):
        writer = QueueWriter(self.process_queue)
        try:
            with contextlib.redirect_stdout(writer), contextlib.redirect_stderr(writer):
                result = run_backend(*args)
        except Exception as exc:  # noqa: BLE001 - show any backend error to user
            self.master.after(0, self._on_done, exc, None)
        else:
            self.master.after(0, self._on_done, None, result)

    def _poll_process_output(self):
        if self.process_queue is not None and self.process_dialog is not None:
            try:
                while True:
                    self.process_dialog.append(self.process_queue.get_nowait())
            except queue.Empty:
                pass
            if self.process_dialog.winfo_exists():
                self.master.after(100, self._poll_process_output)

    def _on_done(self, exc, result):
        self.progress.stop()
        self.run_btn.state(["!disabled"])
        if self.process_dialog is not None and self.process_dialog.winfo_exists():
            self.process_dialog.finish()
        if exc is None:
            self.status.set("Done.")
            messagebox.showinfo("Success", f"Output file:\n{result}")
        else:
            self.status.set("Failed.")
            messagebox.showerror("Backend error", str(exc))


def main():
    parse_args()
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()

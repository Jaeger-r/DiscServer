import json
import math
import tkinter as tk
from copy import deepcopy
from tkinter import colorchooser, filedialog, messagebox, ttk

try:
    from PIL import Image
except ImportError:
    Image = None


DEFAULT_PALETTE = [
    (0, 0, 0, 0),
    (31, 36, 48, 255),
    (92, 120, 230, 255),
    (245, 214, 92, 255),
    (233, 96, 86, 255),
    (102, 187, 106, 255),
    (255, 255, 255, 255),
]

MODE_PRESETS = {
    "tileset": {
        "title": "图块集",
        "entry_name": "图块",
        "width": 16,
        "height": 16,
        "count": 8,
        "columns": 4,
        "fps": 6,
    },
    "character": {
        "title": "人物精灵动画",
        "entry_name": "动画帧",
        "width": 32,
        "height": 32,
        "count": 6,
        "columns": 6,
        "fps": 8,
    },
    "item": {
        "title": "物品/道具",
        "entry_name": "物品",
        "width": 24,
        "height": 24,
        "count": 6,
        "columns": 4,
        "fps": 4,
    },
}


class AssetStudioApp:
    def __init__(self, root):
        self.root = root
        self.root.title("素材工坊 Asset Studio")
        self.root.geometry("1480x900")
        self.root.minsize(1220, 760)

        self.asset_kind_var = tk.StringVar(value="tileset")
        self.canvas_zoom_var = tk.IntVar(value=24)
        self.preview_scale_var = tk.IntVar(value=6)
        self.grid_visible_var = tk.BooleanVar(value=True)
        self.sheet_columns_var = tk.IntVar(value=4)
        self.playback_fps_var = tk.IntVar(value=8)
        self.cell_width_var = tk.IntVar(value=16)
        self.cell_height_var = tk.IntVar(value=16)
        self.asset_name_var = tk.StringVar(value="")
        self.status_var = tk.StringVar(value="准备开始绘制")
        self.preview_mode_var = tk.StringVar(value="sheet")

        self.current_tool = "pencil"
        self.current_color = 1
        self.palette = list(DEFAULT_PALETTE)

        self.frames = []
        self.selected_index = 0
        self.history = []

        self.is_dragging = False
        self.stroke_changed = False
        self.last_painted = None
        self.preview_job = None
        self.preview_anim_index = 0
        self.tool_buttons = {}
        self.palette_buttons = []

        self.setup_style()
        self.build_ui()
        self.bind_shortcuts()
        self.apply_mode_preset(reset_entries=True)
        self.refresh_all()
        self.start_preview_loop()

    def setup_style(self):
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        bg = "#f7f1e3"
        panel = "#efe4c8"
        accent = "#2f5d50"
        style.configure(".", font=("Helvetica", 11))
        style.configure("App.TFrame", background=bg)
        style.configure("Panel.TFrame", background=panel, relief="flat")
        style.configure("PanelHeader.TLabel", background=panel, foreground="#3b2f24", font=("Helvetica", 12, "bold"))
        style.configure("App.TLabel", background=bg, foreground="#2b241d")
        style.configure("Panel.TLabel", background=panel, foreground="#2b241d")
        style.configure("Accent.TButton", background=accent, foreground="white")
        style.map("Accent.TButton", background=[("active", "#3c7867")])
        self.root.configure(bg=bg)

    def build_ui(self):
        self.root.columnconfigure(1, weight=1)
        self.root.rowconfigure(0, weight=1)

        left_panel = ttk.Frame(self.root, style="Panel.TFrame", padding=14)
        left_panel.grid(row=0, column=0, sticky="nsw")

        center_panel = ttk.Frame(self.root, style="App.TFrame", padding=(0, 12, 0, 12))
        center_panel.grid(row=0, column=1, sticky="nsew")
        center_panel.columnconfigure(0, weight=1)
        center_panel.rowconfigure(0, weight=1)

        right_panel = ttk.Frame(self.root, style="Panel.TFrame", padding=14)
        right_panel.grid(row=0, column=2, sticky="nse")

        self.build_left_panel(left_panel)
        self.build_center_panel(center_panel)
        self.build_right_panel(right_panel)

    def build_left_panel(self, parent):
        ttk.Label(parent, text="素材类型", style="PanelHeader.TLabel").pack(anchor="w")
        mode_box = ttk.Frame(parent, style="Panel.TFrame")
        mode_box.pack(fill="x", pady=(8, 14))
        for value, text in [("tileset", "图块集"), ("character", "人物动画"), ("item", "物品素材")]:
            ttk.Radiobutton(
                mode_box,
                text=text,
                value=value,
                variable=self.asset_kind_var,
                command=self.on_mode_changed,
            ).pack(anchor="w", pady=2)

        ttk.Label(parent, text="绘制工具", style="PanelHeader.TLabel").pack(anchor="w")
        tool_box = ttk.Frame(parent, style="Panel.TFrame")
        tool_box.pack(fill="x", pady=(8, 14))
        tool_specs = [
            ("pencil", "铅笔 B"),
            ("eraser", "橡皮 E"),
            ("fill", "油漆桶 F"),
            ("eyedropper", "吸管 I"),
        ]
        for tool_name, label in tool_specs:
            button = tk.Button(
                tool_box,
                text=label,
                width=16,
                relief=tk.RAISED,
                bg="#f8f1dd",
                activebackground="#e4d7b9",
                command=lambda name=tool_name: self.select_tool(name),
            )
            button.pack(fill="x", pady=3)
            self.tool_buttons[tool_name] = button

        ttk.Label(parent, text="画布设置", style="PanelHeader.TLabel").pack(anchor="w")
        canvas_box = ttk.Frame(parent, style="Panel.TFrame")
        canvas_box.pack(fill="x", pady=(8, 14))

        ttk.Label(canvas_box, text="单素材宽度", style="Panel.TLabel").pack(anchor="w")
        ttk.Entry(canvas_box, textvariable=self.cell_width_var, width=8).pack(anchor="w", pady=(0, 6))
        ttk.Label(canvas_box, text="单素材高度", style="Panel.TLabel").pack(anchor="w")
        ttk.Entry(canvas_box, textvariable=self.cell_height_var, width=8).pack(anchor="w", pady=(0, 6))
        ttk.Label(canvas_box, text="编辑缩放", style="Panel.TLabel").pack(anchor="w")
        ttk.Scale(
            canvas_box,
            from_=8,
            to=36,
            variable=self.canvas_zoom_var,
            orient="horizontal",
            command=lambda _value: self.refresh_canvas(),
        ).pack(fill="x", pady=(0, 6))
        ttk.Checkbutton(
            canvas_box,
            text="显示网格",
            variable=self.grid_visible_var,
            command=self.refresh_canvas,
        ).pack(anchor="w")

        ttk.Button(canvas_box, text="按当前尺寸新建素材", command=self.rebuild_entries_for_size).pack(fill="x", pady=(8, 0))

        ttk.Label(parent, text="快捷操作", style="PanelHeader.TLabel").pack(anchor="w")
        quick_box = ttk.Frame(parent, style="Panel.TFrame")
        quick_box.pack(fill="x", pady=(8, 0))
        ttk.Button(quick_box, text="撤销 Ctrl+Z", command=self.undo).pack(fill="x", pady=2)
        ttk.Button(quick_box, text="清空当前素材", command=self.clear_current_entry).pack(fill="x", pady=2)
        ttk.Button(quick_box, text="导入 PNG 到当前素材", command=self.import_png_into_current).pack(fill="x", pady=2)
        ttk.Button(quick_box, text="保存项目 JSON", command=self.save_project).pack(fill="x", pady=2)
        ttk.Button(quick_box, text="打开项目 JSON", command=self.load_project).pack(fill="x", pady=2)

    def build_center_panel(self, parent):
        editor_frame = ttk.Frame(parent, style="App.TFrame")
        editor_frame.grid(row=0, column=0, sticky="nsew", padx=12)
        editor_frame.columnconfigure(0, weight=1)
        editor_frame.rowconfigure(1, weight=1)

        header = ttk.Frame(editor_frame, style="App.TFrame")
        header.grid(row=0, column=0, sticky="ew", pady=(0, 10))
        header.columnconfigure(0, weight=1)

        self.title_label = ttk.Label(header, text="素材工坊", style="App.TLabel", font=("Helvetica", 18, "bold"))
        self.title_label.grid(row=0, column=0, sticky="w")
        ttk.Label(
            header,
            textvariable=self.status_var,
            style="App.TLabel",
            font=("Helvetica", 11),
        ).grid(row=1, column=0, sticky="w", pady=(6, 0))

        canvas_shell = tk.Frame(
            editor_frame,
            bg="#d9ccb2",
            bd=0,
            highlightthickness=0,
        )
        canvas_shell.grid(row=1, column=0, sticky="nsew")
        canvas_shell.grid_rowconfigure(0, weight=1)
        canvas_shell.grid_columnconfigure(0, weight=1)

        self.canvas = tk.Canvas(
            canvas_shell,
            bg="#fff7e8",
            highlightthickness=0,
            cursor="crosshair",
        )
        self.canvas.grid(row=0, column=0, sticky="nsew", padx=18, pady=18)
        self.canvas.bind("<Button-1>", self.on_canvas_press)
        self.canvas.bind("<B1-Motion>", self.on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_canvas_release)
        self.canvas.bind("<Motion>", self.on_canvas_motion)
        self.canvas.bind("<Button-3>", self.pick_color_with_secondary_click)

    def build_right_panel(self, parent):
        ttk.Label(parent, text="素材列表", style="PanelHeader.TLabel").pack(anchor="w")
        list_box = ttk.Frame(parent, style="Panel.TFrame")
        list_box.pack(fill="both", expand=False, pady=(8, 12))

        self.entry_listbox = tk.Listbox(
            list_box,
            width=26,
            height=12,
            activestyle="none",
            bg="#fff9ef",
            selectbackground="#2f5d50",
            selectforeground="white",
        )
        self.entry_listbox.pack(side="left", fill="both", expand=True)
        self.entry_listbox.bind("<<ListboxSelect>>", self.on_entry_selected)

        entry_scroll = ttk.Scrollbar(list_box, orient="vertical", command=self.entry_listbox.yview)
        entry_scroll.pack(side="right", fill="y")
        self.entry_listbox.configure(yscrollcommand=entry_scroll.set)

        ttk.Label(parent, text="当前名称", style="Panel.TLabel").pack(anchor="w")
        ttk.Entry(parent, textvariable=self.asset_name_var, width=24).pack(fill="x", pady=(4, 6))
        ttk.Button(parent, text="重命名当前素材", command=self.rename_current_entry).pack(fill="x", pady=(0, 12))

        entry_actions = ttk.Frame(parent, style="Panel.TFrame")
        entry_actions.pack(fill="x", pady=(0, 14))
        ttk.Button(entry_actions, text="新增", command=self.add_entry).pack(fill="x", pady=2)
        ttk.Button(entry_actions, text="复制", command=self.duplicate_entry).pack(fill="x", pady=2)
        ttk.Button(entry_actions, text="删除", command=self.delete_entry).pack(fill="x", pady=2)
        ttk.Button(entry_actions, text="上移", command=lambda: self.move_entry(-1)).pack(fill="x", pady=2)
        ttk.Button(entry_actions, text="下移", command=lambda: self.move_entry(1)).pack(fill="x", pady=2)

        ttk.Label(parent, text="调色板", style="PanelHeader.TLabel").pack(anchor="w")
        self.palette_frame = ttk.Frame(parent, style="Panel.TFrame")
        self.palette_frame.pack(fill="x", pady=(8, 8))

        palette_actions = ttk.Frame(parent, style="Panel.TFrame")
        palette_actions.pack(fill="x", pady=(0, 14))
        ttk.Button(palette_actions, text="添加颜色", command=self.add_color).pack(fill="x", pady=2)
        ttk.Button(palette_actions, text="修改当前颜色", command=self.edit_current_color).pack(fill="x", pady=2)
        ttk.Button(palette_actions, text="删除当前颜色", command=self.remove_current_color).pack(fill="x", pady=2)

        ttk.Label(parent, text="预览与导出", style="PanelHeader.TLabel").pack(anchor="w")
        preview_options = ttk.Frame(parent, style="Panel.TFrame")
        preview_options.pack(fill="x", pady=(8, 8))
        ttk.Radiobutton(
            preview_options,
            text="整表预览",
            value="sheet",
            variable=self.preview_mode_var,
            command=self.refresh_preview,
        ).pack(anchor="w")
        ttk.Radiobutton(
            preview_options,
            text="动画预览",
            value="animation",
            variable=self.preview_mode_var,
            command=self.refresh_preview,
        ).pack(anchor="w")
        ttk.Label(preview_options, text="导出列数", style="Panel.TLabel").pack(anchor="w", pady=(6, 0))
        ttk.Entry(preview_options, textvariable=self.sheet_columns_var, width=8).pack(anchor="w", pady=(2, 6))
        ttk.Label(preview_options, text="动画 FPS", style="Panel.TLabel").pack(anchor="w")
        ttk.Entry(preview_options, textvariable=self.playback_fps_var, width=8).pack(anchor="w", pady=(2, 6))
        ttk.Label(preview_options, text="预览缩放", style="Panel.TLabel").pack(anchor="w")
        ttk.Scale(
            preview_options,
            from_=2,
            to=12,
            variable=self.preview_scale_var,
            orient="horizontal",
            command=lambda _value: self.refresh_preview(),
        ).pack(fill="x")

        self.preview_canvas = tk.Canvas(
            parent,
            width=280,
            height=280,
            bg="#fff7e8",
            highlightthickness=0,
        )
        self.preview_canvas.pack(fill="both", expand=False, pady=(0, 10))

        export_actions = ttk.Frame(parent, style="Panel.TFrame")
        export_actions.pack(fill="x")
        ttk.Button(export_actions, text="导出 Sprite Sheet PNG", command=self.export_sheet_png).pack(fill="x", pady=2)
        ttk.Button(export_actions, text="导出当前素材 PNG", command=self.export_current_png).pack(fill="x", pady=2)
        ttk.Button(export_actions, text="导出索引矩阵 JSON", command=self.export_frame_data).pack(fill="x", pady=2)

    def bind_shortcuts(self):
        self.root.bind("<Control-z>", lambda _event: self.undo())
        self.root.bind("b", lambda _event: self.select_tool("pencil"))
        self.root.bind("e", lambda _event: self.select_tool("eraser"))
        self.root.bind("f", lambda _event: self.select_tool("fill"))
        self.root.bind("i", lambda _event: self.select_tool("eyedropper"))

    def create_blank_grid(self, width, height):
        return [[0 for _x in range(width)] for _y in range(height)]

    def create_entry(self, name=None):
        width = max(1, int(self.cell_width_var.get()))
        height = max(1, int(self.cell_height_var.get()))
        label = MODE_PRESETS[self.asset_kind_var.get()]["entry_name"]
        index = len(self.frames) + 1
        return {
            "name": name or f"{label}_{index:02d}",
            "grid": self.create_blank_grid(width, height),
        }

    def on_mode_changed(self):
        if any(self.frame_has_content(entry["grid"]) for entry in self.frames):
            answer = messagebox.askyesno("切换素材类型", "切换类型会按预设重建素材列表，是否继续？")
            if not answer:
                for key, preset in MODE_PRESETS.items():
                    if preset["title"] == self.title_label.cget("text"):
                        self.asset_kind_var.set(key)
                        break
                return
        self.apply_mode_preset(reset_entries=True)
        self.push_status("已切换素材类型并载入对应预设")

    def apply_mode_preset(self, reset_entries):
        preset = MODE_PRESETS[self.asset_kind_var.get()]
        self.title_label.configure(text=preset["title"])
        self.cell_width_var.set(preset["width"])
        self.cell_height_var.set(preset["height"])
        self.sheet_columns_var.set(preset["columns"])
        self.playback_fps_var.set(preset["fps"])
        if self.asset_kind_var.get() == "character":
            self.preview_mode_var.set("animation")
        else:
            self.preview_mode_var.set("sheet")

        if reset_entries:
            self.frames = []
            for index in range(preset["count"]):
                self.frames.append(self.create_entry(name=f"{preset['entry_name']}_{index + 1:02d}"))
            self.selected_index = 0
            self.history.clear()

    def rebuild_entries_for_size(self):
        width = max(1, int(self.cell_width_var.get()))
        height = max(1, int(self.cell_height_var.get()))
        if any(self.frame_has_content(entry["grid"]) for entry in self.frames):
            answer = messagebox.askyesno("重建素材", f"会把所有素材重建为 {width}x{height}，继续吗？")
            if not answer:
                return
        for entry in self.frames:
            entry["grid"] = self.create_blank_grid(width, height)
        self.history.clear()
        self.refresh_all()
        self.push_status(f"已按 {width}x{height} 尺寸重建全部素材")

    def refresh_all(self):
        self.update_tool_buttons()
        self.refresh_entry_list()
        self.refresh_palette()
        self.refresh_canvas()
        self.refresh_preview()

    def refresh_entry_list(self):
        selected_name = ""
        if self.frames:
            selected_name = self.frames[self.selected_index]["name"]

        self.entry_listbox.delete(0, tk.END)
        label = MODE_PRESETS[self.asset_kind_var.get()]["entry_name"]
        for index, entry in enumerate(self.frames):
            self.entry_listbox.insert(tk.END, f"{index + 1:02d}. {entry['name']}")
        if self.frames:
            self.selected_index = min(self.selected_index, len(self.frames) - 1)
            self.entry_listbox.selection_set(self.selected_index)
            self.entry_listbox.activate(self.selected_index)
            self.asset_name_var.set(self.frames[self.selected_index]["name"])
            self.status_var.set(f"正在编辑 {label}：{selected_name or self.frames[self.selected_index]['name']}")
        else:
            self.asset_name_var.set("")

    def refresh_palette(self):
        for button in self.palette_buttons:
            button.destroy()
        self.palette_buttons.clear()

        for index, rgba in enumerate(self.palette):
            r, g, b, a = rgba
            color = "#b9b1a0" if a == 0 else f"#{r:02x}{g:02x}{b:02x}"
            text = "T" if a == 0 else ""
            button = tk.Button(
                self.palette_frame,
                text=text,
                width=3,
                height=1,
                bg=color,
                relief=tk.SUNKEN if index == self.current_color else tk.RAISED,
                activebackground=color,
                command=lambda idx=index: self.set_current_color(idx),
            )
            button.grid(row=index // 4, column=index % 4, padx=3, pady=3, sticky="ew")
            self.palette_buttons.append(button)

    def update_tool_buttons(self):
        for name, button in self.tool_buttons.items():
            if name == self.current_tool:
                button.configure(relief=tk.SUNKEN, bg="#d0b97a")
            else:
                button.configure(relief=tk.RAISED, bg="#f8f1dd")

    def set_current_color(self, index):
        self.current_color = index
        self.refresh_palette()
        self.push_status(f"已切换到颜色索引 {index}")

    def select_tool(self, tool_name):
        self.current_tool = tool_name
        self.update_tool_buttons()
        tool_text = {
            "pencil": "铅笔",
            "eraser": "橡皮",
            "fill": "油漆桶",
            "eyedropper": "吸管",
        }[tool_name]
        self.push_status(f"当前工具：{tool_text}")

    def current_entry(self):
        if not self.frames:
            return None
        return self.frames[self.selected_index]

    def refresh_canvas(self):
        self.canvas.delete("all")
        entry = self.current_entry()
        if entry is None:
            return

        grid = entry["grid"]
        zoom = max(4, int(self.canvas_zoom_var.get()))
        height = len(grid)
        width = len(grid[0]) if height else 0
        canvas_width = max(200, width * zoom)
        canvas_height = max(200, height * zoom)
        self.canvas.configure(width=canvas_width, height=canvas_height)

        self.draw_checkerboard(self.canvas, 0, 0, canvas_width, canvas_height, max(8, zoom))

        for y, row in enumerate(grid):
            for x, color_index in enumerate(row):
                rgba = self.palette[color_index]
                if rgba[3] == 0:
                    continue
                fill = f"#{rgba[0]:02x}{rgba[1]:02x}{rgba[2]:02x}"
                self.canvas.create_rectangle(
                    x * zoom,
                    y * zoom,
                    (x + 1) * zoom,
                    (y + 1) * zoom,
                    fill=fill,
                    outline=fill,
                )

        if self.grid_visible_var.get():
            for x in range(width + 1):
                self.canvas.create_line(x * zoom, 0, x * zoom, height * zoom, fill="#d4c7aa")
            for y in range(height + 1):
                self.canvas.create_line(0, y * zoom, width * zoom, y * zoom, fill="#d4c7aa")

    def draw_checkerboard(self, canvas, x0, y0, width, height, cell):
        color_a = "#fff7e8"
        color_b = "#f0e3c9"
        for y in range(0, height, cell):
            for x in range(0, width, cell):
                fill = color_a if ((x // cell) + (y // cell)) % 2 == 0 else color_b
                canvas.create_rectangle(x0 + x, y0 + y, x0 + x + cell, y0 + y + cell, fill=fill, outline=fill)

    def event_to_cell(self, event):
        entry = self.current_entry()
        if entry is None:
            return None
        zoom = max(4, int(self.canvas_zoom_var.get()))
        grid = entry["grid"]
        x = event.x // zoom
        y = event.y // zoom
        if 0 <= y < len(grid) and 0 <= x < len(grid[0]):
            return x, y
        return None

    def on_canvas_press(self, event):
        cell = self.event_to_cell(event)
        if cell is None:
            return
        self.is_dragging = True
        self.stroke_changed = False
        self.last_painted = None
        self.apply_tool_at(*cell, push_history=True)

    def on_canvas_drag(self, event):
        if not self.is_dragging:
            return
        cell = self.event_to_cell(event)
        if cell is None:
            return
        self.apply_tool_at(*cell, push_history=False)

    def on_canvas_release(self, _event):
        self.is_dragging = False
        self.last_painted = None

    def on_canvas_motion(self, event):
        cell = self.event_to_cell(event)
        if cell is None:
            return
        self.status_var.set(f"编辑中：{self.current_entry()['name']} | 坐标 ({cell[0]}, {cell[1]}) | 工具 {self.current_tool}")

    def pick_color_with_secondary_click(self, event):
        cell = self.event_to_cell(event)
        if cell is None:
            return
        entry = self.current_entry()
        color_index = entry["grid"][cell[1]][cell[0]]
        self.set_current_color(color_index)
        self.select_tool("pencil")

    def apply_tool_at(self, x, y, push_history):
        entry = self.current_entry()
        if entry is None:
            return
        grid = entry["grid"]

        if self.current_tool in {"pencil", "eraser"} and self.last_painted == (x, y):
            return

        before = grid[y][x]
        changed = False
        should_capture_history = (push_history or not self.stroke_changed) and self.current_tool in {"pencil", "eraser", "fill"}

        if self.current_tool == "pencil":
            after = self.current_color
            if before != after:
                changed = True
                if should_capture_history:
                    self.push_history_snapshot()
                grid[y][x] = after
                self.stroke_changed = True
        elif self.current_tool == "eraser":
            if before != 0:
                changed = True
                if should_capture_history:
                    self.push_history_snapshot()
                grid[y][x] = 0
                self.stroke_changed = True
        elif self.current_tool == "eyedropper":
            self.set_current_color(before)
        elif self.current_tool == "fill":
            if before != self.current_color:
                if should_capture_history:
                    self.push_history_snapshot()
                self.flood_fill(grid, x, y, before, self.current_color)
                changed = True

        if changed:
            self.refresh_canvas()
            self.refresh_preview()
        self.last_painted = (x, y)

    def flood_fill(self, grid, x, y, target, replacement):
        if target == replacement:
            return
        stack = [(x, y)]
        width = len(grid[0])
        height = len(grid)
        while stack:
            current_x, current_y = stack.pop()
            if grid[current_y][current_x] != target:
                continue
            grid[current_y][current_x] = replacement
            if current_x > 0:
                stack.append((current_x - 1, current_y))
            if current_x < width - 1:
                stack.append((current_x + 1, current_y))
            if current_y > 0:
                stack.append((current_x, current_y - 1))
            if current_y < height - 1:
                stack.append((current_x, current_y + 1))

    def frame_has_content(self, grid):
        return any(cell != 0 for row in grid for cell in row)

    def serialize_state(self):
        return {
            "frames": deepcopy(self.frames),
            "selected_index": self.selected_index,
            "palette": deepcopy(self.palette),
            "current_color": self.current_color,
        }

    def restore_state(self, state):
        self.frames = deepcopy(state["frames"])
        self.selected_index = min(state["selected_index"], len(self.frames) - 1)
        self.palette = deepcopy(state["palette"])
        self.current_color = min(state["current_color"], len(self.palette) - 1)
        self.refresh_all()

    def push_history_snapshot(self):
        self.history.append(self.serialize_state())
        if len(self.history) > 40:
            self.history.pop(0)

    def undo(self):
        if not self.history:
            self.push_status("没有可撤销的操作")
            return
        self.restore_state(self.history.pop())
        self.push_status("已撤销上一步")

    def on_entry_selected(self, _event):
        selection = self.entry_listbox.curselection()
        if not selection:
            return
        self.selected_index = selection[0]
        self.asset_name_var.set(self.frames[self.selected_index]["name"])
        self.preview_anim_index = self.selected_index
        self.refresh_canvas()
        self.refresh_preview()

    def rename_current_entry(self):
        entry = self.current_entry()
        if entry is None:
            return
        new_name = self.asset_name_var.get().strip()
        if not new_name:
            messagebox.showwarning("名称为空", "请输入有效名称")
            return
        entry["name"] = new_name
        self.refresh_entry_list()
        self.push_status(f"已重命名为 {new_name}")

    def add_entry(self):
        self.frames.append(self.create_entry())
        self.selected_index = len(self.frames) - 1
        self.refresh_all()
        self.push_status("已新增一个素材")

    def duplicate_entry(self):
        entry = self.current_entry()
        if entry is None:
            return
        clone = {
            "name": f"{entry['name']}_copy",
            "grid": deepcopy(entry["grid"]),
        }
        self.frames.insert(self.selected_index + 1, clone)
        self.selected_index += 1
        self.refresh_all()
        self.push_status("已复制当前素材")

    def delete_entry(self):
        if len(self.frames) <= 1:
            messagebox.showwarning("无法删除", "至少保留一个素材")
            return
        deleted_name = self.frames[self.selected_index]["name"]
        del self.frames[self.selected_index]
        self.selected_index = max(0, min(self.selected_index, len(self.frames) - 1))
        self.refresh_all()
        self.push_status(f"已删除 {deleted_name}")

    def clear_current_entry(self):
        entry = self.current_entry()
        if entry is None:
            return
        if not self.frame_has_content(entry["grid"]):
            self.push_status("当前素材已经是空白")
            return
        self.push_history_snapshot()
        width = len(entry["grid"][0])
        height = len(entry["grid"])
        entry["grid"] = self.create_blank_grid(width, height)
        self.refresh_canvas()
        self.refresh_preview()
        self.push_status(f"已清空 {entry['name']}")

    def move_entry(self, direction):
        new_index = self.selected_index + direction
        if new_index < 0 or new_index >= len(self.frames):
            return
        self.frames[self.selected_index], self.frames[new_index] = self.frames[new_index], self.frames[self.selected_index]
        self.selected_index = new_index
        self.refresh_all()

    def add_color(self):
        result = colorchooser.askcolor(title="选择新颜色")
        if result is None or result[0] is None:
            return
        r, g, b = [int(channel) for channel in result[0]]
        self.palette.append((r, g, b, 255))
        self.current_color = len(self.palette) - 1
        self.refresh_palette()
        self.push_status(f"已添加颜色索引 {self.current_color}")

    def edit_current_color(self):
        if self.current_color == 0:
            messagebox.showinfo("透明颜色", "透明色作为索引 0，不建议直接修改")
            return
        rgba = self.palette[self.current_color]
        result = colorchooser.askcolor(color=f"#{rgba[0]:02x}{rgba[1]:02x}{rgba[2]:02x}", title="修改当前颜色")
        if result is None or result[0] is None:
            return
        r, g, b = [int(channel) for channel in result[0]]
        self.palette[self.current_color] = (r, g, b, 255)
        self.refresh_all()
        self.push_status(f"已修改颜色索引 {self.current_color}")

    def remove_current_color(self):
        if self.current_color == 0:
            messagebox.showwarning("无法删除", "透明色索引 0 不能删除")
            return
        removing_index = self.current_color
        del self.palette[removing_index]
        for entry in self.frames:
            grid = entry["grid"]
            for y, row in enumerate(grid):
                for x, value in enumerate(row):
                    if value == removing_index:
                        grid[y][x] = 0
                    elif value > removing_index:
                        grid[y][x] = value - 1
        self.current_color = min(self.current_color - 1, len(self.palette) - 1)
        self.refresh_all()
        self.push_status("已删除当前颜色，并自动修正索引")

    def render_grid_to_image(self, grid):
        if Image is None:
            raise RuntimeError("当前环境未安装 Pillow，无法导出 PNG")
        height = len(grid)
        width = len(grid[0]) if height else 0
        image = Image.new("RGBA", (width, height))
        for y, row in enumerate(grid):
            for x, color_index in enumerate(row):
                image.putpixel((x, y), self.palette[color_index])
        return image

    def render_sheet_image(self):
        if Image is None:
            raise RuntimeError("当前环境未安装 Pillow，无法导出 PNG")
        if not self.frames:
            raise RuntimeError("当前没有素材可导出")
        width = len(self.frames[0]["grid"][0])
        height = len(self.frames[0]["grid"])
        columns = max(1, int(self.sheet_columns_var.get()))
        rows = math.ceil(len(self.frames) / columns)
        sheet = Image.new("RGBA", (columns * width, rows * height), (0, 0, 0, 0))
        for index, entry in enumerate(self.frames):
            image = self.render_grid_to_image(entry["grid"])
            x = (index % columns) * width
            y = (index // columns) * height
            sheet.paste(image, (x, y))
        return sheet

    def grid_to_photoimage(self, grid):
        height = len(grid)
        width = len(grid[0]) if height else 0
        photo = tk.PhotoImage(width=width, height=height)
        for y, row in enumerate(grid):
            for x, color_index in enumerate(row):
                rgba = self.palette[color_index]
                if rgba[3] == 0:
                    continue
                color = f"#{rgba[0]:02x}{rgba[1]:02x}{rgba[2]:02x}"
                photo.put(color, (x, y))
        return photo

    def sheet_to_photoimage(self):
        if not self.frames:
            raise RuntimeError("当前没有素材可导出")
        width = len(self.frames[0]["grid"][0])
        height = len(self.frames[0]["grid"])
        columns = max(1, int(self.sheet_columns_var.get()))
        rows = math.ceil(len(self.frames) / columns)
        photo = tk.PhotoImage(width=columns * width, height=rows * height)
        for index, entry in enumerate(self.frames):
            offset_x = (index % columns) * width
            offset_y = (index // columns) * height
            for y, row in enumerate(entry["grid"]):
                for x, color_index in enumerate(row):
                    rgba = self.palette[color_index]
                    if rgba[3] == 0:
                        continue
                    color = f"#{rgba[0]:02x}{rgba[1]:02x}{rgba[2]:02x}"
                    photo.put(color, (offset_x + x, offset_y + y))
        return photo

    def write_photoimage_png(self, photo, file_path):
        photo.write(file_path, format="png")

    def export_sheet_png(self):
        file_path = filedialog.asksaveasfilename(
            title="导出 Sprite Sheet",
            defaultextension=".png",
            filetypes=[("PNG 图片", "*.png")],
            initialfile=f"{self.asset_kind_var.get()}_sheet.png",
        )
        if not file_path:
            return
        try:
            if Image is not None:
                self.render_sheet_image().save(file_path)
            else:
                self.write_photoimage_png(self.sheet_to_photoimage(), file_path)
        except Exception as error:
            messagebox.showerror("导出失败", str(error))
            return
        self.push_status(f"已导出整表 PNG：{file_path}")

    def export_current_png(self):
        entry = self.current_entry()
        if entry is None:
            return
        file_path = filedialog.asksaveasfilename(
            title="导出当前素材",
            defaultextension=".png",
            filetypes=[("PNG 图片", "*.png")],
            initialfile=f"{entry['name']}.png",
        )
        if not file_path:
            return
        try:
            if Image is not None:
                self.render_grid_to_image(entry["grid"]).save(file_path)
            else:
                self.write_photoimage_png(self.grid_to_photoimage(entry["grid"]), file_path)
        except Exception as error:
            messagebox.showerror("导出失败", str(error))
            return
        self.push_status(f"已导出当前素材 PNG：{file_path}")

    def export_frame_data(self):
        entry = self.current_entry()
        if entry is None:
            return
        file_path = filedialog.asksaveasfilename(
            title="导出索引矩阵 JSON",
            defaultextension=".json",
            filetypes=[("JSON 文件", "*.json")],
            initialfile=f"{entry['name']}.json",
        )
        if not file_path:
            return
        payload = {
            "name": entry["name"],
            "width": len(entry["grid"][0]),
            "height": len(entry["grid"]),
            "grid": entry["grid"],
        }
        with open(file_path, "w", encoding="utf-8") as file:
            json.dump(payload, file, ensure_ascii=False, indent=2)
        self.push_status(f"已导出索引矩阵：{file_path}")

    def save_project(self):
        file_path = filedialog.asksaveasfilename(
            title="保存项目",
            defaultextension=".json",
            filetypes=[("JSON 文件", "*.json")],
            initialfile=f"{self.asset_kind_var.get()}_project.json",
        )
        if not file_path:
            return
        payload = {
            "asset_kind": self.asset_kind_var.get(),
            "cell_width": int(self.cell_width_var.get()),
            "cell_height": int(self.cell_height_var.get()),
            "sheet_columns": int(self.sheet_columns_var.get()),
            "playback_fps": int(self.playback_fps_var.get()),
            "palette": [list(rgba) for rgba in self.palette],
            "frames": deepcopy(self.frames),
        }
        with open(file_path, "w", encoding="utf-8") as file:
            json.dump(payload, file, ensure_ascii=False, indent=2)
        self.push_status(f"项目已保存：{file_path}")

    def load_project(self):
        file_path = filedialog.askopenfilename(
            title="打开项目",
            filetypes=[("JSON 文件", "*.json")],
        )
        if not file_path:
            return
        try:
            with open(file_path, "r", encoding="utf-8") as file:
                payload = json.load(file)
        except Exception as error:
            messagebox.showerror("打开失败", f"读取项目失败：{error}")
            return

        try:
            self.asset_kind_var.set(payload["asset_kind"])
            self.cell_width_var.set(int(payload["cell_width"]))
            self.cell_height_var.set(int(payload["cell_height"]))
            self.sheet_columns_var.set(int(payload["sheet_columns"]))
            self.playback_fps_var.set(int(payload["playback_fps"]))
            self.palette = [tuple(rgba) for rgba in payload["palette"]]
            self.frames = payload["frames"]
            self.selected_index = 0
            self.current_color = min(self.current_color, len(self.palette) - 1)
            self.title_label.configure(text=MODE_PRESETS[self.asset_kind_var.get()]["title"])
        except Exception as error:
            messagebox.showerror("打开失败", f"项目格式不正确：{error}")
            return

        if self.asset_kind_var.get() == "character":
            self.preview_mode_var.set("animation")
        else:
            self.preview_mode_var.set("sheet")
        self.history.clear()
        self.refresh_all()
        self.push_status(f"已打开项目：{file_path}")

    def import_png_into_current(self):
        entry = self.current_entry()
        if entry is None:
            return
        if Image is None:
            messagebox.showerror("导入失败", "当前环境未安装 Pillow，无法读取 PNG")
            return
        file_path = filedialog.askopenfilename(
            title="导入 PNG 到当前素材",
            filetypes=[("PNG 图片", "*.png"), ("所有文件", "*.*")],
        )
        if not file_path:
            return
        try:
            image = Image.open(file_path).convert("RGBA")
        except Exception as error:
            messagebox.showerror("导入失败", f"无法打开图片：{error}")
            return

        target_width = len(entry["grid"][0])
        target_height = len(entry["grid"])
        if image.size != (target_width, target_height):
            should_resize = messagebox.askyesno(
                "尺寸不匹配",
                f"当前素材尺寸为 {target_width}x{target_height}，导入图片为 {image.width}x{image.height}。\n是否按最近邻缩放到当前尺寸？",
            )
            if not should_resize:
                return
            image = image.resize((target_width, target_height), Image.NEAREST)

        self.push_history_snapshot()
        grid = self.create_blank_grid(target_width, target_height)
        for y in range(target_height):
            for x in range(target_width):
                rgba = image.getpixel((x, y))
                palette_index = self.find_or_add_color(rgba)
                grid[y][x] = palette_index
        entry["grid"] = grid
        self.refresh_all()
        self.push_status(f"已将 PNG 导入到 {entry['name']}")

    def find_or_add_color(self, rgba):
        if rgba[3] == 0:
            return 0
        if rgba in self.palette:
            return self.palette.index(rgba)
        self.palette.append(rgba)
        return len(self.palette) - 1

    def start_preview_loop(self):
        self.refresh_preview(animated=True)

    def refresh_preview(self, animated=False):
        self.preview_canvas.delete("all")
        width = int(self.preview_canvas.cget("width"))
        height = int(self.preview_canvas.cget("height"))
        self.draw_checkerboard(self.preview_canvas, 0, 0, width, height, 20)

        if not self.frames:
            return

        scale = max(2, int(self.preview_scale_var.get()))
        preview_mode = self.preview_mode_var.get()

        if preview_mode == "animation":
            if self.asset_kind_var.get() == "character":
                entry = self.frames[self.preview_anim_index % len(self.frames)]
            else:
                entry = self.current_entry()
            self.draw_grid_preview(entry["grid"], scale, centered=True)
        else:
            columns = max(1, int(self.sheet_columns_var.get()))
            cell_width = len(self.frames[0]["grid"][0]) * scale
            cell_height = len(self.frames[0]["grid"]) * scale
            total_width = columns * cell_width
            rows = math.ceil(len(self.frames) / columns)
            total_height = rows * cell_height
            offset_x = max(10, (width - total_width) // 2)
            offset_y = max(10, (height - total_height) // 2)
            for index, entry in enumerate(self.frames):
                draw_x = offset_x + (index % columns) * cell_width
                draw_y = offset_y + (index // columns) * cell_height
                self.draw_grid_preview(entry["grid"], scale, x0=draw_x, y0=draw_y, centered=False)

        if self.preview_job is not None:
            self.root.after_cancel(self.preview_job)
            self.preview_job = None

        fps = max(1, int(self.playback_fps_var.get()))
        if preview_mode == "animation" and self.asset_kind_var.get() == "character":
            self.preview_job = self.root.after(max(60, int(1000 / fps)), self.advance_preview_animation)
        elif animated:
            self.preview_job = self.root.after(300, self.start_preview_loop)

    def advance_preview_animation(self):
        self.preview_anim_index = (self.preview_anim_index + 1) % max(1, len(self.frames))
        self.refresh_preview()

    def draw_grid_preview(self, grid, scale, x0=0, y0=0, centered=True):
        grid_height = len(grid)
        grid_width = len(grid[0]) if grid_height else 0
        width = grid_width * scale
        height = grid_height * scale

        if centered:
            canvas_width = int(self.preview_canvas.cget("width"))
            canvas_height = int(self.preview_canvas.cget("height"))
            x0 = (canvas_width - width) // 2
            y0 = (canvas_height - height) // 2

        for y, row in enumerate(grid):
            for x, color_index in enumerate(row):
                rgba = self.palette[color_index]
                if rgba[3] == 0:
                    continue
                fill = f"#{rgba[0]:02x}{rgba[1]:02x}{rgba[2]:02x}"
                self.preview_canvas.create_rectangle(
                    x0 + x * scale,
                    y0 + y * scale,
                    x0 + (x + 1) * scale,
                    y0 + (y + 1) * scale,
                    fill=fill,
                    outline=fill,
                )

    def push_status(self, message):
        self.status_var.set(message)


def main():
    root = tk.Tk()
    AssetStudioApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()

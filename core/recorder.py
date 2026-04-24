"""录制与截图管理"""

import os
import time
import threading
import cv2 as _cv2
import numpy as np
from OpenGL.GL import *
from typing import Optional


class Recorder:
    """管理屏幕录制和截图捕获"""

    def __init__(self, param_manager, audit_logger):
        self._params = param_manager
        self._audit = audit_logger
        self._recorder: Optional[_cv2.VideoWriter] = None
        self._recording = False
        self._pending_screenshot = False
        self._record_interval = 1.0 / 30.0
        self._record_next_t = 0.0

    @property
    def is_recording(self) -> bool:
        return self._recording

    @property
    def pending_screenshot(self) -> bool:
        return self._pending_screenshot

    def start_recording(self) -> None:
        import pygame
        w, h = pygame.display.get_window_size()
        fmt_idx = self._params.get_param("recording_format") or 0
        fmt_map = {0: 'mp4v', 1: 'XVID', 2: 'XVID'}
        ext_map = {0: '.mp4', 1: '.mkv', 2: '.avi'}
        fourcc = _cv2.VideoWriter_fourcc(*fmt_map.get(fmt_idx, 'mp4v'))
        fps = self._params.get_param("stream_fps") or 30
        ts = time.strftime("%Y%m%d_%H%M%S")
        save_dir = self._params.get_param("save_dir") or "."
        path = os.path.join(save_dir, "recordings", f"rec_{ts}{ext_map.get(fmt_idx, '.mp4')}")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self._recorder = _cv2.VideoWriter(path, fourcc, fps, (w, h))
        self._recording = True
        self._record_interval = 1.0 / fps
        self._record_next_t = time.monotonic()
        print(f"[Recorder] Recording started: {path}")
        self._audit.log("recording", f"start: {path}")

    def stop_recording(self) -> None:
        if self._recorder:
            self._recorder.release()
            self._recorder = None
        self._recording = False
        print("[Recorder] Recording stopped")
        self._audit.log("recording", "stop")

    def request_screenshot(self) -> None:
        self._pending_screenshot = True

    def open_save_folder(self) -> None:
        """弹出文件夹选择对话框（后台线程，不阻塞主循环）"""
        def _pick():
            try:
                import tkinter as tk
                from tkinter import filedialog
                from config import _asset
                root = tk.Tk()
                root.withdraw()
                root.attributes("-topmost", True)
                # 设置窗口图标（优先 icon.png，fallback icon.ico）
                try:
                    _icon_png = _asset("assets/icon.png")
                    _icon_ico = _asset("assets/icon.ico")
                    if os.path.exists(_icon_png):
                        _img = tk.PhotoImage(file=_icon_png)
                        root.iconphoto(True, _img)
                    elif os.path.exists(_icon_ico):
                        root.iconbitmap(_icon_ico)
                except Exception:
                    pass
                folder = filedialog.askdirectory(title="选择保存目录")
                root.destroy()
                if folder:
                    self._params.set_param("save_dir", folder)
                    print(f"[Recorder] Save dir: {folder}")
            except Exception as e:
                print(f"[Recorder] Folder picker error: {e}")
        threading.Thread(target=_pick, daemon=True).start()

    def grab_gl_frame(self) -> np.ndarray:
        """读取当前 GL 帧缓冲，叠加鼠标光标"""
        import pygame
        w, h = pygame.display.get_window_size()
        data = glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE)
        frame = np.frombuffer(data, dtype=np.uint8).reshape(h, w, 3)
        frame = np.flipud(frame).copy()

        mx, my = pygame.mouse.get_pos()
        if 0 <= mx < w and 0 <= my < h:
            _cv2.circle(frame, (mx, my), 4, (0, 220, 255), -1, lineType=_cv2.LINE_AA)
            overlay = frame.copy()
            for dx, dy in ((0, -9), (0, 9), (-9, 0), (9, 0)):
                px, py = mx + dx, my + dy
                if 0 <= px < w and 0 <= py < h:
                    _cv2.circle(overlay, (px, py), 2, (0, 220, 255), -1, lineType=_cv2.LINE_AA)
            _cv2.addWeighted(overlay, 0.45, frame, 0.55, 0, frame)

        return frame

    def process_frame(self, now: float) -> None:
        """按帧率写入录制帧，并处理待处理的截图请求"""
        need_record = self._recording and self._recorder and now >= self._record_next_t
        if not need_record and not self._pending_screenshot:
            return

        gl_frame = self.grab_gl_frame()

        if need_record:
            self._recorder.write(gl_frame)
            self._record_next_t += self._record_interval
            if self._record_next_t < now:
                self._record_next_t = now + self._record_interval

        if self._pending_screenshot:
            self._pending_screenshot = False
            save_dir = self._params.get_param("save_dir") or "."
            shot_dir = os.path.join(save_dir, "screenshots")
            os.makedirs(shot_dir, exist_ok=True)
            ts = time.strftime("%Y%m%d_%H%M%S")
            path = os.path.join(shot_dir, f"shot_{ts}.png")
            _cv2.imwrite(path, gl_frame)
            print(f"[Recorder] Screenshot saved: {path}")
            self._audit.log("screenshot", path)

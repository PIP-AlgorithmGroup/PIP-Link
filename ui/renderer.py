"""UI Renderer - OpenGL video rendering"""

import pygame
from pygame.locals import *
from OpenGL.GL import *
from OpenGL.GLU import *
import numpy as np
from typing import Optional


class VideoRenderer:
    """Video renderer"""

    def __init__(self, width: int, height: int):
        self.width = width
        self.height = height
        self.texture_id: Optional[int] = None
        self.frame_data: Optional[np.ndarray] = None
        self.texture_initialized = False

    def init_texture(self):
        """Initialize texture (cleans up old texture if any)"""
        if self.texture_id is not None:
            try:
                glDeleteTextures([self.texture_id])
            except Exception:
                pass
        self.texture_id = glGenTextures(1)
        self.texture_initialized = False
        self.frame_data = None
        glBindTexture(GL_TEXTURE_2D, self.texture_id)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE)

    def update_frame(self, frame_data: np.ndarray):
        """Update frame data"""
        if frame_data is None:
            return

        glBindTexture(GL_TEXTURE_2D, self.texture_id)

        # First frame uses glTexImage2D to allocate VRAM, subsequent frames use glTexSubImage2D
        if not self.texture_initialized:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, self.width, self.height, 0, GL_BGR, GL_UNSIGNED_BYTE, frame_data)
            self.texture_initialized = True
        else:
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, self.width, self.height, GL_BGR, GL_UNSIGNED_BYTE, frame_data)

        self.frame_data = frame_data

    def render(self):
        """Render video"""
        if self.frame_data is None:
            return

        glLoadIdentity()

        # Bind texture
        glBindTexture(GL_TEXTURE_2D, self.texture_id)

        # Draw fullscreen quad
        glBegin(GL_QUADS)
        glTexCoord2f(0, 1)
        glVertex3f(-1, -1, 0)
        glTexCoord2f(1, 1)
        glVertex3f(1, -1, 0)
        glTexCoord2f(1, 0)
        glVertex3f(1, 1, 0)
        glTexCoord2f(0, 0)
        glVertex3f(-1, 1, 0)
        glEnd()

        # Unbind texture
        glBindTexture(GL_TEXTURE_2D, 0)

    def cleanup(self):
        """Clean up resources"""
        if self.texture_id is not None:
            glDeleteTextures([self.texture_id])
            self.texture_id = None

    def get_resolution(self) -> tuple:
        """Get resolution"""
        return (self.width, self.height)

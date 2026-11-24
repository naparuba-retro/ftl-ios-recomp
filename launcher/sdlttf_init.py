# -*- coding: utf-8 -*-
import sdl2.sdlttf as ttf

def init():
    if ttf.TTF_WasInit() == 0 and ttf.TTF_Init() != 0: raise RuntimeError("SDL2_ttf indisponible")

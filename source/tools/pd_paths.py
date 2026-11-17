r"""Where the build tools find YOUR OWN copy of Phantom Dust (independently authored for this project).
Set the environment variable PD_GAME to the game's folder (the one holding Assets\Data), for example
    set PD_GAME=D:\PhantomDust
The tools only READ your game files; they never change them. Nothing of the game is in this repository."""
import os

GAME = os.environ.get('PD_GAME', '')
DATA = os.path.join(GAME, 'Assets', 'Data')
MAP = os.path.join(DATA, 'Map')

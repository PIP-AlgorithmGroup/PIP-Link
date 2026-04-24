"""主入口"""

import multiprocessing
from core.app import Application

if __name__ == "__main__":
    multiprocessing.freeze_support()
    app = Application()
    app.run()

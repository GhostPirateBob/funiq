import pyfiglet
from tqdm import tqdm
from rich.console import Console
from rich.progress import track
import time

# ---------- 1. TQDM Progress Bar ----------
print("\n[1] Loading with tqdm:")
for _ in tqdm(range(50), desc="Loading..."):
    time.sleep(0.02)

# ---------- 2. Rich Progress Bar ----------
console = Console()
print("\n[2] Fancy Progress with Rich:")
for step in track(range(10), description="Processing tasks..."):
    time.sleep(0.1)


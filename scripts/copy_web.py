Import("env")

from pathlib import Path
import shutil


def copy_companion_app(source, target, env):
    project = Path(env["PROJECT_DIR"]).parent
    app_dir = project / "companion-app"
    data_dir = Path(env["PROJECT_DATA_DIR"])
    data_dir.mkdir(parents=True, exist_ok=True)
    for old in data_dir.iterdir():
        if old.name == ".gitkeep":
            continue
        if old.is_dir():
            shutil.rmtree(old)
        else:
            old.unlink()
    for item in app_dir.iterdir():
        destination = data_dir / item.name
        if item.is_dir():
            shutil.copytree(item, destination)
        else:
            shutil.copy2(item, destination)
    print(f"Buddy: copied web app from {app_dir} to {data_dir}")


env.AddPreAction("buildfs", copy_companion_app)
env.AddPreAction("uploadfs", copy_companion_app)

Import("env")

from pathlib import Path
import shutil


def install_guardian_frontend():
    project = Path(env.subst("$PROJECT_DIR"))
    dependency = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "SensESP"
    generated = project / "webui" / "generated" / "frontend_files.h"
    destination = dependency / "src" / "sensesp" / "net" / "web" / "autogen" / "frontend_files.h"
    if not generated.exists():
        raise RuntimeError("Missing webui/generated/frontend_files.h")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(generated, destination)


install_guardian_frontend()

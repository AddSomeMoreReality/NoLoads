"""Start Ghidra in-process (PyGhidra, headless) and open a program of a project. Runs under gconfig.VENV_PYTHON."""
import contextlib
import pathlib

import gconfig


def start_jvm(heap=None):
    import pyghidra
    from pyghidra.launcher import HeadlessPyGhidraLauncher
    if pyghidra.started():
        return
    launcher = HeadlessPyGhidraLauncher(install_dir=pathlib.Path(gconfig.GHIDRA_DIR))
    launcher.java_home = pathlib.Path(gconfig.JAVA_HOME)
    launcher.add_vmargs(f"-Xmx{heap or gconfig.SERVER_HEAP}")
    launcher.start()


@contextlib.contextmanager
def open_program(project_dir, project_name, program_path, read_only):
    """Yield (project, program). The project lock is held until exit; read-only programs are never saved."""
    from ghidra.base.project import GhidraProject
    folder, _, name = program_path.rpartition("/")
    project = GhidraProject.openProject(project_dir, project_name, True)
    try:
        program = project.openProgram(folder or "/", name, read_only)
        try:
            yield project, program
        finally:
            if not read_only:
                project.save(program)
            project.close(program)
    finally:
        project.close()

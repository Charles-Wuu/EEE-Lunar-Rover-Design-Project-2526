# Materials Inventory

This file records how the desktop project materials were organised for GitHub.

## Included

- `3D Printing/Fully_Assembled_Rover.stl` -> `mechanical/cad/fully_assembled_rover.stl`
- `3D Printing/assembled_rover_separate_stls/*.stl` -> `mechanical/cad/parts/*.stl`
- `Software/app.py` -> `software/web/rover_server.py`
- `Software/index.html`, `control.html`, `detect.html` -> `software/web/`
- `Software/control_monitor_combined/*.ino` -> `firmware/arduino/final/`
- `Software/control_final_worked/*.ino` -> `firmware/arduino/tests/control_final_worked/`
- `Software/monitor_worked_test/*.ino` -> `firmware/arduino/tests/monitor_worked_test/`
- existing repository RF sketch -> `firmware/arduino/tests/radio_freq_code/`
- `Report Writing/ELEC40006-Project_Report_Group_25.pdf` -> `docs/report/final/`
- `Downloads/Group Project Interim Presentation 2026.pdf` -> `docs/presentation/`

## Excluded

- `.DS_Store` files - macOS metadata.
- `Software/.venv/` - local Python virtual environment.
- all files under `Software/草稿/`.
- all files under `Report Writing/草稿/`.

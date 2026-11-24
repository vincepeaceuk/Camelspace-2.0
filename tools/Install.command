#!/bin/zsh
set -eu
task_package_dir="${0:A:h}"
python3 "$task_package_dir/install.py"

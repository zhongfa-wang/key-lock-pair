import json
import os


def dump_stats(file_path: str, stats_dict: dict):
    try:
        ipc = stats_dict["system"]["cpu"]["ipc"]
        custom_stats = {"experiment_name": "test", "ipc": ipc}
    except KeyError as e:
        print(f"Error! Cant find key {e}!")
        custom_stats = {"experiment_name": "test", "error": "Stats not found!"}

    out_file = os.path.join(file_path)
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(custom_stats, f, indent=4)

import json
import os
import glob
import copy

def remove_key_from_json(filepath, key_to_remove):
    made_change = False
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
            if content.startswith('\ufeff'): # Handle BOM
                content = content[1:]
            original_data = json.loads(content)

        data_to_modify = copy.deepcopy(original_data)

        if key_to_remove in data_to_modify:
            del data_to_modify[key_to_remove]
            if data_to_modify != original_data: # Check if a change actually occurred
                with open(filepath, 'w', encoding='utf-8') as f:
                    json.dump(data_to_modify, f, indent=4) # Removed sort_keys
                made_change = True
    except json.JSONDecodeError:
        # print(f"Error decoding JSON from {filepath}. Skipping.")
        pass # Reduce noise
    except Exception as e:
        print(f"An error occurred with {filepath} during remove: {e}")
    return made_change

def add_key_to_filament_json(filepath, key_to_add, default_value, anchor_key="filament_flow_ratio"):
    made_change = False
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
            if content.startswith('\ufeff'): # Handle BOM
                content = content[1:]
            original_data = json.loads(content)

        if key_to_add in original_data and isinstance(original_data.get(key_to_add), list) and original_data.get(key_to_add) == default_value:
            return False # Key exists, is a list, and has the correct default value, no change needed

        reconstructed_data = {}
        key_inserted_or_updated = False

        if key_to_add not in original_data: # Key needs to be added
            for k, v in original_data.items():
                reconstructed_data[k] = v
                if k == anchor_key:
                    reconstructed_data[key_to_add] = default_value
                    key_inserted_or_updated = True
            if not key_inserted_or_updated: # Anchor key not found, add at the end
                reconstructed_data[key_to_add] = default_value
                key_inserted_or_updated = True
        elif not isinstance(original_data.get(key_to_add), list) or original_data.get(key_to_add) != default_value : # Key exists but needs type correction or value update
            for k, v in original_data.items():
                if k == key_to_add:
                    reconstructed_data[k] = default_value
                    key_inserted_or_updated = True
                else:
                    reconstructed_data[k] = v
            if not key_inserted_or_updated : # Should not happen if key_to_add was in original_data
                 reconstructed_data[key_to_add] = default_value
                 key_inserted_or_updated = True


        if key_inserted_or_updated:
            # Check if the reconstructed data is actually different from original
            # This covers cases where the key was already there with the correct value but wrong type,
            # or if the only change would be ordering (which we don't force if content is same)
            if reconstructed_data != original_data:
                with open(filepath, 'w', encoding='utf-8') as f:
                    json.dump(reconstructed_data, f, indent=4) # Removed sort_keys
                made_change = True

    except json.JSONDecodeError:
        # print(f"Error decoding JSON from {filepath}. Skipping.")
        pass # Reduce noise
    except Exception as e:
        print(f"An error occurred with {filepath} during add/update: {e}")
    return made_change


def process_print_profiles(base_path, key_to_remove):
    print(f"\nProcessing print profiles in {base_path} to remove '{key_to_remove}':")
    changed_files_count = 0
    error_files_count = 0

    for dirpath, dirnames, filenames in os.walk(base_path):
        path_parts = dirpath.split(os.sep)
        is_filament_subdir = False
        if 'filament' in path_parts:
            try:
                base_path_basename = os.path.basename(base_path.rstrip(os.sep))
                indices = [i for i, part in enumerate(path_parts) if part == base_path_basename]
                if indices:
                    last_base_path_index = indices[-1]
                    if 'filament' in path_parts[last_base_path_index+1:]:
                        filament_index_relative_to_base = path_parts[last_base_path_index+1:].index('filament')
                        if filament_index_relative_to_base == 1:
                            is_filament_subdir = True
            except ValueError:
                pass

        if is_filament_subdir:
            continue

        for filename in filenames:
            if filename.endswith(".json"):
                filepath = os.path.join(dirpath, filename)
                try:
                    if remove_key_from_json(filepath, key_to_remove):
                        changed_files_count += 1
                except Exception:
                    error_files_count += 1

    print(f"Finished processing print profiles. Key removed from {changed_files_count} files. Errors in {error_files_count} files.")
    return changed_files_count

def process_filament_profiles(base_path, key_to_add, default_value):
    print(f"\nProcessing filament profiles in {base_path} to add '{key_to_add}':")
    filament_profile_paths = []
    for vendor_dir_name in os.listdir(base_path):
        vendor_path = os.path.join(base_path, vendor_dir_name)
        if os.path.isdir(vendor_path):
            filament_dir_path = os.path.join(vendor_path, "filament")
            if os.path.isdir(filament_dir_path):
                for filename in glob.glob(os.path.join(filament_dir_path, "*.json")):
                    filament_profile_paths.append(filename)

    changed_files_count = 0
    error_files_count = 0
    for filepath in filament_profile_paths:
        try:
            if add_key_to_filament_json(filepath, key_to_add, default_value):
                changed_files_count +=1
        except Exception:
            error_files_count += 1

    print(f"Finished processing filament profiles. Key added/updated in {changed_files_count} files. Errors in {error_files_count} files.")
    return changed_files_count

if __name__ == "__main__":
    profiles_base_path = "resources/profiles"
    key_name = "top_solid_infill_flow_ratio"

    abs_profiles_path = os.path.abspath(profiles_base_path)
    print(f"Script starting. Working directory: {os.getcwd()}")
    print(f"Absolute profiles base path: {abs_profiles_path}")

    if not os.path.isdir(abs_profiles_path):
        print(f"Error: Profiles directory '{abs_profiles_path}' not found. Exiting.")
    else:
        print("Starting removal process...")
        num_removed = process_print_profiles(profiles_base_path, key_name)
        print(f"Script attempted to write to {num_removed} files for key removal.")

        print("\nStarting addition/correction process...")
        num_added = process_filament_profiles(profiles_base_path, key_name, [1.0])
        print(f"Script attempted to write to {num_added} files for key addition/correction.")

        print(f"\nTotal files intended for writing by script: {num_removed + num_added}")
    print("Profile modification script finished.")

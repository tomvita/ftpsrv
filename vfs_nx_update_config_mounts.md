# `vfs_nx_update_config_mounts` Function Documentation

This document explains the inner workings of the `vfs_nx_update_config_mounts` function in `vfs_nx.c`. This function is responsible for dynamically managing virtual mount points within the application, primarily for cheat loading and application-specific data access. It reacts to changes in the running application and values in the `/switch/breeze/config.ini` file.

## Core Functionality

The function's main purpose is to create and manage several dynamic mount points that provide convenient access to cheat files and application content directories. It achieves this by monitoring the currently running application (Title ID or TID) and specific settings in the configuration file.

The function can be broken down into three main parts:

### 1. Current Application (TID) Detection

- The function first determines the Title ID (TID) of the currently running application.
- It uses the `pmdmnt` and `pminfo` services to get the process ID and then the program ID (TID).
- If the home menu (qlaunch) is running, a static TID (`0x0100000000001000`) is used.

### 2. Dynamic Mounts Based on TID

- The function tracks the last known TID. If the TID changes (e.g., the user launches a new game), the function performs the following actions:
    1.  **Unmounts Old Paths**: It unmounts and removes the virtual file system devices associated with the previous application. This includes:
        - `ams_tid_mount`
        - `breeze_tid_mount`
        - A mount point named after the previous application's name.
        - `breeze_cheat_dir`
        - `atmosphere_cheat_dir`
    2.  **Updates Configuration**: The new TID is written to the `config.ini` file under the `[Nx]` section for the `save_application_id` and `bcat_application_id` keys. This persists the last-run game's TID.
    3.  **Mounts New Paths**: It then creates new mount points for the current application:
        - `ams_tid_mount`:  Points to `/atmosphere/contents/<current_tid>/`
        - `breeze_tid_mount`: Points to `/switch/breeze/cheats/<current_tid>/`
        - `<app_name>`: Points to `/switch/breeze/cheats/<app_name>/`. The application name is retrieved and sanitized to be used as a valid device name.

### 3. Configuration-Driven Mounts

The function also updates mount points based on values in the `config.ini` file. These updates only occur if no game is currently running (i.e., the user is in the home menu).

- **`game_cheat_dir`**:
    - It reads the `game_cheat_dir` value from the `[Nx]` section of the `config.ini` file.
    - If this value changes, it updates the `breeze_cheat_dir` mount to point to `/switch/breeze/cheats/<game_cheat_dir>/`. This allows a user to manually specify a cheat folder to be mounted.

- **`atmosphere_cheat_dir`**:
    - It reads the `save_application_id` from the `config.ini` file.
    - It constructs a path: `/atmosphere/contents/<save_application_id>/cheats`.
    - If this path is different from the currently mounted path for `atmosphere_cheat_dir`, it updates the mount. This is useful for accessing the cheat folder for the last-played game from the home menu.

## Summary of Mount Points

| Mount Name             | Path                                                     | Update Trigger                               |
| ---------------------- | -------------------------------------------------------- | -------------------------------------------- |
| `ams_tid_mount`        | `/atmosphere/contents/<tid>/`                            | Running application (TID) changes.           |
| `breeze_tid_mount`     | `/switch/breeze/cheats/<tid>/`                           | Running application (TID) changes.           |
| `<app_name>`           | `/switch/breeze/cheats/<app_name>/`                      | Running application (TID) changes.           |
| `breeze_cheat_dir`     | `/switch/breeze/cheats/<game_cheat_dir>/`                | `game_cheat_dir` in `config.ini` changes.    |
| `atmosphere_cheat_dir` | `/atmosphere/contents/<save_application_id>/cheats/`     | `save_application_id` in `config.ini` changes the path. |

## Code Reference

The relevant code is located in [`src/platform/nx/vfs_nx.c`](src/platform/nx/vfs_nx.c:305) in the `vfs_nx_update_config_mounts` function.
## Triggering on Login

To trigger this function automatically upon a successful user login, you need to set the `login_callback` in the `FtpSrvConfig` struct. The `vfs_nx_update_config_mounts` function is the ideal candidate for this callback.

The configuration is initialized in both `main.c` and `main_sysmod.c`. You will need to apply the change in the relevant file for your build target.

### In `src/platform/nx/main.c`:

Locate the `g_ftpsrv_config` definition and add the `login_callback`:

```c
static struct FtpSrvConfig g_ftpsrv_config = {
    .user = "anonymous",
    .pass = "anonymous",
    .anon = 1,
    .port = 5000,
    .timeout = 30,
    .use_localtime = 0,
    .login_callback = vfs_nx_update_config_mounts, // Add this line
};
```

### In `src/platform/nx/main_sysmod.c`:

Similarly, update the `g_ftpsrv_config` definition in this file:

```c
static struct FtpSrvConfig g_ftpsrv_config = {
    .user = "anonymous",
    .pass = "anonymous",
    .anon = 1,
    .port = 5000,
    .timeout = 30,
    .use_localtime = 0,
    .login_callback = vfs_nx_update_config_mounts, // Add this line
};
```

By making this change, `vfs_nx_update_config_mounts` will be executed every time a user successfully logs in, ensuring that all dynamic mounts are immediately updated.
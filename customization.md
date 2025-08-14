# Customization Tasks

## Configuration Changes
- Change `INI_PATH` to `/switch/breeze/config.ini`
- Change title id of sysmodule to `420000000000012B`
- Add `mount_breeze_devices` to `config.ini`. This will be a boolean that is OR'd with `mount_devices` to determine which devices to mount.

## New Mountable Devices (`mount_breeze_devices`)
The `mount_breeze_devices` option will mount the following:
- `sdmc`
- `save`
- `switch`
- `atmosphere_contents`
- `album_nand`
- `album_sd`
- `breeze`
- `current_game_save`
- `current_game_cheats`
- `album_nand_today`
- `album_sd_today`

## Detailed Descriptions

### `breeze`
- A new mount point, similar to `switch`.
- Path: `/switch/breeze`

### `current_game_save`
- A new mount point, similar to `save`.
- It screens for a `tid` that matches the running game's `tid` (`current_game_tid`).
- The method to get this is demonstrated in `ftp_custom_cmd_TID`.
- `current_game_tid` and `current_game_name` are obtained when `current_game_save` is opened.
- These two values are to be saved/updated in the ini file.
- If there is no game running (i.e. `tid`=0 or `tid`=QLAUNCH_TID), these two values shall be loaded from the ini file.

### `current_game_cheats`
- A new mount point.
- When opened, `current_game_tid` and `current_game_name` shall be obtained and preserved, similar to `current_game_save`.
- It shall contain three directories:
    - `breeze_tid_cheats`: Opens the directory `/switch/breeze/cheats/%016LX` where `%016LX` is the `current_game_tid`.
    - `breeze_name_cheats`: Opens the directory `/switch/breeze/cheats/%s` where `%s` is the sanitized `current_game_name`.
    - `atmosphere_tid_cheats`: Opens the directory `/atmosphere/contents/%016LX` where `%016LX` is the `current_game_tid`.

### `album_nand_today` and `album_sd_today`
- New mount points.
- When opened, `current_day_path` is evaluated:
  ```c
  time_t now = time(NULL);
  struct tm* local_time = localtime(&now);
  static char current_day_path[16];
  sprintf(current_day_path, "/%04d/%02d/%02d", local_time->tm_year + 1900, local_time->tm_mon + 1, local_time->tm_mday);
  ```
- The path of `album_nand` and `album_sd` are to be extended by `current_day_path`.

### `sanitize_fs_name`
- Method to sanitize a name for the filesystem.
  ```c
  void sanitize_fs_name(char* name) {
      if (!name) return;
      char* d = name;
      char* s = name;
      while (*s) {
          if (((unsigned char)*s) >= 0x80) {
              // "®" is 0xC2 0xAE
              if ((unsigned char)*s == 0xC2 && (unsigned char)*(s + 1) == 0xAE) {
                  s += 2;
                  continue;
              }
              // "–" is 0xE2 0x80 0x93
              if ((unsigned char)*s == 0xE2 && (unsigned char)*(s + 1) == 0x80 && (unsigned char)*(s + 2) == 0x93) {
                  s += 3;
                  continue;
              }
              // "™" is 0xE2 0x84 0xA2
              if ((unsigned char)*s == 0xE2 && (unsigned char)*(s + 1) == 0x84 && (unsigned char)*(s + 2) == 0xA2) {
                  s += 3;
                  continue;
              }
              // "é" is 0xC3 0xA9
              if ((unsigned char)*s == 0xC3 && (unsigned char)*(s + 1) == 0xA9) {
                  *d++ = 'e';
                  s += 2;
                  continue;
              }

              // Generic multi-byte char handling from GetLegacySanitizedTitleName
              *d++ = '\'';
              if ((*s & 0xE0) == 0xC0)
                  s += 2;
              else if ((*s & 0xF0) == 0xE0)
                  s += 3;
              else if ((*s & 0xF8) == 0xF0)
                  s += 4;
              else
                  s++;  // Should not happen with valid UTF-8
          } else {
              switch (*s) {
                  case ':':
                  case '\\':
                  case '/':
                  case '"':
                  case '-':
                      s++;
                      break;
                  // case ' ':
                  //     *d++ = '_';
                  //     s++;
                  //     break;
                  default:
                      *d++ = *s++;
                      break;
              }
          }
      }
      *d = '\0';
  }

  The current implementation at vfs_nx_init concerning enable_devices and mount_breeze_devices is wrong, there is a set for enable device and there is a set for mount_breeze_devices, there is overlap and there are exclusive ones, each of them are to enable their own exclusive ones and concerning the common ones one of them enabled means it is enabled is the desired behaviour
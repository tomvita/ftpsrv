## ftpsrv 1.2.2 with mount breeze devices 1.0 Release Notes

It is now based on the latest source from `ITotalJustice/ftpsrv:master`.

### Custom Mount Points for Breeze Users
- To enable this feature, set `mount_breeze_devices = 1` in `/breeze/config.ini`.

### Dynamic Cheat Directories
These cheat directory mounts now automatically point to the directories of the last game detected by the ftp server or breeze upon FTP login:
- `current_game_ams_tid_dir:` atmosphere's content directory for title id of game, cheat files placed here will auto load when the game launch.
- `current_game_breeze_tid_dir:` breeze's cheats directory for title id of game, `Load Cheats from file` in `Cheats menu` will load from here if `use titleid` is 1 in `Settings menu`.
- `current_game_save:` save directories screened by title id of game.
- `{title name}:` breeze's cheats directory with sanitized title name of game, `Load Cheats from file` in `Cheats menu` will load from here if `use titleid` is 0 in `Settings menu`.

### Today's Album Shortcut
Mount points adjusted dynamically upon FTP login.
- `album_nand_today:`
- `album_sd_today:`

### Security Note
- Anonymous login for sys-ftp-breeze is disabled by default.
- If both username and password are left blank **and** `anon = 0` in the config, the server will **refuse to start** to avoid potential security risks, especially related to `ldn-mitm`.
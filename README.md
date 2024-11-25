# ftpsrv

small, fast, single threaded ftp implementation in C.

it uses no dynamic memory allocation, has very low memory footprint (the size of everything can be configured at build time) and uses poll() (or select() if poll isn't available) to allow for a responsive single threaded server with very low overhead.

i created ftpsrv so learn about the ftp protocal.

## platforms

i have ported ftpsrv to a few platforms that i regularly use, such as:

- nintendo switch, both as an application and sys-module.
- nintendo wii.
- nintendo 3ds.
- nintendo ds.

i do plan to port it to the ps4 at some point, just haven't had the time to learn how to make a dameon (background service), if anyone has any guides / examples, please open an issue!

The 3ds port needs some further work on the filesystem front. Currently it used posix io which works fine, but there is overhead in doing so. Using the fs service directly is more performent.

## config

the config is located in /config/ftpsrv/config.ini.
All releases come with `/config/ftpsrv/config.ini.template`, as to not overwrite existing configs on each update.

The Nintendo Switch port requires a user and password to be set, or, set `anon=1`. This is due to security concerns when paired with ldn-mitm as a user could modify your sd card if no user/pass is set.

## building

you need to install devkitpro along with cmake.

### switch

```sh
cmake --preset switch
cmake --build --preset switch
```

### wii

```sh
cmake --preset wii
cmake --build --preset wii
```

### 3ds

```sh
cmake --preset 3ds
cmake --build --preset 3ds
```

### nds

```sh
pacman -S dswifi
cmake --preset nds
cmake --build --preset nds
```

NOTE: as of 25/11/24, dswifi doesn't work with WPS. I have fixed this, but devkitpro is very hostile towards developers and blocks them from submitting patches, so i can't submit a fix. The nds build in releases is compiled with the fix, so WPS will work.

## LIST kde-dolphin bug workaround

LIST command on a file will not send pathname back in the listing due to kdolphin breaking (for some reason).

This bug can be observed when viewing a dir with .jpg/.png inside. It will issue a CWD with the filename to check if its a dir or not. When that fails, it will issue LIST with the filename, which i return info about the file, including the name (exactly as it's passed in). This will cause kde to stop doing anything with the file...

If you dont return the filename, or, return an error code with LIST then kde will send a SIZE and RETR and function normally.

## Ffmpeg ABOR workaround

due to the streaming nature, multiple send() can be recived by a single recv().
Normally, a client would not issue multiple commands, the exception being ABOR, which can be sent whenever it wants, even during the middle of a transfer.

Due to this, i split the commands and it's arguments by the ending "\r\n" which should work across clients, and it satisfies ffmpeg/mpv which seems to use ABOR during every transfer, causing a hang on ffmpeg's side due to it waiting for the response from ESPV.

## list of allowed un-authenticated commands

- USER
- PASS
- ACCT
- REIN
- QUIT
- ABOR
- SYST
- HELP
- NOOP
- FEAT

## notes

- in RFC959, PWD is shown to not return 530, meaning that it should be allowed for un-authenticated access. In my implementation however, i require it to be be authentiacted and will return 530.

## todo

- validate commands in order (RNTO happens after RNFR)
- add tls support
- add ACCT
- add SMNT
- add REIN
- add STOU
- add ALLO
- add SITE
- add STAT
- add HELP

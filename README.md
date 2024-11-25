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
- add ini support in main() to load config
- add tls support
- add ACCT
- add SMNT
- add REIN
- add STOU
- add ALLO
- add SITE
- add STAT
- add HELP

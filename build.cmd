copy apple2\template.dsk cc65-Chess.dsk
java -jar apple2\AppleCommander-win64-1.5.0.jar -p  cc65-Chess.dsk chess.system sys < \cc65\target\apple2\util\loader.system
java -jar apple2\AppleCommander-win64-1.5.0.jar -as cc65-Chess.dsk chess        bin < cc65-Chess.apple2

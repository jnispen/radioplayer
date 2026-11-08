## RadioPlayer

A minimal application to play internet radio streams.

The RadioPlayer application reads internet radio stream URL's from a text file and plays them using the commandline 'mpg123' or 'mplayer' audio player. Stream playback is controlled via an IR remote by reading keyevents from an HID input device.

The application was created on a Raspberry Pi 3 running Raspbian GNU/Linux 11 (bullseye), mounted with a HiFiBerry DAC+. IR remote control button events are read from a FLIRC USB Universal Remote Control Receiver device.

![rpi](img/rpi.jpg)

### 1. Flirc Setup
If using a Flirc USB receiver, first setup the IR remote control button key-mapping:
https://docs.flirc.io/flirc-usb/

For Debian the Flirc cmdline tools can be downloaded from (use armhf arch):
http://apt.flirc.tv/arch/

To control playback, the application uses the IR remote control buttons 1-9 (play entries 0-8), 0 (stop playback), arrow-left (previous entry), arrow-right (next entry), and `R` (play a random entry). A separate button mapped to the `M` key switches between radio and music modes. These key events are read from the USB HID input device. In either mode, the number, arrow, and `R` buttons operate on that mode's list; switching modes stops playback. In music mode, songs advance automatically from the selected entry and the list repeats from the beginning after the last song.

### 2. Testing the USB Universal Remote Control Receiver

After the remote control key-mapping has been setup, test key event reception with the read_keyevent application.

Compile the file:
```
gcc -Wall -Wextra -o read_keyevent read_keyevent.c
```

Example usage:
```
read_keyevent /dev/input/event0
```

### 3.Audio Players
#### 3.1 mpg123
For stream playback the (MP3) audio player 'mpg123' is used by default. Project details can be found here: https://mpg123.org

#### 3.2 mplayer
If desired an alternative audio player 'mplayer' can be configured. This player has the advantage that is able to playback many more audio formats next to MP3, like for example AAC, which is also often used as an audio streaming format. Project details can be found here: https://mplayerhq.hu/design7/info.html

### 4. Searching for Internet Radio Streams
There are many databases tracking internet radio streams. One of the larger and well known ones is: https://fmstream.org

After copying an audio stream URL, you can test it from the commandline in either of the following ways, depending on the player chosen:
```
[mpg123|mplayer] <audio stream URL>
```
The list of audio streams can be added to a regular text file, using one audio stream URL per line. Only valid http:// and https:// URLs will be read by the application during startup.

For local music, create a separate songlist text file with one absolute path to a readable audio file per line. Blank lines and lines starting with `#` are ignored. For example:
```
/home/guest/Music/song-one.mp3
/home/guest/Music/song two.mp3
```
Relative paths, missing files, and non-regular files are ignored with a warning.

### 5. Compiling the RadioPlayer
Default compilation is as follows:
```
g++ -Wall -Wextra -O2 radioplayer.cpp -o radioplayer
```

To use 'mplayer' as player use:

```
g++ -Wall -Wextra -O2 -DAUDIO_PLAYER=\"mplayer\" radioplayer.cpp -o radioplayer
```

Running the application with radio mode only (unchanged):
```
radioplayer <event input device> <path to audio streams file>
```

To enable music mode, pass the songlist as the optional third argument:
```
radioplayer <event input device> <path to audio streams file> <path to music songlist>
```
Map the spare remote button to the `M` key in Flirc to toggle modes. Linux reports this as keycode 50. The `M` button is ignored if no valid songs were loaded.

### 6. Run the RadioPlayer using 'screen' (in detached-mode)
For most practical usecases the player will need to be run in the background. This can be acchieved by running the application using 'screen' in detached mode (for more options see 'man screen'). For example:

```
screen -d -m /home/guest/radioplayer /dev/input/event0 /home/guest/radiostreams.txt /home/guest/songlist.txt
```

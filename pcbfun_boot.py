# FM Buddy 2.0: find the PCBfun board's USB serial port for flashing, without the BOOT button (tools/pcbfun_flash.bat).
#
#   1. The board is the PC's serial port already (it started with the PC on its USB-C port): that port. esptool's own
#      reset then takes it into download mode and, after the flash, back to the app.
#   2. It's in USB MIDI mode for the PC ("fm1buddy"): it's sent the SysEx F0 7D 66 6D 31 62 02 F7
#      (computer_s3.cpp TO_FLASH), it restarts into download mode on the serial port, and that port is waited for.
#   3. Neither (it started as the USB host, e.g. on its battery before the PC was plugged in): it asks for
#      Settings > Flash mode > Restart for flashing on the board, and waits.
#
# Prints the port (COM5) as its last line; exits 1 if there's none. Windows only for 2 (winmm, no extra package).
import ctypes
import sys
import time

sys.path.insert(0, r'.pydeps')  # (standalone flash kit: esptool's pyserial lives beside this file)
from serial.tools import list_ports  # (pyserial: comes with esptool)

ESPRESSIF = 0x303A  # the ESP32-S3's own USB Serial/JTAG (303A:1001): the app's serial port and the ROM's download mode
TO_FLASH = bytes([0xF0, 0x7D, 0x66, 0x6D, 0x31, 0x62, 0x02, 0xF7])
NAME = 'fm1buddy'


def say(text):
    print(text, file=sys.stderr, flush=True)


def board_port():
    for p in list_ports.comports():
        if p.vid == ESPRESSIF:
            return p.device
    return None


def wait_port(seconds):
    end = time.time() + seconds
    while time.time() < end:
        p = board_port()
        if p:
            time.sleep(1.0)  # (Windows: the port settles)
            return board_port()
        time.sleep(0.25)
    return None


class MIDIOUTCAPSW(ctypes.Structure):
    _fields_ = [('wMid', ctypes.c_ushort), ('wPid', ctypes.c_ushort), ('vDriverVersion', ctypes.c_uint),
                ('szPname', ctypes.c_wchar * 32), ('wTechnology', ctypes.c_ushort), ('wVoices', ctypes.c_ushort),
                ('wNotes', ctypes.c_ushort), ('wChannelMask', ctypes.c_ushort), ('dwSupport', ctypes.c_uint)]


class MIDIHDR(ctypes.Structure):
    pass


MIDIHDR._fields_ = [('lpData', ctypes.c_char_p), ('dwBufferLength', ctypes.c_uint),
                    ('dwBytesRecorded', ctypes.c_uint), ('dwUser', ctypes.c_void_p), ('dwFlags', ctypes.c_uint),
                    ('lpNext', ctypes.POINTER(MIDIHDR)), ('reserved', ctypes.c_void_p),
                    ('dwOffset', ctypes.c_uint), ('dwReserved', ctypes.c_void_p * 8)]


def midi_to_flash():
    """The board in USB MIDI mode: asked for download mode. True if the SysEx went out."""
    if sys.platform != 'win32':
        return False
    winmm = ctypes.windll.winmm
    for i in range(winmm.midiOutGetNumDevs()):
        caps = MIDIOUTCAPSW()
        if winmm.midiOutGetDevCapsW(i, ctypes.byref(caps), ctypes.sizeof(caps)):
            continue
        if NAME not in caps.szPname.lower():
            continue
        h = ctypes.c_void_p()
        if winmm.midiOutOpen(ctypes.byref(h), i, None, None, 0):
            say('Its MIDI port "%s" is in use (a DAW or another app has it): close that, then run this again.'
                % caps.szPname)
            return False
        buf = ctypes.create_string_buffer(TO_FLASH, len(TO_FLASH))
        hdr = MIDIHDR()
        hdr.lpData = ctypes.cast(buf, ctypes.c_char_p)
        hdr.dwBufferLength = len(TO_FLASH)
        winmm.midiOutPrepareHeader(h, ctypes.byref(hdr), ctypes.sizeof(hdr))
        winmm.midiOutLongMsg(h, ctypes.byref(hdr), ctypes.sizeof(hdr))
        time.sleep(0.3)
        winmm.midiOutUnprepareHeader(h, ctypes.byref(hdr), ctypes.sizeof(hdr))
        winmm.midiOutClose(h)
        say('The board is in USB MIDI mode ("%s"): asked it for download mode.' % caps.szPname)
        return True
    return False


def main():
    p = board_port()
    if p:
        say('The board is on %s.' % p)
        print(p)
        return 0
    if midi_to_flash():
        p = wait_port(15)
        if p:
            say('The board is on %s (download mode).' % p)
            print(p)
            return 0
    say('')
    say('The PC can\'t see the board (it started as the USB host, maybe on its battery).')
    say('On the board: Settings > Flash mode > Restart for flashing. Waiting for it (2 minutes) ...')
    p = wait_port(120)
    if p:
        say('The board is on %s.' % p)
        print(p)
        return 0
    say('Not found. Or: restart the board with the PC already plugged in (it then starts as the PC\'s serial')
    say('port), and run this again.')
    return 1


if __name__ == '__main__':
    sys.exit(main())

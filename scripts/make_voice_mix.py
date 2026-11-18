#!/usr/bin/env python3
"""Packs a Red Alert Remastered voice mod into SC mixfiles for this port.

Remastered voice mods (for example Mechanics VoiceOver's Russian dub) ship WAV
files named after the original sounds: RAC_SFX_EVA_<NAME>_<LANG>.wav for EVA,
RAC_SFX_UNT_<NAME>[.V00..R03]_<LANG>.wav and RAC_SFX_CMD_<NAME>_<LANG>.wav for
unit voices. This script converts the classic (RAC) set into the game's own
format, 16-bit IMA ADPCM .AUD files, and writes two mixfiles:

    SC--EVAR.MIX   EVA announcements (NAME.AUD)
    SC--UNTR.MIX   unit and hero voices (NAME.AUD or NAME.V00 ... NAME.R03)

The engine searches SC*.MIX files in a disc's data folder before the stock
mixfiles, so copying them into assets/redalert/allies/ replaces the voices.

    python3 scripts/make_voice_mix.py <mod>/Data/Audio/EN-US assets/redalert/allies

The voices are the mod authors' and EA's work: keep the output in the
git-ignored assets/ folder. Resampling uses macOS's afconvert.
"""

import os
import re
import struct
import subprocess
import sys
import tempfile
import wave

SPEECH_BUFFER_SIZE = 50000  # CODE/DEFINES.H: EVA files are read into buffers this size.
RATES = (22050, 16000, 11025, 8000)  # tried in order until an EVA line fits
FRAME_SAMPLES = 1024  # samples per AUD frame (2048 decoded bytes, 512 compressed)

IMA_STEPS = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767)
IMA_INDEX = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def clamp(value, low, high):
    return low if value < low else high if value > high else value


def ima_decode_step(state, nibble):
    """Mirrors decode_ima_nibble() in PORT/MAC/src/mac_audio_stub.cpp."""
    step = IMA_STEPS[state[1]]
    diff = step >> 3
    if nibble & 1:
        diff += step >> 2
    if nibble & 2:
        diff += step >> 1
    if nibble & 4:
        diff += step
    if nibble & 8:
        diff = -diff
    state[0] = clamp(state[0] + diff, -32768, 32767)
    state[1] = clamp(state[1] + IMA_INDEX[nibble], 0, 88)
    return state[0]


def ima_encode(samples):
    state = [0, 0]  # predictor, step index: the decoder starts from zero for each file
    nibbles = []
    for sample in samples:
        step = IMA_STEPS[state[1]]
        diff = sample - state[0]
        nibble = 0
        if diff < 0:
            nibble = 8
            diff = -diff
        if diff >= step:
            nibble |= 4
            diff -= step
        if diff >= step >> 1:
            nibble |= 2
            diff -= step >> 1
        if diff >= step >> 2:
            nibble |= 1
        ima_decode_step(state, nibble)  # track exactly what the decoder will reconstruct
        nibbles.append(nibble)
    return nibbles


def make_aud(samples, rate):
    nibbles = ima_encode(samples)
    frames = []
    for start in range(0, len(nibbles), FRAME_SAMPLES):
        chunk = nibbles[start:start + FRAME_SAMPLES]
        if len(chunk) % 2:
            chunk = chunk + [0]
        packed = bytes(chunk[i] | (chunk[i + 1] << 4) for i in range(0, len(chunk), 2))
        decoded = 2 * min(FRAME_SAMPLES, len(nibbles) - start)
        frames.append(struct.pack('<HHI', len(packed), decoded, 0x0000DEAF) + packed)
    body = b''.join(frames)
    header = struct.pack('<HiiBB', rate, len(body), 2 * len(samples), 2, 99)  # 16-bit, IMA
    return header + body


def read_pcm(path, rate):
    """Converts any WAV to 16-bit mono PCM at the given rate with afconvert."""
    with tempfile.TemporaryDirectory() as scratch:
        target = os.path.join(scratch, 'pcm.wav')
        subprocess.run(['afconvert', '-f', 'WAVE', '-d', 'LEI16@%d' % rate, '-c', '1', path, target],
                       check=True, capture_output=True)
        with wave.open(target, 'rb') as handle:
            frames = handle.readframes(handle.getnframes())
    return list(struct.unpack('<%dh' % (len(frames) // 2), frames))


def ra_crc(name):
    """The engine's mixfile name hash (CODE/CRC.CPP)."""
    data = name.upper().encode('ascii')
    crc = 0
    full = len(data) - len(data) % 4
    for index in range(0, full + (4 if len(data) % 4 else 0), 4):
        word = struct.unpack('<I', data[index:index + 4].ljust(4, b'\0'))[0]
        crc = ((((crc << 1) | (crc >> 31)) & 0xFFFFFFFF) + word) & 0xFFFFFFFF
    return struct.unpack('<i', struct.pack('<I', crc))[0]


def write_mix(path, files):
    entries = sorted(((ra_crc(name), data) for name, data in files.items()), key=lambda item: item[0])
    table, body = b'', b''
    for crc, data in entries:
        table += struct.pack('<iII', crc, len(body), len(data))
        body += data
    with open(path, 'wb') as handle:
        handle.write(struct.pack('<HHHI', 0, 0, len(entries), len(body)) + table + body)


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: make_voice_mix.py <wav folder> <output folder>')
    source, output = sys.argv[1], sys.argv[2]
    pattern = re.compile(r'^RAC_SFX_(EVA|UNT|CMD)_(.+?)_[A-Z]{2}-[A-Z]{2}\.wav$', re.IGNORECASE)
    speech, units, skipped = {}, {}, []
    for filename in sorted(os.listdir(source)):
        match = pattern.match(filename)
        if not match:
            if filename.lower().endswith('.wav') and not filename.upper().startswith('RAR_'):
                skipped.append(filename)
            continue
        kind, name = match.group(1).upper(), match.group(2).upper()
        if '.' not in name:
            name += '.AUD'
        path = os.path.join(source, filename)
        if kind == 'EVA':
            for rate in RATES:
                aud = make_aud(read_pcm(path, rate), rate)
                if len(aud) <= SPEECH_BUFFER_SIZE:
                    break
            else:
                sys.exit('%s does not fit the speech buffer even at %d Hz' % (filename, RATES[-1]))
            if rate != RATES[0]:
                print('  %s: %d Hz to fit the speech buffer' % (name, rate))
            speech[name] = aud
        else:
            units[name] = make_aud(read_pcm(path, RATES[0]), RATES[0])
    os.makedirs(output, exist_ok=True)
    if speech:
        write_mix(os.path.join(output, 'SC--EVAR.MIX'), speech)
    if units:
        write_mix(os.path.join(output, 'SC--UNTR.MIX'), units)
    print('EVA lines: %d, unit voices: %d' % (len(speech), len(units)))
    if skipped:
        print('skipped (unrecognised names): ' + ', '.join(skipped))


if __name__ == '__main__':
    main()

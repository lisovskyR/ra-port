#!/usr/bin/env python3
"""Moves a Red Alert Remastered cutscene dub into the original game's movies.

Remastered voice mods (for example Mechanics VoiceOver's Russian dub) ship the
cutscenes as Bink 2 video (<NAME>.BK2) with the dubbed soundtrack as an audio
track. The video can't be converted, but the audio can: this script finds each
cutscene's original VQA movie in the game's MAIN.MIX files by comparing their
soundtracks, measures the time offset between the two from the shared music
and effects, and writes the dubbed audio into the original movie in place of
its own. Only audio bytes change: the original movies store 22 kHz mono audio,
IMA ADPCM (SND2) or plain 16-bit PCM (SND0), in one chunk per frame, and the
new audio is cut into chunks of exactly the same sizes. The results go into one mixfile, SC--VIDR.MIX, which the
engine searches before the stock mixfiles.

    python3 scripts/make_movie_mix.py --track 1 <mod>/Data/ART/MOVIES/RA \\
        assets/redalert/allies assets/redalert/allies/MAIN.MIX assets/redalert/soviet/MAIN.MIX

--track picks which of the Bink file's audio tracks holds the dub (counting
from 0); --dry-run only reports the matches. Requires ffmpeg and a C compiler
(cc). The movies are EA's and the dub is its authors': keep the output in the
git-ignored assets/ folder.
"""

import argparse
import hashlib
import math
import mmap
import os
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_voice_mix as voice  # noqa: E402  (IMA encoder and mixfile writer)

RATE = 22050
# Remastered names that differ from the movie names the game plays (CODE/CONST.CPP VQName).
GAME_NAMES = {'ALLIEND': 'ALLYEND'}
COARSE_RATE = 2205
# A candidate movie's score adds up the correlation of every Bink track that agrees on the
# same offset, minus a penalty for each second its length differs from the cutscene's.
AGREE = 3  # coarse samples within which two tracks agree on an offset
LENGTH_PENALTY = 0.15  # per second of length difference beyond 0.1 s
ACCEPT = 0.9  # a score this high is accepted outright
MIN_SCORE = 0.6  # otherwise accepted only this high, and MARGIN ahead of the next candidate
MARGIN = 0.15

XCORR_SOURCE = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static short *load(const char *p, long *n) {
	FILE *f = fopen(p, "rb"); if (!f) exit(2);
	fseek(f, 0, SEEK_END); long s = ftell(f); fseek(f, 0, SEEK_SET);
	short *d = malloc(s ? s : 2); *n = s / 2; if (fread(d, 2, *n, f) != (size_t)*n) exit(3); fclose(f); return d;
}
static int cmp(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return a < b ? 1 : a > b ? -1 : 0; }
/* Best lag (b[i + lag] matches a[i]) by the 4th best window correlation. */
int main(int argc, char **argv) {
	long na, nb; short *a = load(argv[1], &na), *b = load(argv[2], &nb);
	long minlag = atol(argv[3]), maxlag = atol(argv[4]), win = atol(argv[5]), step = atol(argv[6]);
	double best = -1; long best_lag = 0;
	double *scores = malloc(sizeof(double) * (na / step + 2));
	for (long lag = minlag; lag <= maxlag; ++lag) {
		int k = 0;
		for (long s = 0; s + win <= na; s += step) {
			long t = s + lag; if (t < 0 || t + win > nb) continue;
			double xy = 0, xx = 0, yy = 0;
			for (long i = 0; i < win; ++i) { double x = a[s + i], y = b[t + i]; xy += x * y; xx += x * x; yy += y * y; }
			scores[k++] = (xx > 0 && yy > 0) ? xy / sqrt(xx * yy) : 0;
		}
		if (!k) continue;
		qsort(scores, k, sizeof(double), cmp);
		double score = scores[k < 4 ? 0 : 3];
		if (score > best) { best = score; best_lag = lag; }
	}
	printf("%ld %.4f\n", best_lag, best);
	return 0;
}
'''


def run_pcm(args, rate):
    cmd = ['ffmpeg', '-v', 'error', '-nostdin'] + args + ['-ac', '1', '-ar', str(rate), '-f', 's16le', '-']
    raw = subprocess.run(cmd, capture_output=True).stdout
    return raw[:len(raw) & ~1]


def as_samples(raw):
    return list(struct.unpack('<%dh' % (len(raw) // 2), raw))


class Movie:
    def __init__(self, data):
        self.data = bytearray(data)
        header = struct.unpack_from('<HHHHHBBBBHHHHHHBB', self.data, 20)
        self.frames, self.fps = header[2], header[7]
        self.rate, self.channels, self.bits = header[14], header[15], header[16]
        self.duration = self.frames / float(self.fps or 15)
        self.audio = []  # (chunk type, payload offset, size) of each audio chunk
        pos = 12
        while pos + 8 <= len(self.data):
            chunk, size = bytes(self.data[pos:pos + 4]), struct.unpack('>I', self.data[pos + 4:pos + 8])[0]
            if chunk in (b'SND0', b'SND2'):
                self.audio.append((chunk, pos + 8, size))
            pos += 8 + size + (size & 1)
        self.kinds = {kind for kind, _, _ in self.audio}
        self.samples = sum(self.chunk_samples(kind, size) for kind, _, size in self.audio)

    @staticmethod
    def chunk_samples(kind, size):
        return 2 * size if kind == b'SND2' else size // 2  # 4-bit IMA, or 16-bit PCM

    def usable(self):
        return (self.audio and len(self.kinds) == 1 and self.rate == RATE and self.channels == 1 and self.bits == 16)

    def replace_audio(self, samples):
        if self.kinds == {b'SND2'}:
            # The player decodes IMA continuously across a movie's chunks, so encode it as one stream.
            nibbles = voice.ima_encode(samples)
            index = 0
            for _, offset, size in self.audio:
                chunk = nibbles[index:index + 2 * size]
                index += 2 * size
                self.data[offset:offset + size] = bytes(chunk[i] | (chunk[i + 1] << 4) for i in range(0, len(chunk), 2))
        else:
            index = 0
            for _, offset, size in self.audio:
                count = size // 2
                self.data[offset:offset + 2 * count] = struct.pack('<%dh' % count, *samples[index:index + count])
                index += count


def find_movies(paths):
    movies, seen = [], set()
    for path in paths:
        with open(path, 'rb') as handle:
            data = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
            pos = 0
            while True:
                found = data.find(b'WVQAVQHD', pos)
                if found < 0:
                    break
                start = found - 8
                size = struct.unpack('>I', data[start + 4:start + 8])[0] + 8
                pos = found + 8
                digest = hashlib.sha1(data[start:start + size]).hexdigest()
                if digest in seen:
                    continue
                seen.add(digest)
                movie = Movie(data[start:start + size])
                if movie.usable():
                    movies.append(movie)
    return movies


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--track', type=int, required=True, help='audio track of the Bink files that holds the dub (from 0)')
    parser.add_argument('--dry-run', action='store_true', help='only report which movies match')
    parser.add_argument('--max-offset', type=float, default=1.5, help='largest time offset to search, in seconds (default 1.5)')
    parser.add_argument('--only', default='', help='comma-separated cutscene names to limit the run to')
    parser.add_argument('bink_folder')
    parser.add_argument('output_folder')
    parser.add_argument('main_mix', nargs='+')
    args = parser.parse_args()

    scratch = tempfile.mkdtemp(prefix='movie_mix_')
    xcorr = os.path.join(scratch, 'xcorr')
    with open(xcorr + '.c', 'w') as handle:
        handle.write(XCORR_SOURCE)
    subprocess.run(['cc', '-O2', '-o', xcorr, xcorr + '.c', '-lm'], check=True)

    def best_lag(a_raw, b_raw, minlag, maxlag, window, step):
        a_path, b_path = os.path.join(scratch, 'a.s16'), os.path.join(scratch, 'b.s16')
        open(a_path, 'wb').write(a_raw)
        open(b_path, 'wb').write(b_raw)
        out = subprocess.run([xcorr, a_path, b_path, str(minlag), str(maxlag), str(window), str(step)],
                             capture_output=True, text=True, check=True).stdout.split()
        return int(out[0]), float(out[1])

    movies = find_movies(args.main_mix)
    print('original movies with 22 kHz mono audio: %d' % len(movies))
    coarse_cache = {}

    def original_audio(movie, rate):
        key = (id(movie), rate)
        if key not in coarse_cache:
            path = os.path.join(scratch, 'movie.vqa')
            open(path, 'wb').write(movie.data)
            coarse_cache[key] = run_pcm(['-i', path, '-vn'], rate)
        return coarse_cache[key]

    candidates = {}  # cutscene name -> scored candidates, best first
    for filename in sorted(os.listdir(args.bink_folder)):
        if not filename.upper().endswith('.BK2'):
            continue
        name = filename[:-4].upper()
        if args.only and name not in [only.strip().upper() for only in args.only.split(',')]:
            continue
        bink = os.path.join(args.bink_folder, filename)
        duration = float(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', bink],
                                        capture_output=True, text=True).stdout or 0)
        # All tracks of a Bink file share one timeline, and the music and effects that
        # match the original can sit on any of them, so every track is compared.
        tracks = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a', '-show_entries', 'stream=index',
                                 '-of', 'csv=p=0', bink], capture_output=True, text=True).stdout.split()
        aligns = [run_pcm(['-i', bink, '-map', '0:a:%d' % track], COARSE_RATE) for track in range(len(tracks))]
        span = int(args.max_offset * COARSE_RATE)
        scored = []
        for movie in movies:
            if abs(movie.duration - duration) > 4:
                continue
            per_track = [best_lag(align, original_audio(movie, COARSE_RATE), -span, span, COARSE_RATE, COARSE_RATE) for align in aligns]
            lead = max(range(len(per_track)), key=lambda track: per_track[track][1])
            lag = per_track[lead][0]
            agreed = sum(max(0.0, score) for other, score in per_track if abs(other - lag) <= AGREE)
            score = agreed - LENGTH_PENALTY * max(0.0, abs(movie.duration - duration) - 0.1)
            scored.append((score, movie, lag, lead, duration))
        scored.sort(key=lambda item: -item[0])
        candidates[name] = scored

    # Assign the most certain matches first, each original movie to one cutscene only.
    results, taken = {}, set()
    order = sorted(candidates, key=lambda name: -(candidates[name][0][0] if candidates[name] else 0))
    for name in order:
        options = [option for option in candidates[name] if id(option[1]) not in taken]
        if not options:
            print('  %-9s no original movie matched' % name)
            continue
        score, movie, coarse, track, duration = options[0]
        runner_up = options[1][0] if len(options) > 1 else 0.0
        if not (score >= ACCEPT or (score >= MIN_SCORE and score - runner_up >= MARGIN)):
            print('  %-9s no confident match (best %.2f, next %.2f): left in the original language' % (name, score, runner_up))
            continue
        taken.add(id(movie))
        bink = os.path.join(args.bink_folder, name + '.BK2')
        fine_align = run_pcm(['-i', bink, '-map', '0:a:%d' % track], RATE)
        lag, _ = best_lag(fine_align, original_audio(movie, RATE), coarse * 10 - 20, coarse * 10 + 20, RATE // 5, RATE)
        print('  %-9s %6.2f s movie, %6.2f s dub, offset %+.3f s, score %.2f via track %d (next %.2f)'
              % (name, movie.duration, duration, lag / float(RATE), score, track, runner_up))
        results[name] = (movie, lag)

    if args.dry_run or not results:
        return

    files = {}
    for name, (movie, lag) in sorted(results.items()):
        bink = os.path.join(args.bink_folder, name + '.BK2')
        dub = as_samples(run_pcm(['-i', bink, '-map', '0:a:%d' % args.track], RATE))
        original = as_samples(original_audio(movie, RATE))[:movie.samples]
        original += [0] * (movie.samples - len(original))
        # Match the original's loudness so cutscenes sit at the same volume as before, but
        # never boost past the dub's peaks: clipping would distort it.
        rms = lambda values: math.sqrt(sum(v * v for v in values[::5]) / max(1, len(values[::5]))) or 1.0
        peak = max(1, max(abs(v) for v in dub)) if dub else 1
        gain = max(0.5, min(2.0, rms(original) / rms(dub), 32767.0 / peak))
        audio = []
        for index in range(movie.samples):
            source = index - lag
            if 0 <= source < len(dub):
                audio.append(max(-32768, min(32767, int(dub[source] * gain))))
            else:
                audio.append(original[index])  # lead-in and tail the dub doesn't cover
        movie.replace_audio(audio)
        files[GAME_NAMES.get(name, name) + '.VQA'] = bytes(movie.data)
        print('  %-9s dubbed (gain %.2f)' % (name, gain))
    os.makedirs(args.output_folder, exist_ok=True)
    voice.write_mix(os.path.join(args.output_folder, 'SC--VIDR.MIX'), files)
    print('wrote SC--VIDR.MIX with %d movies' % len(files))


if __name__ == '__main__':
    main()

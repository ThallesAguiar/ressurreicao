"""Pack ST0 assets and sparse, byte-exact CD audio without shipping the disc."""
import argparse
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
SECTOR = 2352
FILES = [
    'DRA.BIN', 'BIN/F_END.BIN', 'BIN/F_GAME2.BIN', 'BIN/F_PROLO0.BIN',
    'BIN/F_PROLO1.BIN', 'BIN/F_TITLE0.BIN', 'BIN/F_TITLE1.BIN', 'BIN/RIC.BIN',
    'ST/ST0/F_ST0.BIN', 'ST/ST0/SD_ZKST0.VB', 'ST/ST0/SD_ZKST0.VH',
    'VAB/SD_MAR.VB', 'VAB/SD_MAR.VH', 'VAB/SD_RIH.VB', 'VAB/SD_RIH.VH',
]


def pack(output):
    for name in FILES:
        source = ROOT / 'disks/us' / name
        destination = output / 'disks/us' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)

    # Preserve the original sector addresses and interleaving. The browser
    # mounts a sparse read-only track, backed only by the retained ranges.
    cue = (ROOT / 'disks/sotn.us.cue').read_text()
    track_name = re.search(r'FILE "([^"]+)"', cue).group(1)
    track = ROOT / 'disks' / track_name
    lba = int(re.search(r'OFF_SD_XA_STR1\s+(0x\w+)',
                       (ROOT / 'include/lba_us.h').read_text()).group(1), 16)
    configs = []
    for row in re.findall(r'\{([^{}]+)\}',
                          (ROOT / 'src/dra/gen/us/xa_music_config.h').read_text()):
        configs.append([int(x.strip(), 0) for x in row.split(',')])

    # ST0 music, battle voices and the complete introductory dialogue.
    ids = set(range(0x358, 0x360))
    source = '\n'.join(p.read_text(encoding='utf-8') for p in
                       (ROOT / 'src/st/st0').glob('*.c'))
    source += (ROOT / 'src/dra/game_handlers.c').read_text(encoding='utf-8')
    for number, name in re.findall(r'/\* us:(0x[0-9A-Fa-f]+)[^*]*\*/\s*(\w+)',
                                    (ROOT / 'include/sfx.h').read_text()):
        if name.startswith('MU_') and re.search(r'\b' + name + r'\b', source):
            ids.add(int(number, 16))
    # The stage BGM is selected from data rather than a direct PlaySfx call.
    ids.update((0x31F, 0x320, 0x321, 0x322, 0x336, 0x337))
    ids.update(int(x, 16) for x in re.findall(r'PLAY_SOUND,\s*(0x\w+)',
                   (ROOT / 'assets/st/st0/cutscene_script_psx.yaml').read_text()))
    indices = {i - 0x300 for i in ids if 0x300 < i < 0x533}
    indices |= {i + 1 for i in indices if configs[i][6] == 1}
    spans = []
    streams = []
    for i in sorted(indices):
        address, frames, file_id, channel, *_ = configs[i]
        start = lba + address + channel
        # XA runs at 150 sectors/sec; durations are measured in 60Hz frames.
        end = min(track.stat().st_size // SECTOR,
                  start + (frames * 5 + 1) // 2 + 32)
        spans.append((max(0, start - 16), end))
        streams.append((max(0, start - 16), end, file_id, channel % 16))
    merged = []
    for start, end in sorted(spans):
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    ranges = []
    with track.open('rb') as original, (output / 'cd-audio.bin').open('wb') as packed:
        for start, end in merged:
            original.seek(start * SECTOR)
            for sector in range(start, end):
                data = original.read(SECTOR)
                if len(data) != SECTOR:
                    raise RuntimeError('Incomplete source CD track')
                keep_audio = any(a <= sector < b and data[16] == f
                                 and data[17] == c for a, b, f, c in streams)
                # Keep every subheader so the native decoder still observes
                # stream boundaries; omit only unrelated channel payloads.
                length = SECTOR if keep_audio else 24
                offset = packed.tell()
                packed.write(data[:length])
                first, last = sector * SECTOR, sector * SECTOR + length
                if ranges and ranges[-1][1] == first:
                    ranges[-1][1] = last
                else:
                    ranges.append([first, last, offset])
    (output / 'cd-map.json').write_text(json.dumps({
        'size': track.stat().st_size, 'ranges': ranges,
    }), encoding='utf-8')
    (output / 'disc.cue').write_text(
        'FILE "track.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n',
        encoding='ascii')
    print(f'Web audio: {(output / "cd-audio.bin").stat().st_size / 1048576:.1f} MiB')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    pack(parser.parse_args().output)

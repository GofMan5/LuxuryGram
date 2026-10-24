#!/usr/bin/env python3
"""Generate nonsecret, v2-only Packer/alpha compatibility headers for canary CI."""
from pathlib import Path
import argparse


def write_headers(source: Path, private: Path) -> None:
    source.mkdir(parents=True, exist_ok=True)
    private.mkdir(parents=True, exist_ok=True)
    (source / 'packer_private.h').write_text(
        '#define PACKER_V2_ONLY\n'
        'const char *PrivateKey = "";\n'
        'const char *PrivateBetaKey = "";\n', encoding='utf-8')
    (private / 'alpha_private.h').write_text(
        'static const char *AlphaPrivateKey = "";\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('private', type=Path)
    args = parser.parse_args()
    write_headers(args.source, args.private)

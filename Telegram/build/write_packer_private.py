'''
Writes Telegram/SourceFiles/_other/packer_private.h from the UPDATE_KEY
environment variable, so Packer can sign LuxuryGram update packages without the
private key ever being written into the repository.

Usage: write_packer_private.py <output header path>
'''
import os
import sys


def literal(name, pem):
    lines = pem.strip().splitlines()
    out = f'const char *{name} = "\\\n'
    for line in lines[:-1]:
        out += line + '\\n\\\n'
    return out + lines[-1] + '\\\n";\n'


def main():
    if len(sys.argv) != 2:
        print('Usage: write_packer_private.py <output header path>')
        return 1

    pem = os.environ.get('UPDATE_KEY', '')
    if 'PRIVATE KEY' not in pem:
        print('UPDATE_KEY does not contain a PEM private key.')
        return 1

    header = (
        '// Generated at build time from the UPDATE_KEY secret. Never commit this file.\n'
        '#pragma once\n\n'
        + literal('PrivateKey', pem)
        + '\n'
        # The beta channel is unused, but packer.cpp references the symbol.
        + literal('PrivateBetaKey', pem)
    )
    with open(sys.argv[1], 'w', encoding='utf-8', newline='\n') as f:
        f.write(header)

    print(f'Wrote {sys.argv[1]} ({len(header)} bytes).')
    return 0


if __name__ == '__main__':
    sys.exit(main())

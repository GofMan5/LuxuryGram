'''
Writes the update feed the LuxuryGram updater polls.

The client requests <prefix>/current<AutoUpdateVersion> and looks up its own
platform key, so one file serves every platform we build. Links keep the
{version} placeholder: the client substitutes the advertised version itself.

Usage: write_update_feed.py <released version> <output path>
'''
import json
import sys

PLATFORMS = {
    'win64': 'tx64upd{version}',
    'linux': 'tlinuxupd{version}',
}


def build(released):
    return {
        platform: {'stable': {'released': released, 'link': link}}
        for platform, link in PLATFORMS.items()
    }


def main():
    if len(sys.argv) != 3:
        print('Usage: write_update_feed.py <released version> <output path>')
        return 1

    released = int(sys.argv[1])
    if released <= 1016 or released > 999999999:
        print(f'Refusing to advertise version {released}: Packer rejects it.')
        return 1

    with open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as f:
        json.dump(build(released), f, indent=1)
        f.write('\n')

    print(f'Wrote {sys.argv[2]} advertising {released}.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

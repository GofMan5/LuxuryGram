'''
Writes the update feed the LuxuryGram updater polls.

The client requests <prefix>/current<AutoUpdateVersion> and looks up its own
platform key, so one file serves every platform we build. Links keep the
{version} placeholder: the client substitutes the advertised version itself.

Usage: write_update_feed.py <released version> <output path>
'''
import json
import sys

PACKAGES = {
    'win64': 'tx64upd{version}',
    'linux': 'tlinuxupd{version}',
}


def build(released):
    # The link is joined onto the prefix with nothing in between --
    # Local::readAutoupdatePrefix() strips the trailing slash off the stored
    # prefix, and HttpChecker::parseResponse() does a bare concatenation -- so
    # the leading slash here is the only separator in the resulting URL. Without
    # it 1.0.2 fetched .../releases/download/updatestx64upd1000002, got GitHub's
    # 404 page, and wrote it to disk as the update.
    return {
        platform: {'stable': {'released': released, 'link': '/' + package}}
        for platform, package in PACKAGES.items()
    }


def main():
    if len(sys.argv) != 3:
        print('Usage: write_update_feed.py <released version> <output path>')
        return 1

    released = int(sys.argv[1])
    if released <= 1016 or released > 999999999:
        print(f'Refusing to advertise version {released}: Packer rejects it.')
        return 1

    feed = build(released)
    for platform, entry in feed.items():
        link = entry['stable']['link']
        assert link.startswith('/') and '//' not in link, (platform, link)

    with open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as f:
        json.dump(feed, f, indent=1)
        f.write('\n')

    print(f'Wrote {sys.argv[2]} advertising {released}.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

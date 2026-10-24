#!/usr/bin/env python3
"""Fail closed before release mutations; published product versions are immutable."""
import argparse
import json
import os
import urllib.error
import urllib.parse
import urllib.request


class ReleaseCheckError(RuntimeError):
    pass


def get_release(repository, tag, token, opener=urllib.request.urlopen):
    url = f'https://api.github.com/repos/{repository}/releases/tags/{urllib.parse.quote(tag, safe="")}'
    request = urllib.request.Request(url, headers={
        'Accept': 'application/vnd.github+json',
        'Authorization': f'Bearer {token}',
        'X-GitHub-Api-Version': '2022-11-28',
        'User-Agent': 'LuxuryGram-release-preflight',
    })
    try:
        with opener(request, timeout=30) as response:
            value = json.load(response)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise ReleaseCheckError(f'Release API failed with HTTP {error.code}; no mutations allowed.') from error
    except (OSError, ValueError) as error:
        raise ReleaseCheckError('Release API unavailable or invalid; no mutations allowed.') from error
    if not isinstance(value, dict) or not isinstance(value.get('draft'), bool):
        raise ReleaseCheckError('Release API omitted the draft state; no mutations allowed.')
    return value


def check_release(repository, tag, token, names=(), opener=urllib.request.urlopen):
    product = get_release(repository, tag, token, opener)
    if product is not None and not product['draft']:
        raise ReleaseCheckError(f'{tag} is published: build a new product version instead of rewriting it.')
    updates = get_release(repository, 'updates', token, opener)
    if updates is not None:
        assets = updates.get('assets')
        if not isinstance(assets, list) or any(not isinstance(a, dict) or not isinstance(a.get('name'), str) for a in assets):
            raise ReleaseCheckError('Update-container asset list is invalid; no mutations allowed.')
        collisions = set(names).intersection(a['name'] for a in assets)
        if collisions:
            raise ReleaseCheckError('Immutable update packages already exist: ' + ', '.join(sorted(collisions)))
    return {'product': 'draft' if product else 'absent', 'updates': 'present' if updates else 'absent'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('--package', action='append', default=[])
    args = parser.parse_args()
    try:
        result = check_release(os.environ['GITHUB_REPOSITORY'], args.tag, os.environ['GH_TOKEN'], args.package)
    except (KeyError, ReleaseCheckError) as error:
        parser.exit(1, f'{error}\n')
    output = os.environ.get('GITHUB_OUTPUT')
    if output:
        with open(output, 'a', encoding='utf-8') as file:
            for key, value in result.items():
                file.write(f'{key}={value}\n')
    print(f'Release mutation preflight passed: {result}')

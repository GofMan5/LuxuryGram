"""Reject known Qt inputs in generated MSBuild application link settings.

This is only an early negative check. A clean project file does not certify a
Qt-free executable: inherited settings, transitive archives and runtime loads
require separate evidence. Run on the production project, not the Rust demo.
"""

import argparse
from pathlib import PureWindowsPath
import re
import sys
import xml.etree.ElementTree as ET


MSBUILD = '{http://schemas.microsoft.com/developer/msbuild/2003}'
QT_LIBRARY = re.compile(r'(?:lib)?qt[56][a-z0-9_]+\.(?:lib|a|dll)', re.I)


def qt_inputs(project):
    links = project.findall(f'{MSBUILD}ItemDefinitionGroup/{MSBUILD}Link')
    if not links:
        raise ValueError('missing application Link settings')
    matches = set()
    for link in links:
        dependencies = link.findtext(f'{MSBUILD}AdditionalDependencies', '')
        if not dependencies.strip():
            raise ValueError('missing explicit AdditionalDependencies')
        if not any(value.strip() and not value.lstrip().startswith('%(')
                   for value in dependencies.split(';')):
            raise ValueError('no inspectable direct linker inputs')
        delay_loads = link.findtext(f'{MSBUILD}DelayLoadDLLs', '')
        for value in (dependencies + ';' + delay_loads).split(';'):
            path = PureWindowsPath(value.strip().strip('"'))
            if QT_LIBRARY.fullmatch(path.name) or any(
                part.lower().startswith('external_qt_static_plugins')
                for part in path.parts
            ):
                matches.add(str(path))
    return sorted(matches)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('project', help='CMake-generated production .vcxproj')
    args = parser.parse_args()
    try:
        matches = qt_inputs(ET.parse(args.project).getroot())
    except (OSError, ET.ParseError, ValueError) as error:
        print(f'Unknown Qt status: {error}', file=sys.stderr)
        return 2
    if matches:
        for match in matches:
            print(f'Qt link input: {match}')
        return 1
    print('No recognized direct Qt link inputs; artifact and runtime unverified.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

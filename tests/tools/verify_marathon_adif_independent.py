#!/usr/bin/env python3
"""Verify the opt-in synthetic Marathon export with independent adif_io 0.6.0."""
import argparse
import importlib.metadata
from pathlib import Path

import adif_io


def check(path):
    text = Path(path).read_text(encoding='utf-8')
    rows, _ = adif_io.read_from_string(text)
    if len(rows) != 3 or [r.get('CALL') for r in rows] != ['K1AAA', 'K1BBB', 'K1CCC']:
        raise ValueError('expected the three synthetic fixture calls only')
    required = ('QSO_DATE', 'TIME_ON', 'CALL', 'BAND', 'MODE')
    if any(not all(row.get(key) for key in required) for row in rows):
        raise ValueError('a minimum submission field is missing')
    if (rows[0].get('QSO_DATE'), rows[0].get('TIME_ON')) != ('20261231', '233000'):
        raise ValueError('UTC year-boundary fixture changed')
    if (rows[2].get('DXCC'), rows[2].get('CQZ')) != ('0', '3'):
        raise ValueError('explicit no-DXCC and zone data changed')
    marker = '<call:5>K1AAA'
    if marker not in text:
        raise ValueError('fixture marker missing')
    try:
        corrupt, _ = adif_io.read_from_string(text.replace(marker, '<call:4>K1AAA', 1))
        if corrupt and corrupt[0].get('CALL') == 'K1AAA':
            raise ValueError('negative control did not change the decoded call')
    except adif_io.AdifError:
        pass
    print('PASS adif_io %s: three synthetic QSO, minimum fields, UTC, explicit DXCC=0; negative-control length changed decoded call' %
          importlib.metadata.version('adif-io'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    args = parser.parse_args()
    check(args.fixture)

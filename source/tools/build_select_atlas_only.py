"""Rebuilds only the select-portrait atlas (cwo_select_outfits.dxt) and its .txt from the outfit renders, keeping the
#image lines of an installed .txt (the menu images' extra cell is unchanged). v5.86 (owner 2026-11-03): + the 11
selectable monsters as atlas rows 11-21; Nanashi colour 3 back to the game's own model.
Usage: python -B build_select_atlas_only.py OLD_TXT FACES_DIR OUT_DIR
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_select_outfits as bso  # noqa: E402


def main(old_txt, faces, out):
    os.makedirs(out, exist_ok=True)
    W, H, enc, missing, _ = bso.atlas(faces)
    open(os.path.join(out, 'cwo_select_outfits.dxt'), 'wb').write(enc)
    images = [l.rstrip('\n') for l in open(old_txt, encoding='utf-8') if l.startswith('#image\t')]
    assert images, 'no #image lines in ' + old_txt
    lines = ['# select portraits per outfit (build_select_outfits.py / build_select_atlas_only.py): atlas w h cell_w cell_h columns; image menu-slot textures sprites texture sprite hash0 hash1 first-player-sprite',
             '#atlas\t%d\t%d\t%d\t%d\t%d' % (W, H, bso.CW, bso.CH, bso.OUTFITS), '#stems\t' + '\t'.join(bso.STEMS)] + images
    open(os.path.join(out, 'cwo_select_outfits.txt'), 'w', newline='\n').write('\n'.join(lines) + '\n')
    print('atlas %d x %d, %d rows, dxt %s; missing: %s' % (W, H, len(bso.STEMS), hashlib.sha256(enc).hexdigest()[:8], ', '.join(missing) or 'none'))


if __name__ == '__main__':
    main(*sys.argv[1:4])

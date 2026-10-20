"""web/ の index.html・app.js・engine.wasm を 1 つの HTML にまとめる。

  python3 tools/bundle.py web dist/index.html             そのまま開ける完全な HTML
  python3 tools/bundle.py web out.html --fragment         <html>/<head>/<body> を除いた本文だけ
"""
import base64
import pathlib
import sys

SKELETON = ('<!doctype html>', '<html lang="ja">', '<head>', '</head>', '<body>', '</body>', '</html>',
            '<meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    fragment = '--fragment' in sys.argv
    src, out = pathlib.Path(args[0]), pathlib.Path(args[1])
    html = (src / 'index.html').read_text(encoding='utf-8')
    js = (src / 'app.js').read_text(encoding='utf-8')
    wasm = base64.b64encode((src / 'engine.wasm').read_bytes()).decode('ascii')
    tag = '<script src="app.js"></script>'
    assert tag in html
    html = html.replace(tag, f'<script>window.MCBE_WASM_B64 = "{wasm}";</script>\n<script>\n{js}</script>')
    if fragment:
        html = '\n'.join(line for line in html.splitlines() if line.strip() not in SKELETON) + '\n'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(html, encoding='utf-8')
    print(f'wrote {out} ({out.stat().st_size // 1024} KB)')


if __name__ == '__main__':
    main()

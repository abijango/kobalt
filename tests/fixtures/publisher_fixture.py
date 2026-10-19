"""Synthetic books with publisher markup that the reader must handle.

The English book has code, characters outside the reading fonts, search text
with compatibility characters, an unmarked multi-paragraph note, a footnote
aside, a non-linear spine item and an SVG spine item. Before DOM version
20240114, CREngine gave the SVG item no DocFragment, so a position in the
chapter after it has a different DocFragment index in older versions.
"""
import zipfile

# A position in `after.xhtml` written by DOM version 20210904, which skipped
# the SVG spine item. The current version reads it as a position in the SVG.
OLD_DOM_VERSION = 20210904
OLD_POSITION = '/body/DocFragment[3]/body/p[2]/text().0'
# Words that occur once each, for locating text in checks.
TARGET_WORD = 'Domtarget'
ASIDE_WORD = 'Asidetext'
NONLINEAR_WORD = 'Nonlinearspine'
LAST_WORD = 'Closingword'

GERMAN = ('Die Donaudampfschifffahrtsgesellschaftskapitäne besprachen Rechtsschutzversicherungsgesellschaften '
          'und Straßenbahnhaltestellen, während die Arbeitsunfähigkeitsbescheinigungen auf dem Tisch lagen. ')


def _xhtml(body, language=None):
    lang = f' xml:lang="{language}" lang="{language}"' if language else ''
    return ('<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"' + lang
            + '><head><title>Publisher markup</title></head><body>' + body + '</body></html>')


def _write(path, language, title, items, spine):
    manifest = ''.join(f'<item id="{name}" href="{name}" media-type="{media}"/>' for name, media, _ in items)
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
                         '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                         f'<dc:identifier id="id">kobalt-publisher-{language}</dc:identifier><dc:title>{title}</dc:title>'
                         f'<dc:language>{language}</dc:language></metadata><manifest>{manifest}</manifest><spine>{spine}</spine></package>')
        for name, _, content in items:
            archive.writestr(name, content)


def book(path, svg_spine=True):
    """English book with every case. See the module docstring.

    Kobalt sends a book with a non-XHTML spine item to Nickel before layout,
    so reader scenarios pass `svg_spine=False`. The DOM version case needs it.
    """
    text = '<p>Linear text continues after the aside with enough words to fill part of a page.</p>' * 12
    start = (
        '<p id="lead">Read the first note<a epub:type="noteref" href="notes.xhtml#n1">1</a> and the second<a epub:type="noteref" href="notes.xhtml#n2">2</a>.</p>'
        '<pre id="code">iiiiiiiiii\nMMMMMMMMMM</pre>'
        '<p id="scripts">Greek αβγΩ, Cyrillic Жж, phonetic ɛʃ and mathematics ∑∫→.</p>'
        '<p id="search">The ﬁnal e‑mail about hyphen​ation.</p>'
        f'<p lang="fr">Une phrase française dans un livre anglais.</p>'
        f'<aside epub:type="footnote" id="aside"><p>{ASIDE_WORD} belongs to a footnote.</p></aside>' + text)
    items = [
        ('start.xhtml', 'application/xhtml+xml', _xhtml(start)),
        ('extra.xhtml', 'application/xhtml+xml', _xhtml(f'<p>{NONLINEAR_WORD} content outside the reading order.</p>')),
        *([('plate.svg', 'image/svg+xml', '<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><rect width="100" height="100" fill="#888"/></svg>')]
          if svg_spine else []),
        ('after.xhtml', 'application/xhtml+xml', _xhtml(f'<p>First paragraph of the second chapter.</p><p>{TARGET_WORD} marks the saved position.</p>' + text)),
        ('notes.xhtml', 'application/xhtml+xml', _xhtml(
            '<h2>Notes</h2><p id="n1">Note one opens here.</p><p>Note one continues in a second paragraph.</p>'
            f'<p id="n2">Note two stands alone.</p><h2>Afterword</h2><p>{LAST_WORD} ends the book.</p>')),
    ]
    spine = ('<itemref idref="start.xhtml"/><itemref idref="extra.xhtml" linear="no"/>'
             + ('<itemref idref="plate.svg"/>' if svg_spine else '') + '<itemref idref="after.xhtml"/><itemref idref="notes.xhtml"/>')
    _write(path, 'en', 'Publisher markup', items, spine)


def cjk_book(path, language, inline_language=None):
    """The same Han characters expose regional glyph differences through locl."""
    tag = f' lang="{inline_language}"' if inline_language else ''
    body = f'<p style="text-indent:0"{tag}>骨直令曜</p>'
    _write(path, language, 'CJK fallback', [('text.xhtml', 'application/xhtml+xml', _xhtml(body))],
           '<itemref idref="text.xhtml"/>')


def german_book(path):
    """German text with long compound words and no lang attributes."""
    _write(path, 'de', 'Silbentrennung', [('text.xhtml', 'application/xhtml+xml', _xhtml(('<p>' + GERMAN * 4 + '</p>') * 6))],
           '<itemref idref="text.xhtml"/>')


def russian_book(path):
    """Russian text, a language without a bundled dictionary."""
    _write(path, 'ru', 'Перенос', [('text.xhtml', 'application/xhtml+xml', _xhtml('<p>' + 'Достопримечательности государственного университета. ' * 40 + '</p>'))],
           '<itemref idref="text.xhtml"/>')


def hyphenation_books(directory):
    """Package language selects each added dictionary, including regional tags."""
    words = {
        'et-EE': 'rahvusvaheline', 'fi-FI': 'kirjallisuus', 'ka-GE': 'საქართველო',
        'hu-HU': 'Magyarország', 'lt-LT': 'universitetas', 'lv-LV': 'universitāte',
        'pl-PL': 'przedstawienie', 'sk-SK': 'medzinárodného', 'sl-SI': 'slovenščina',
        'uk-UA': 'література',
    }
    for language, word in words.items():
        _write(directory / f'hyphenation-{language}.epub', language, 'Hyphenation',
               [('text.xhtml', 'application/xhtml+xml', _xhtml('<p>' + (word + ' ') * 80 + '</p>'))],
               '<itemref idref="text.xhtml"/>')


def equations_book(path):
    """Font-relative SVG equations, separate from ordinary coloured artwork."""
    svg = '<svg xmlns="http://www.w3.org/2000/svg" width="400" height="200" viewBox="0 0 400 200"><rect width="400" height="200" fill="#d02030"/></svg>'
    math = '<svg xmlns="http://www.w3.org/2000/svg" width="400" height="200" viewBox="0 0 400 200"><rect width="100" height="200"/><rect x="100" width="100" height="200" fill="#333333"/><rect x="200" width="100" height="200" fill="#00cc66"/></svg>'
    body = ('<p>Inline <img src="size.svg" style="width:4ex;height:2ex"/> equation.</p>'
            '<p><img data-tex="x" src="math.svg" style="width:8ex;height:4ex"/></p>')
    _write(path, 'en', 'Equation rendering', [('text.xhtml', 'application/xhtml+xml', _xhtml(body)),
           ('size.svg', 'image/svg+xml', svg), ('math.svg', 'image/svg+xml', math)], '<itemref idref="text.xhtml"/>')


def embedded_fonts_book(path, font):
    """A real embedded family, two styles, a missing file and a local alias."""
    css = ('@font-face{font-family:Embedded;src:url(font.ttf)}'
           '@font-face{font-family:Embedded;font-style:italic;src:url(font.ttf)}'
           '@font-face{font-family:Missing;src:url(missing.ttf)}'
           '@font-face{font-family:Alias;src:local("Libron")}'
           'p.embedded{font-family:Embedded}')
    html = _xhtml('<p class="embedded">Embedded family text.</p><p>Fallback family text.</p>')
    html = html.replace('</head>', '<link rel="stylesheet" href="fonts.css"/></head>')
    _write(path, 'en', 'Embedded fonts', [('text.xhtml', 'application/xhtml+xml', html),
           ('fonts.css', 'text/css', css), ('font.ttf', 'font/ttf', font.read_bytes())], '<itemref idref="text.xhtml"/>')


def font_families_book(path, font=None):
    """Each CSS family starts a page so checks can inspect its rendered face."""
    css = ('p,pre{page-break-before:always;text-indent:0}'
           '.serif{font-family:serif}.sans{font-family:sans-serif}'
           '.mono{font-family:monospace}.named{font-family:"IBM Plex Sans",serif}'
           '.embedded{font-family:Embedded,serif}')
    body = ('<p class="serif">Serifmarker family text.</p>'
            '<p class="sans">Sansmarker family text.</p>'
            '<pre class="mono">Monomarker family text.</pre>'
            '<p class="named">Namedmarker family text.</p>')
    html = _xhtml(body).replace('</head>', '<link rel="stylesheet" href="fonts.css"/></head>')
    items = [('text.xhtml', 'application/xhtml+xml', html), ('fonts.css', 'text/css', css)]
    spine = '<itemref idref="text.xhtml"/>'
    if font:
        embedded = _xhtml('<p class="embedded">Embeddedmarker family text.</p>')
        embedded = embedded.replace('</head>', '<link rel="stylesheet" href="embedded.css"/></head>')
        items.extend([('embedded.xhtml', 'application/xhtml+xml', embedded),
                      ('embedded.css', 'text/css', '@font-face{font-family:Embedded;src:url(font.ttf)}' + css),
                      ('font.ttf', 'font/ttf', font.read_bytes())])
        spine += '<itemref idref="embedded.xhtml"/>'
    _write(path, 'en', 'Font family defaults', items, spine)

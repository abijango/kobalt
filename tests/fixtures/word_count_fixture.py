"""Synthetic logical-text counts, including duplicate and inline TOC anchors."""
import zipfile


def book(path, repeats=1):
    paragraph = '<p>rain<span>bow</span> don\'t café café co&#173;operation A𐐀word 123</p>'
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="uid"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="uid">logical-words</dc:identifier><dc:title>Logical word fixture</dc:title><dc:language>en</dc:language></metadata><manifest><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="c2" href="c2.xhtml" media-type="application/xhtml+xml"/><item id="notes" href="notes.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c1"/><itemref idref="c2"/><itemref idref="notes" linear="no"/></spine></package>')
        destinations = [('first', 'First', 'c1.xhtml#first'), ('part', 'Part', 'c1.xhtml#part'),
                        ('alias', 'Alias', 'c1.xhtml#part'), ('inside', 'Inside', 'c1.xhtml#inside'),
                        ('last', 'Last', 'c2.xhtml#last')]
        nav = ''.join(f'<navPoint id="{identity}" playOrder="{i}"><navLabel><text>{title}</text></navLabel><content src="{href}"/></navPoint>'
                      for i, (identity, title, href) in enumerate(destinations, 1))
        archive.writestr('toc.ncx', '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>Words</text></docTitle><navMap>' + nav + '</navMap></ncx>')
        body = ('<p>Front matter.</p><h1 id="first">First chapter</h1>' + paragraph * repeats +
                '<p>日本語中文 عربي كلمتان עברית שלום 한국어 문장 ภาษาไทย</p>'
                '<p style="display:none">These hidden words must not count.</p>'
                '<ruby>漢字<rt>ruby annotation</rt><rp>ignored fallback</rp></ruby>'
                '<h2 id="part">Second part</h2><p>After inline anchors.</p>'
                '<p><b>un<span id="inside">break</span>able</b></p>')
        archive.writestr('c1.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Ignored title</title></head><body>' + body + '</body></html>')
        archive.writestr('c2.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Ignored title</title></head><body><h1 id="last">Last chapter</h1><p>one two three</p></body></html>')
        archive.writestr('notes.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head/><body><h1>Note</h1><p>Extra note text</p></body></html>')

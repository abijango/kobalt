"""Synthetic EPUB books for the reader tests."""
import struct
import zipfile
import zlib
from xml.sax.saxutils import escape


def book(path, nested=False, chapter_count=3, paragraphs=30, colors=False, rtl=False, cover=False, gif=False, deferred_cover=False, later_images=False, title="Native reader fixture", author="Kobalt", formatting_issue=None):
    prefix = 'OEBPS/' if nested else ''
    paragraph = ('A reader keeps the chapter ready. Changing the size of the words should preserve the passage. '
                 'We can turn the page, visit another chapter, and return to the same place. ')
    progression = 'rtl' if rtl else 'ltr'
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', f'<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="{prefix}book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        manifest = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(1, chapter_count + 1))
        inspection_styles = ''
        if formatting_issue in ('resource-budget', 'total-budget'):
            count = 1 if formatting_issue == 'resource-budget' else 5
            size = 3 * 1024 * 1024 if formatting_issue == 'resource-budget' else 1800000
            for index in range(count):
                name = f'inspection-{index}.css'
                z.writestr(prefix + name, ' ' * size)
                manifest += f'<item id="inspection-{index}" href="{name}" media-type="text/css"/>'
                inspection_styles += f'<link rel="stylesheet" type="text/css" href="{name}"/>'
        if colors or cover or gif:
            def chunk(kind, data):
                return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
            pixels = b''.join(b'\0' + b''.join(bytes((0, 192, 0, 255 if y < 32 else 128)) if x < 64
                else bytes((0, 0, 255, 255 if y < 32 else 128)) for x in range(128)) for y in range(64))
            png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 128, 64, 8, 6, 0, 0, 0))
            if deferred_cover:
                # Keep a declared cover outside the first page and larger than a probe block.
                png += chunk(b'tEXt', b'Comment\0' + b'cover fixture ' * 12000)
            png += chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b'')
            z.writestr(prefix + 'colors.png', png)
            manifest += '<item id="colors" href="colors.png" media-type="image/png"/>'
            z.writestr(prefix + 'artwork/colors.svg',
                '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="384" height="64" viewBox="0 0 384 64">'
                '<rect width="64" height="64" fill="#ff00ff"/>'
                '<text x="72" y="44" font-family="Bitter" font-size="32" fill="#008080">SVG</text>'
                '<image x="200" width="128" height="64" xlink:href="../colors.png"/></svg>')
            manifest += '<item id="svg" href="artwork/colors.svg" media-type="image/svg+xml"/>'
        if gif:
            # Clear the LZW dictionary before each pixel to keep every code three bits.
            codes = [code for y in range(64) for x in range(128) for code in (4, int(x >= 64))] + [5]
            packed = bytearray()
            bits = count = 0
            for code in codes:
                bits |= code << count
                count += 3
                while count >= 8:
                    packed.append(bits & 255)
                    bits >>= 8
                    count -= 8
            if count:
                packed.append(bits & 255)
            data = b'GIF89a' + struct.pack('<HHBBB', 128, 64, 0x80, 0, 0)
            data += bytes((0, 192, 0, 0, 0, 255))
            data += b',' + struct.pack('<HHHHB', 0, 0, 128, 64, 0) + b'\x02'
            data += b''.join(bytes((len(packed[i:i + 255]),)) + packed[i:i + 255] for i in range(0, len(packed), 255))
            z.writestr(prefix + 'colors.gif', data + b'\x00;')
            manifest += '<item id="gif" href="colors.gif" media-type="image/gif"/>'
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, chapter_count + 1))
        cover_meta = f'<meta name="cover" content="{"gif" if gif else "colors"}"/>' if cover else ''
        z.writestr(prefix + 'book.opf', f'<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="uid"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="uid">kobalt-fixture</dc:identifier><dc:title>{escape(title)}</dc:title><dc:language>en</dc:language><dc:creator>{escape(author)}</dc:creator>{cover_meta}</metadata><manifest>{manifest}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx" page-progression-direction="{progression}">{spine}</spine></package>')
        nav = ''.join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chapter {i}</text></navLabel><content src="c{i}.xhtml"/></navPoint>' for i in range(1, chapter_count + 1))
        z.writestr(prefix + 'toc.ncx', f'<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="kobalt-fixture"/></head><docTitle><text>Native reader fixture</text></docTitle><navMap>{nav}</navMap></ncx>')
        for i in range(1, chapter_count + 1):
            body = ''.join(f'<p><span class="koboSpan" id="kobo.{n}.1">{paragraph * 3}</span></p>' for n in range(2, paragraphs + 2))
            if gif:
                body = '<p><img src="colors.gif" alt="GIF test"/></p>' + body
            if colors and (not later_images or i > 1):
                body = ('<p style="color:#ff0000">Publisher red text.</p>'
                        '<p><span style="background-color:#ffff00">Yellow highlight.</span></p>'
                        '<p><img src="colors.png" width="128" height="64" alt="Color test"/></p>'
                        '<p><img src="artwork/colors.svg" width="384" height="64" alt="SVG test"/></p>'
                        '<p><svg xmlns="http://www.w3.org/2000/svg" width="64" height="24" viewBox="0 0 64 24">'
                        '<rect width="64" height="24" fill="#ff8000"/></svg></p>') + body
            if formatting_issue == 'html-entity':
                body += '<p>Readable text&nbsp;with an HTML entity.</p>'
            style = '<link rel="stylesheet" href="missing.css"/>' if formatting_issue == 'missing-css' else ''
            if formatting_issue == 'adobe-template':
                style = '<link rel="stylesheet" type="application/adobe-page-template+xml" href="missing.xpgt"/>'
            if formatting_issue == 'unsupported-writing':
                style = '<style>body { writing-mode: vertical-lr; }</style>'
            style += inspection_styles
            z.writestr(prefix + f'c{i}.xhtml', f'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter {i}</title>{style}</head><body><h1><span class="koboSpan" id="kobo.1.1">Chapter {i}</span></h1>{body}</body></html>')

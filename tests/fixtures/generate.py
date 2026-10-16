#!/usr/bin/env python3
"""Write the regression books, public-domain chapters and the test font to a directory."""
import argparse
import importlib.util
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def load(name):
    # Load by path, so a module with the same name elsewhere on sys.path is never used.
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write(directory, source_root=ROOT):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    reader, dictionary = load('reader_fixture'), load('dictionary_fixture')
    links, publisher = load('link_fixture'), load('publisher_fixture')
    reader.book(directory / 'text.epub', nested=True)
    reader.book(directory / 'local-cache.epub', nested=True, chapter_count=2, paragraphs=300)
    reader.book(directory / 'formatting-issues.epub', nested=True, formatting_issue='missing-css')
    reader.book(directory / 'formatting-entity.epub', nested=True, formatting_issue='html-entity')
    for issue in ('resource-budget', 'total-budget', 'unsupported-writing'):
        reader.book(directory / f'formatting-{issue}.epub', nested=True, formatting_issue=issue)
    reader.book(directory / 'adobe-template.epub', nested=True, formatting_issue='adobe-template')
    reader.book(directory / 'oversized-metadata.epub', nested=True, chapter_count=1, paragraphs=1,
                title='T' * 4095 + '😀' + 'T' * 300000, author='A' * 4095 + '😀' + 'A' * 300000)
    reader.book(directory / 'color.epub', nested=True, colors=True)
    reader.book(directory / 'later-images.epub', nested=True, colors=True, later_images=True)
    reader.book(directory / 'cover.epub', nested=True, cover=True)
    reader.book(directory / 'deferred-cover.epub', nested=True, cover=True, deferred_cover=True)
    reader.book(directory / 'gif.epub', nested=True, cover=True, gif=True)
    reader.book(directory / 'rtl.epub', nested=True, rtl=True)
    load('writing_modes').book(directory / 'writing-modes.epub')
    load('international').write_all(directory)
    load('public_domain').write_all(directory)
    for language in ('ar', 'he'):
        load('multilingual').book(directory / f'multilingual-{language}.epub', language)
    dictionary.book(directory / 'dictionary.epub')
    load('word_count_fixture').book(directory / 'word-counts.epub')
    load('word_count_fixture').book(directory / 'word-count-many.epub', repeats=1500)
    links.book(directory / 'links.epub')
    publisher.book(directory / 'publisher.epub')
    publisher.book(directory / 'publisher-non-linear.epub', svg_spine=False)
    publisher.equations_book(directory / 'equations.epub')
    publisher.embedded_fonts_book(directory / 'embedded-fonts.epub', Path(source_root) / 'resources/fonts/IBMPlexSans-Regular.ttf')
    publisher.font_families_book(directory / 'font-families.epub')
    publisher.font_families_book(directory / 'font-families-embedded.epub', Path(source_root) / 'resources/fonts/IBMPlexSans-Regular.ttf')
    publisher.german_book(directory / 'publisher-de.epub')
    publisher.russian_book(directory / 'publisher-ru.epub')
    publisher.hyphenation_books(directory)
    for language in ('ja', 'ko', 'zh-Hans', 'zh-Hant', 'zh-HK'):
        publisher.cjk_book(directory / f'cjk-{language}.epub', language)
    publisher.cjk_book(directory / 'cjk-inline.epub', 'ja', 'zh-Hans')
    shutil.copyfile(Path(source_root) / 'resources/fonts/IBMPlexSans-Regular.ttf', directory / 'font.ttf')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    write(args.directory)


if __name__ == '__main__':
    main()

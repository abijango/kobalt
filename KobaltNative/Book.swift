import UIKit

struct BookBlock {
    var role: Role
    var text: String

    enum Role {
        case chapterNumber
        case title
        case heading
        case paragraph
        case specimenLabel
        case specimenLine
        case quotation
    }
}

struct BookDocument {
    var title: String
    var blocks: [BookBlock]
    var image: UIImage?
}

enum BookLoader {
    static let spine = [
        "cover.png", "title.xhtml", "prologue.xhtml",
        "chapter1.xhtml", "chapter2.xhtml", "chapter3.xhtml", "chapter4.xhtml",
        "chapter5.xhtml", "chapter6.xhtml", "chapter7.xhtml", "chapter8.xhtml",
        "epilogue.xhtml",
    ]

    static func load() throws -> [BookDocument] {
        try spine.map { name in
            guard let url = Bundle.main.url(forResource: name, withExtension: nil) else {
                throw Failure.missing(name)
            }
            if name.hasSuffix(".png") {
                guard let image = UIImage(contentsOfFile: url.path) else { throw Failure.missing(name) }
                return BookDocument(title: "Cover", blocks: [], image: image)
            }
            let data = try Data(contentsOf: url)
            let blocks = try XHTMLBlocks.parse(data)
            let title = blocks.first { $0.role == .title }?.text
                ?? blocks.first { $0.role == .heading }?.text
                ?? name
            return BookDocument(title: title, blocks: blocks, image: nil)
        }
    }

    enum Failure: Error, CustomStringConvertible {
        case missing(String)
        case xml(String)

        var description: String {
            switch self {
            case .missing(let name): return "The sample book is missing \(name)."
            case .xml(let name): return "Could not read \(name)."
            }
        }
    }
}

private final class XHTMLBlocks: NSObject, XMLParserDelegate {
    private var blocks: [BookBlock] = []
    private var activeTag = ""
    private var activeClasses = Set<String>()
    private var buffer = ""
    private var capturing = false

    static func parse(_ data: Data) throws -> [BookBlock] {
        var source = String(decoding: data, as: UTF8.self)
        if let start = source.range(of: "<html") {
            source = String(source[start.lowerBound...])
        }
        let reader = XHTMLBlocks()
        let parser = XMLParser(data: Data(source.utf8))
        parser.delegate = reader
        guard parser.parse() else { throw BookLoader.Failure.xml(parser.parserError?.localizedDescription ?? "XML") }
        return reader.blocks
    }

    func parser(
        _ parser: XMLParser, didStartElement name: String, namespaceURI: String?, qualifiedName: String?,
        attributes: [String: String] = [:]
    ) {
        let tag = name.lowercased()
        let classes = Set((attributes["class"] ?? "").split(separator: " ").map(String.init))
        guard !capturing, isBlock(tag, classes) else { return }
        activeTag = tag
        activeClasses = classes
        buffer = ""
        capturing = true
    }

    func parser(_ parser: XMLParser, foundCharacters string: String) {
        guard capturing else { return }
        buffer += string
    }

    func parser(_ parser: XMLParser, didEndElement name: String, namespaceURI: String?, qualifiedName: String?) {
        guard capturing, name.lowercased() == activeTag else { return }
        let text = buffer.replacingOccurrences(of: "\\s+", with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        capturing = false
        buffer = ""
        guard !text.isEmpty else { return }
        blocks.append(BookBlock(role: role(activeTag, activeClasses), text: text))
    }

    private func isBlock(_ tag: String, _ classes: Set<String>) -> Bool {
        if tag == "h1" || tag == "h2" || tag == "h3" || tag == "p" || tag == "blockquote" { return true }
        return tag == "div" && !classes.isDisjoint(with: ["kern-label", "kern-row"])
    }

    private func role(_ tag: String, _ classes: Set<String>) -> BookBlock.Role {
        if classes.contains("chapter-num") { return .chapterNumber }
        if tag == "h1" { return .title }
        if tag == "h2" || tag == "h3" { return .heading }
        if tag == "blockquote" { return .quotation }
        if classes.contains("kern-label") { return .specimenLabel }
        if classes.contains("kern-row") { return .specimenLine }
        return .paragraph
    }
}

enum BookText {
    static func make(_ document: BookDocument, dark: Bool) -> NSAttributedString {
        let result = NSMutableAttributedString()
        let ink = dark ? UIColor.white : UIColor.black
        let secondary = dark ? UIColor(white: 0.75, alpha: 1) : UIColor(white: 0.28, alpha: 1)
        for block in document.blocks {
            if result.length > 0 { result.append(NSAttributedString(string: "\n")) }
            result.append(NSAttributedString(string: block.text, attributes: attributes(block.role, ink: ink, secondary: secondary)))
        }
        return result
    }

    private static func serif(_ size: CGFloat, weight: UIFont.Weight) -> UIFont {
        let base = UIFont.systemFont(ofSize: size, weight: weight)
        guard let designed = base.fontDescriptor.withDesign(.serif) else { return base }
        return UIFont(descriptor: designed, size: size)
    }

    private static func attributes(_ role: BookBlock.Role, ink: UIColor, secondary: UIColor) -> [NSAttributedString.Key: Any] {
        let style = NSMutableParagraphStyle()
        style.hyphenationFactor = 0.85
        let size: CGFloat
        let weight: UIFont.Weight
        let color: UIColor
        switch role {
        case .chapterNumber:
            size = 12
            weight = .regular
            color = secondary
            style.alignment = .center
            style.paragraphSpacing = 4
            style.minimumLineHeight = 16
            style.maximumLineHeight = 16
        case .title:
            size = 28
            weight = .semibold
            color = ink
            style.alignment = .center
            style.paragraphSpacing = 22
            style.minimumLineHeight = 34
            style.maximumLineHeight = 34
        case .heading:
            size = 20
            weight = .semibold
            color = ink
            style.alignment = .left
            style.paragraphSpacingBefore = 18
            style.paragraphSpacing = 10
            style.minimumLineHeight = 26
            style.maximumLineHeight = 26
        case .quotation:
            size = 18
            weight = .regular
            color = ink
            style.alignment = .left
            style.firstLineHeadIndent = 18
            style.headIndent = 18
            style.paragraphSpacing = 14
            style.minimumLineHeight = 27
            style.maximumLineHeight = 27
        case .specimenLabel:
            size = 13
            weight = .semibold
            color = secondary
            style.alignment = .left
            style.paragraphSpacingBefore = 14
            style.paragraphSpacing = 2
            style.minimumLineHeight = 18
            style.maximumLineHeight = 18
        case .specimenLine:
            size = 18
            weight = .regular
            color = ink
            style.alignment = .left
            style.paragraphSpacing = 2
            style.minimumLineHeight = 26
            style.maximumLineHeight = 26
            style.hyphenationFactor = 0
        case .paragraph:
            size = 20
            weight = .regular
            color = ink
            style.alignment = .left
            style.firstLineHeadIndent = 18
            style.paragraphSpacing = 10
            style.minimumLineHeight = 29
            style.maximumLineHeight = 29
        }
        return [
            .font: serif(size, weight: weight),
            .foregroundColor: color,
            .paragraphStyle: style,
        ]
    }
}

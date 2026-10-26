import UIKit

final class ReaderController: UIViewController {
    private let pageView = PageView()

    override func loadView() {
        view = pageView
    }

    override var prefersStatusBarHidden: Bool { true }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
}

final class PageView: UIView {
    private var documents: [BookDocument] = []
    private var pages: [Page] = []
    private var index = 0
    private var anchor = 0
    private var paginatedSize = CGSize.zero
    private var openMilliseconds: Double?
    private var glyphMilliseconds: Double?
    private var turnMilliseconds: Double?
    private var turnStarted: CFTimeInterval?
    private var failure: String?
    private let openedAt = CACurrentMediaTime()

    override init(frame: CGRect) {
        super.init(frame: frame)
        isOpaque = true
        contentMode = .redraw
        let tap = UITapGestureRecognizer(target: self, action: #selector(tapped))
        addGestureRecognizer(tap)
        do {
            documents = try BookLoader.load()
        } catch let error as BookLoader.Failure {
            failure = error.description
        } catch {
            failure = error.localizedDescription
        }
    }

    required init?(coder: NSCoder) { nil }

    override func layoutSubviews() {
        super.layoutSubviews()
        paginateIfNeeded()
    }

    override func traitCollectionDidChange(_ previous: UITraitCollection?) {
        super.traitCollectionDidChange(previous)
        guard traitCollection.userInterfaceStyle != previous?.userInterfaceStyle else { return }
        paginatedSize = .zero
        paginateIfNeeded()
    }

    override func draw(_ rect: CGRect) {
        let paper = traitCollection.userInterfaceStyle == .dark ? UIColor.black : UIColor.white
        paper.setFill()
        UIRectFill(bounds)
        guard failure == nil, pages.indices.contains(index) else {
            drawMessage(failure ?? "Opening…")
            return
        }
        let page = pages[index]
        let area = textArea
        let glyphStart = CACurrentMediaTime()
        if let image = page.image {
            let fitted = aspectFit(image.size, in: area)
            image.draw(in: fitted)
        } else if let range = page.glyphRange, let manager = page.manager {
            manager.drawBackground(forGlyphRange: range, at: area.origin)
            manager.drawGlyphs(forGlyphRange: range, at: area.origin)
        }
        glyphMilliseconds = (CACurrentMediaTime() - glyphStart) * 1000
        if openMilliseconds == nil {
            openMilliseconds = (CACurrentMediaTime() - openedAt) * 1000
        }
        if let started = turnStarted {
            turnMilliseconds = (CACurrentMediaTime() - started) * 1000
            turnStarted = nil
        }
        drawChrome()
    }

    @objc private func tapped(_ gesture: UITapGestureRecognizer) {
        let x = gesture.location(in: self).x
        let delta = x < bounds.width * 0.3 ? -1 : x > bounds.width * 0.7 ? 1 : 0
        guard delta != 0 else { return }
        let next = index + delta
        guard pages.indices.contains(next) else { return }
        turnStarted = CACurrentMediaTime()
        anchor = pages[next].character
        index = next
        setNeedsDisplay()
    }

    private var textArea: CGRect {
        let safe = safeAreaInsets
        return bounds.inset(by: UIEdgeInsets(top: safe.top + 36, left: safe.left + 28, bottom: safe.bottom + 44, right: safe.right + 28))
    }

    private func paginateIfNeeded() {
        let size = textArea.size
        guard size.width > 40, size.height > 80, size != paginatedSize else { return }
        paginatedSize = size
        let dark = traitCollection.userInterfaceStyle == .dark
        var built: [Page] = []
        var characterBase = 0
        for document in documents {
            let text = BookText.make(document, dark: dark)
            let made = Page.make(document: document, text: text, size: size, characterBase: characterBase)
            built.append(contentsOf: made)
            characterBase += max(text.length, 1) + 1
        }
        pages = built
        if pages.isEmpty {
            failure = "This page size is too small to lay out."
            return
        }
        index = pages.lastIndex { $0.character <= anchor } ?? 0
        setNeedsDisplay()
    }

    private func drawChrome() {
        let ink = traitCollection.userInterfaceStyle == .dark ? UIColor.white : UIColor.black
        let gray = UIColor(white: traitCollection.userInterfaceStyle == .dark ? 0.7 : 0.4, alpha: 1)
        let safe = safeAreaInsets
        let title = pages[index].title as NSString
        let titleFont = UIFont.systemFont(ofSize: 12, weight: .medium)
        title.draw(in: CGRect(x: safe.left + 28, y: safe.top + 12, width: bounds.width - safe.left - safe.right - 56, height: 18),
                   withAttributes: [.font: titleFont, .foregroundColor: gray])
        let count = pages.count
        let percent = count > 1 ? Int((Double(index) / Double(count - 1) * 100).rounded()) : 100
        let open = openMilliseconds.map { String(format: "open %.0f ms", $0) } ?? "open …"
        let glyphs = glyphMilliseconds.map { String(format: "draw %.1f ms", $0) } ?? ""
        let turn = turnMilliseconds.map { String(format: "  ·  turn %.1f ms", $0) } ?? ""
        let status = "\(index + 1) / \(count)    \(percent)%    \(open)  ·  \(glyphs)\(turn)" as NSString
        let footerFont = UIFont.monospacedDigitSystemFont(ofSize: 11, weight: .regular)
        status.draw(in: CGRect(x: safe.left + 28, y: bounds.height - safe.bottom - 28, width: bounds.width - safe.left - safe.right - 56, height: 16),
                    withAttributes: [.font: footerFont, .foregroundColor: ink])
    }

    private func drawMessage(_ text: String) {
        let ink = traitCollection.userInterfaceStyle == .dark ? UIColor.white : UIColor.black
        (text as NSString).draw(in: bounds.insetBy(dx: 32, dy: 32), withAttributes: [
            .font: UIFont.systemFont(ofSize: 17),
            .foregroundColor: ink,
        ])
    }

    private func aspectFit(_ size: CGSize, in area: CGRect) -> CGRect {
        guard size.width > 0, size.height > 0 else { return area }
        let scale = min(area.width / size.width, area.height / size.height)
        let fitted = CGSize(width: size.width * scale, height: size.height * scale)
        return CGRect(x: area.midX - fitted.width / 2, y: area.midY - fitted.height / 2, width: fitted.width, height: fitted.height)
    }
}

private struct Page {
    var title: String
    var character: Int
    var image: UIImage?
    var manager: NSLayoutManager?
    var glyphRange: NSRange?

    static func make(document: BookDocument, text: NSAttributedString, size: CGSize, characterBase: Int) -> [Page] {
        if let image = document.image {
            return [Page(title: document.title, character: characterBase, image: image, manager: nil, glyphRange: nil)]
        }
        guard text.length > 0 else { return [] }
        let storage = NSTextStorage(attributedString: text)
        let manager = NSLayoutManager()
        storage.addLayoutManager(manager)
        var pages: [Page] = []
        var laid = 0
        while pages.count < 5000 {
            let container = NSTextContainer(size: size)
            container.lineFragmentPadding = 0
            manager.addTextContainer(container)
            manager.ensureLayout(for: container)
            let range = manager.glyphRange(for: container)
            if range.length == 0 { break }
            let character = characterBase + manager.characterIndexForGlyph(at: range.location)
            pages.append(Page(title: document.title, character: character, image: nil, manager: manager, glyphRange: range))
            laid = NSMaxRange(range)
            if laid >= manager.numberOfGlyphs { break }
        }
        return pages
    }
}

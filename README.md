# Kobalt Native

A personal iPad and iPhone reader for comparing a native UIKit screen with the Qt Kobalt port. It opens the same public-domain book, [Trevelyan's Type Tester v1.2](https://github.com/nicoverbruggen/type-tester-epub/releases/tag/v1.2).

The pages are laid out with TextKit. They will not match Kobalt's CREngine page breaks, fonts, or hyphenation. The footer times how long this app took to open and how long the last native draw took, so both apps can be watched on the same iPad.

Tap the left edge for the previous page and the right edge for the next. The paper follows the system appearance: white in light mode, black in dark mode.

## Run

Open `KobaltNative.xcodeproj` in Xcode 15 or later, select your team, and run it on an iPad. The deployment target is iOS 17. This repository does not include a build; it has to be compiled on a Mac.

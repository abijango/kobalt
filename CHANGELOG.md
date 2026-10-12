## Unreleased

### What's new

Optional Hardcover.app reading-progress sync on Kobo when NickelHardcover is installed. The switch is in Settings → Advanced and stays off until turned on. Progress is sent when a book closes or the device sleeps, and waits for Wi-Fi without delaying sleep.

### What's changed

Sleep writes the current reading position to Nickel, as closing a book already does.

Hardcover progress sync logs the command's stdout, stderr and exit reason, and only passes flags that the installed NickelHardcover command accepts. Pending updates are retried when Wi-Fi connects.

Sleep now commits the reading position to Nickel's database before Nickel continues into sleep. Previously the write was left in the event queue, so the sleep screen and library kept the older percent.

## v1.0-beta.2

### Fixed

- Invalidate parsed document caches when the Kobalt release version changes. Remove older entries for the same book after saving a new cache.

- Fixed opening and appearance previews for EPUBs whose structural CSS selectors require styles to be recalculated after parsing. Publisher image spacing and centered text are preserved.

- Fixed opening EPUBs with repeated identical manifest items. Conflicting duplicate IDs remain unsupported.

- Fixed opening EPUBs with standard IDPF-obfuscated embedded TrueType or OpenType fonts.

- Added evenly spaced line-spacing slider marks with snapping at 100%, 110%, 120%, 130%, 140%, 150%, 175%, 200%, 250% and 300%. Plus and minus still adjust spacing by one percentage point.

- Fixed line spacing being applied twice. 150% now means 1.5 times the font size. Existing book settings are converted to preserve their approximate spacing, with 122% becoming 150% and a maximum of 300%.

- Fixed left alignment and justification moving publisher-centered text to the left.

- Fixed reversed landscape touch targets on affected Clara devices with firmware 4, using NickelRotate’s coordinate correction.

- Fixed opening and saved chapter lookup for sideloaded filenames containing `#`, `?` or literal percent escapes on firmware 4. If files fail to open, an explanation is logged.

- Fixed opening DRM-free local EPUBs regardless of their folder, including hidden folders, SD storage, Dropbox, Google Drive and third-party sync services. Protected books still open in the built-in reader.

- Fixed missing Chinese, Japanese, Korean and other bundled fallback characters in chapter titles and interface controls. CJK fallback text and CFF font previews use Kobalt’s font renderer to avoid crashes in the firmware font driver.

- Load private interface fallback fonts when text first needs them, including the CJK collection. This is more memory efficient.

## v1.0-beta.1

### What's New

First public Kobo beta, using Qt5 on firmware 4.38 and later.
